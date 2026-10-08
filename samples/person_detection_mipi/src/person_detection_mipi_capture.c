/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "person_detection_mipi_capture.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "person_detection_mipi_stream.h"

#define MIPI_DEV_NODE DT_NODELABEL(video_syna0)

#if !DT_NODE_EXISTS(MIPI_DEV_NODE)
#error "Devicetree node 'video_syna0' is required for this sample"
#endif

#if !DT_NODE_HAS_PROP(MIPI_DEV_NODE, memory_region)
#error "video_syna0 must provide a memory-region for live MIPI capture"
#endif

#define CAPTURE_MEM_NODE DT_PHANDLE(MIPI_DEV_NODE, memory_region)
#define CAPTURE_MEM_BASE DT_REG_ADDR(CAPTURE_MEM_NODE)
#define CAPTURE_MEM_SIZE DT_REG_SIZE(CAPTURE_MEM_NODE)

#define APP_CAPTURE_TIMEOUT K_SECONDS(5)
#define APP_CAPTURE_FIRST_FRAME_TIMEOUT K_SECONDS(5)
#define APP_CAPTURE_FRMIVAL_NUMERATOR 1U
#define APP_CAPTURE_FRMIVAL_DENOMINATOR 3U
#define APP_CAPTURE_BUFFER_COUNT 2U

/* The MIPI driver always DMA-writes one raw frame at the pool base. */
#define APP_CAPTURE_DMA_STAGING_OFFSET 0U
#define APP_CAPTURE_FULL_LATEST_FRAME_OFFSET \
	(APP_CAPTURE_DMA_STAGING_OFFSET + PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE)
#define APP_CAPTURE_LATEST_FRAME_OFFSET \
	(APP_CAPTURE_FULL_LATEST_FRAME_OFFSET + PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE)
#define APP_CAPTURE_SCRATCH_FRAME_OFFSET \
	(APP_CAPTURE_LATEST_FRAME_OFFSET + PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE)
#define APP_CAPTURE_INPUT_BUF_OFFSET \
	(APP_CAPTURE_SCRATCH_FRAME_OFFSET + PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE)
#define APP_CAPTURE_RESERVED_TOTAL_SIZE APP_CAPTURE_INPUT_BUF_OFFSET

BUILD_ASSERT(CAPTURE_MEM_SIZE >= APP_CAPTURE_RESERVED_TOTAL_SIZE,
	     "Capture reserved memory is too small for frame buffers");

static uint8_t *const app_capture_mem = (uint8_t *)CAPTURE_MEM_BASE;

enum capture_state {
	CAPTURE_STATE_UNINITIALIZED = 0,
	CAPTURE_STATE_READY,
	CAPTURE_STATE_RUNNING,
	CAPTURE_STATE_STOPPED,
};

struct capture_context {
	struct k_mutex lock;
	struct k_sem first_frame_sem;
	struct k_sem worker_stopped_sem;
	struct k_thread worker_thread;
	const struct device *mipi_dev;
	struct video_caps caps;
	struct video_format fmt;
	struct video_frmival frmival;
	struct video_buffer *vbufs[APP_CAPTURE_BUFFER_COUNT];
	enum capture_state state;
	bool initialized;
	bool stop_requested;
	bool stream_started;
	bool buffers_queued;
	bool latest_valid;
	int worker_result;
	uint32_t latest_bytesused;
	uint32_t latest_timestamp;
	uint32_t latest_sequence;
	uint32_t frames_captured;
};

static struct capture_context app_ctx;

K_THREAD_STACK_DEFINE(capture_worker_stack, 4096);

static void capture_context_init(struct capture_context *ctx)
{
	if (ctx->initialized) {
		return;
	}

	k_mutex_init(&ctx->lock);
	k_sem_init(&ctx->first_frame_sem, 0, 1);
	k_sem_init(&ctx->worker_stopped_sem, 0, 1);
	ctx->initialized = true;
}

static void drain_buffers(const struct device *dev)
{
	struct video_buffer *vbuf = NULL;
	uint32_t drained = 0U;

	while ((video_dequeue(dev, &vbuf, K_NO_WAIT) == 0) && (vbuf != NULL)) {
		drained++;
		vbuf->bytesused = 0U;
	}

	if (drained > 0U) {
		printk("person_detection_mipi: drained %u returned capture buffer(s)\n", drained);
	}
}

static int prepare_device_locked(struct capture_context *ctx)
{
	ctx->mipi_dev = DEVICE_DT_GET(MIPI_DEV_NODE);

	if (!device_is_ready(ctx->mipi_dev)) {
		return -ENODEV;
	}

	memset(&ctx->caps, 0, sizeof(ctx->caps));
	ctx->caps.type = VIDEO_BUF_TYPE_OUTPUT;

	return video_get_caps(ctx->mipi_dev, &ctx->caps);
}

static int configure_mipi_locked(struct capture_context *ctx)
{
	int ret;

	ctx->fmt = (struct video_format) {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_PIX_FMT_SRGGB8,
		.width = PERSON_DETECTION_MIPI_CAPTURE_WIDTH,
		.height = PERSON_DETECTION_MIPI_CAPTURE_HEIGHT,
		.pitch = PERSON_DETECTION_MIPI_CAPTURE_PITCH,
	};

	ret = video_set_format(ctx->mipi_dev, &ctx->fmt);
	if (ret != 0) {
		return ret;
	}

	printk("person_detection_mipi: negotiated fmt width=%u height=%u pitch=%u size=%u\n",
	       ctx->fmt.width, ctx->fmt.height, ctx->fmt.pitch, ctx->fmt.size);

	ctx->frmival = (struct video_frmival) {
		.numerator = APP_CAPTURE_FRMIVAL_NUMERATOR,
		.denominator = APP_CAPTURE_FRMIVAL_DENOMINATOR,
	};

	ret = video_set_frmival(ctx->mipi_dev, &ctx->frmival);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

static int prepare_capture_buffers(struct capture_context *ctx)
{
	const size_t align = MAX(ctx->caps.buf_align, 1U);

	if (ctx->caps.min_vbuf_count > APP_CAPTURE_BUFFER_COUNT) {
		return -ENOMEM;
	}

	for (uint32_t i = 0U; i < APP_CAPTURE_BUFFER_COUNT; i++) {
		if (ctx->vbufs[i] == NULL) {
			ctx->vbufs[i] = video_buffer_aligned_alloc(ctx->fmt.size, align,
								   K_NO_WAIT);
			if (ctx->vbufs[i] == NULL) {
				printk("person_detection_mipi: capture buffer[%u] alloc failed\n",
				       i);
				return -ENOMEM;
			}
		}

		ctx->vbufs[i]->driver_data = NULL;
		ctx->vbufs[i]->type = VIDEO_BUF_TYPE_OUTPUT;
		ctx->vbufs[i]->bytesused = 0U;
		ctx->vbufs[i]->timestamp = 0U;
		printk("person_detection_mipi: capture buffer[%u] addr=%p size=%u\n", i,
		       ctx->vbufs[i]->buffer, ctx->vbufs[i]->size);
	}

	return 0;
}

static int queue_capture_buffers(struct capture_context *ctx)
{
	for (uint32_t i = 0U; i < APP_CAPTURE_BUFFER_COUNT; i++) {
		printk("person_detection_mipi: queue buffer[%u]\n", i);
		int ret = video_enqueue(ctx->mipi_dev, ctx->vbufs[i]);

		if (ret != 0) {
			printk("person_detection_mipi: queue buffer[%u] failed: %d\n", i, ret);
			return ret;
		}

		ctx->buffers_queued = true;
	}

	return 0;
}

static int copy_latest_frame_locked(struct capture_context *ctx, const struct video_buffer *src)
{
	if ((src == NULL) || (src->buffer == NULL)) {
		return -EINVAL;
	}

	if (src->bytesused < PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE) {
		return -EINVAL;
	}

	memcpy(app_capture_mem + APP_CAPTURE_FULL_LATEST_FRAME_OFFSET,
	       src->buffer, PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE);

	/* Match the parent usecase: crop the top-left 480x256 model window in place. */
	for (size_t row = 0U; row < 256U; row++) {
		memcpy(app_capture_mem + APP_CAPTURE_LATEST_FRAME_OFFSET +
		       (row * PERSON_DETECTION_MIPI_CAPTURE_WIDTH),
		       (const uint8_t *)src->buffer + (row * PERSON_DETECTION_MIPI_CAPTURE_PITCH),
		       PERSON_DETECTION_MIPI_CAPTURE_WIDTH);
	}
	ctx->latest_valid = true;
	ctx->latest_bytesused = PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE;
	ctx->latest_timestamp = src->timestamp;
	ctx->latest_sequence++;
	ctx->frames_captured++;

	return 0;
}

static void capture_worker(void *arg1, void *arg2, void *arg3)
{
	struct capture_context *ctx = arg1;
	struct video_buffer *captured = NULL;
	bool started = false;
	int ret;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	ret = prepare_capture_buffers(ctx);
	if (ret != 0) {
		printk("person_detection_mipi: capture worker: prepare buffers failed: %d\n", ret);
		goto out;
	}

	ret = queue_capture_buffers(ctx);
	if (ret != 0) {
		printk("person_detection_mipi: capture worker: queue buffers failed: %d\n", ret);
		goto out;
	}

	ret = video_stream_start(ctx->mipi_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret != 0) {
		printk("person_detection_mipi: capture worker: stream start failed: %d\n", ret);
		goto out;
	}
	started = true;

	while (!ctx->stop_requested) {
		ret = video_dequeue(ctx->mipi_dev, &captured, APP_CAPTURE_TIMEOUT);
		if (ret != 0) {
			if (ctx->stop_requested) {
				break;
			}
			printk("person_detection_mipi: capture worker: dequeue failed: %d\n", ret);
			goto out;
		}

		if (captured == NULL) {
			ret = -EIO;
			printk("person_detection_mipi: capture worker: dequeue returned NULL buffer\n");
			goto out;
		}

		k_mutex_lock(&ctx->lock, K_FOREVER);
		ret = copy_latest_frame_locked(ctx, captured);
		k_mutex_unlock(&ctx->lock);
		if (ret != 0) {
			printk("person_detection_mipi: capture worker: copy latest failed: %d\n", ret);
			goto out;
		}

		if ((ctx->latest_sequence <= 3U) || ((ctx->latest_sequence % 30U) == 0U)) {
			printk("person_detection_mipi: capture worker: frame seq=%u bytes=%u timestamp=%u total=%u\n",
			       ctx->latest_sequence, captured->bytesused, captured->timestamp,
			       ctx->frames_captured);
		}

		if (ctx->latest_sequence == 1U) {
			k_sem_give(&ctx->first_frame_sem);
		}

		ret = video_enqueue(ctx->mipi_dev, captured);
		captured = NULL;
		if (ret != 0) {
			printk("person_detection_mipi: capture worker: requeue failed: %d\n", ret);
			goto out;
		}

		ret = person_detection_mipi_stream_process_frame(
			app_capture_mem + APP_CAPTURE_FULL_LATEST_FRAME_OFFSET,
			PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE, ctx->latest_sequence);
		if ((ret != 0) && (ret != -ENOTCONN) && (ret != -EAGAIN) &&
		    (ret != -ECANCELED)) {
			printk("person_detection_mipi: capture worker: stream process failed: %d\n",
			       ret);
		}
	}

out:
	if (captured != NULL) {
		(void)video_enqueue(ctx->mipi_dev, captured);
	}

	if (started) {
		(void)video_stream_stop(ctx->mipi_dev, VIDEO_BUF_TYPE_OUTPUT);
	}

	if (ctx->buffers_queued) {
		(void)video_flush(ctx->mipi_dev, true);
		drain_buffers(ctx->mipi_dev);
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->worker_result = ret;
	ctx->stream_started = false;
	ctx->buffers_queued = false;
	ctx->state = CAPTURE_STATE_READY;
	k_mutex_unlock(&ctx->lock);

	k_sem_give(&ctx->worker_stopped_sem);
}

int person_detection_mipi_capture_init(void)
{
	struct capture_context *ctx = &app_ctx;
	int ret;

	capture_context_init(ctx);

	k_mutex_lock(&ctx->lock, K_FOREVER);

	if (ctx->state != CAPTURE_STATE_UNINITIALIZED) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	ret = prepare_device_locked(ctx);
	if (ret != 0) {
		printk("person_detection_mipi: video device preparation failed: %d\n", ret);
		k_mutex_unlock(&ctx->lock);
		return ret;
	}

	ret = configure_mipi_locked(ctx);
	if (ret != 0) {
		printk("person_detection_mipi: video format configuration failed: %d\n", ret);
		k_mutex_unlock(&ctx->lock);
		return ret;
	}

	ctx->state = CAPTURE_STATE_READY;

	k_mutex_unlock(&ctx->lock);
	return 0;
}

int person_detection_mipi_capture_start(void)
{
	struct capture_context *ctx = &app_ctx;
	int ret;

	capture_context_init(ctx);

	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (ctx->state != CAPTURE_STATE_READY && ctx->state != CAPTURE_STATE_STOPPED) {
		k_mutex_unlock(&ctx->lock);
		return -EALREADY;
	}

	if (ctx->stream_started) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	/*
	 * Re-apply the MIPI configuration for every start cycle. The first cold
	 * boot start works reliably, but repeated start/stop cycles can leave the
	 * sensor/driver path needing an explicit refresh before streaming again.
	 */
	ret = prepare_device_locked(ctx);
	if (ret != 0) {
		k_mutex_unlock(&ctx->lock);
		printk("person_detection_mipi: start: video device preparation failed: %d\n", ret);
		return ret;
	}

	ret = configure_mipi_locked(ctx);
	if (ret != 0) {
		k_mutex_unlock(&ctx->lock);
		printk("person_detection_mipi: start: video format reconfiguration failed: %d\n",
		       ret);
		return ret;
	}

	ctx->stop_requested = false;
	ctx->frames_captured = 0U;
	ctx->latest_sequence = 0U;
	ctx->latest_valid = false;
	ctx->worker_result = 0;
	k_sem_reset(&ctx->first_frame_sem);
	k_sem_reset(&ctx->worker_stopped_sem);
	ctx->stream_started = true;
	k_mutex_unlock(&ctx->lock);

	k_thread_create(&ctx->worker_thread, capture_worker_stack,
			K_THREAD_STACK_SIZEOF(capture_worker_stack),
			capture_worker, ctx, NULL, NULL,
			CONFIG_HOST_API_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&ctx->worker_thread, "pd_mipi_capture");

	ret = k_sem_take(&ctx->first_frame_sem, APP_CAPTURE_FIRST_FRAME_TIMEOUT);
	if (ret != 0) {
		k_mutex_lock(&ctx->lock, K_FOREVER);
		ctx->stop_requested = true;
		k_mutex_unlock(&ctx->lock);
		(void)k_sem_take(&ctx->worker_stopped_sem, APP_CAPTURE_TIMEOUT);
		k_mutex_lock(&ctx->lock, K_FOREVER);
		ret = (ctx->worker_result != 0) ? ctx->worker_result : ret;
		k_mutex_unlock(&ctx->lock);
		return ret;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->state = CAPTURE_STATE_RUNNING;
	k_mutex_unlock(&ctx->lock);

	return 0;
}

int person_detection_mipi_capture_stop(void)
{
	struct capture_context *ctx = &app_ctx;
	int ret;

	capture_context_init(ctx);

	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (!ctx->stream_started) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	ctx->stop_requested = true;
	k_mutex_unlock(&ctx->lock);

	ret = k_sem_take(&ctx->worker_stopped_sem, APP_CAPTURE_TIMEOUT);
	if (ret != 0) {
		return ret;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->state = CAPTURE_STATE_STOPPED;
	k_mutex_unlock(&ctx->lock);

	return 0;
}

int person_detection_mipi_capture_get_latest(struct person_detection_mipi_capture_frame *frame,
					     uint8_t *dst, size_t dst_size)
{
	struct capture_context *ctx = &app_ctx;
	int ret = 0;

	if ((frame == NULL) || (dst == NULL)) {
		return -EINVAL;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (!ctx->latest_valid) {
		ret = -EAGAIN;
		goto out;
	}

	if (dst_size < PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE) {
		ret = -EINVAL;
		goto out;
	}

	memcpy(dst, app_capture_mem + APP_CAPTURE_LATEST_FRAME_OFFSET,
	       PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE);
	frame->data = dst;
	frame->size = PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE;
	frame->width = PERSON_DETECTION_MIPI_CAPTURE_WIDTH;
	frame->height = 256U;
	frame->pitch = PERSON_DETECTION_MIPI_CAPTURE_PITCH;
	frame->bytesused = ctx->latest_bytesused;
	frame->timestamp = ctx->latest_timestamp;
	frame->sequence = ctx->latest_sequence;
	frame->valid = true;

out:
	k_mutex_unlock(&ctx->lock);
	return ret;
}

int person_detection_mipi_capture_get_latest_full(struct person_detection_mipi_capture_frame *frame,
						  uint8_t *dst, size_t dst_size)
{
	struct capture_context *ctx = &app_ctx;
	int ret = 0;

	if ((frame == NULL) || (dst == NULL)) {
		return -EINVAL;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (!ctx->latest_valid) {
		ret = -EAGAIN;
		goto out;
	}

	if (dst_size < PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE) {
		ret = -EINVAL;
		goto out;
	}

	memcpy(dst, app_capture_mem + APP_CAPTURE_FULL_LATEST_FRAME_OFFSET,
	       PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE);
	frame->data = dst;
	frame->size = PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE;
	frame->width = PERSON_DETECTION_MIPI_CAPTURE_WIDTH;
	frame->height = PERSON_DETECTION_MIPI_CAPTURE_HEIGHT;
	frame->pitch = PERSON_DETECTION_MIPI_CAPTURE_PITCH;
	frame->bytesused = PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE;
	frame->timestamp = ctx->latest_timestamp;
	frame->sequence = ctx->latest_sequence;
	frame->valid = true;

out:
	k_mutex_unlock(&ctx->lock);
	return ret;
}

uint8_t *person_detection_mipi_capture_scratch_frame_buffer(void)
{
	return app_capture_mem + APP_CAPTURE_SCRATCH_FRAME_OFFSET;
}
