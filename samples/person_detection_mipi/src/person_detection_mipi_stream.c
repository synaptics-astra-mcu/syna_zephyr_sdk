/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "person_detection_mipi_stream.h"

#include "person_detection_mipi_app.h"
#include "person_detection_mipi_capture.h"
#include "stream_cdc_transport.h"

#include <errno.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/printk.h>
#include <zephyr/video/video.h>

#define ENC_NODE DT_NODELABEL(enc0)

#if !DT_NODE_EXISTS(ENC_NODE)
#error "Devicetree node 'enc0' is required for person detection streaming"
#endif

#if !DT_NODE_HAS_COMPAT(ENC_NODE, syna_enc_video)
#error "enc0 must use compatible = \"syna,enc-video\""
#endif

#define APP_STREAM_WIDTH PERSON_DETECTION_MIPI_CAPTURE_WIDTH
#define APP_STREAM_HEIGHT PERSON_DETECTION_MIPI_CAPTURE_HEIGHT
#define APP_STREAM_PITCH PERSON_DETECTION_MIPI_CAPTURE_PITCH
#define APP_STREAM_FRAME_SIZE PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE
#define APP_STREAM_TIMEOUT K_SECONDS(5)
#define APP_STREAM_IDLE_SLEEP_MS 20
#define APP_STREAM_FRAME_INTERVAL_MS 500
#define APP_STREAM_LOG_WINDOW_FRAMES 30U
#define APP_STREAM_JPEG_BUF_COUNT 2U

#define APP_ENC_MEM_NODE DT_PHANDLE(ENC_NODE, memory_region)
#define APP_ENC_MEM_BASE ((uintptr_t)DT_REG_ADDR(APP_ENC_MEM_NODE))
#define APP_ENC_MEM_SIZE ((size_t)DT_REG_SIZE(APP_ENC_MEM_NODE))
#define APP_ENC_SRC_OFFSET ((size_t)DT_PROP_OR(ENC_NODE, frame_raw_offset, 0))
#define APP_ENC_MAX_JPEG_SIZE ((size_t)DT_PROP_OR(ENC_NODE, max_jpeg_size, (128U * 1024U)))

enum person_detection_mipi_stream_state {
	STREAM_STATE_UNINITIALIZED = 0,
	STREAM_STATE_READY,
	STREAM_STATE_RUNNING,
	STREAM_STATE_STOPPED,
};

struct person_detection_mipi_stream_context {
	struct k_mutex lock;
	struct k_sem worker_stopped_sem;
	struct k_thread worker_thread;
	const struct device *enc_dev;
	struct video_caps enc_caps;
	struct video_format jpeg_fmt;
	struct stream_cdc_transport_ctx stream_cdc;
	struct video_buffer jpeg_vbufs[APP_STREAM_JPEG_BUF_COUNT];
	enum person_detection_mipi_stream_state state;
	bool initialized;
	bool stop_requested;
	bool worker_running;
	bool enc_started;
	bool runtime_prepared;
	bool jpeg_buffer_prepared;
	struct video_buffer *jpeg_free_list[APP_STREAM_JPEG_BUF_COUNT];
	size_t jpeg_free_count;
	size_t jpeg_inflight_count;
	uint32_t last_stream_sequence;
	uint32_t next_jpeg_index;
	int64_t last_stream_uptime_ms;
	uint32_t frames_encoded;
	uint32_t frames_streamed;
};

static struct person_detection_mipi_stream_context app_ctx;
static uint8_t app_stream_tx_jpeg_mem[STREAM_CDC_JPEG_PAYLOAD_SIZE] __aligned(64);

K_THREAD_STACK_DEFINE(person_detection_mipi_stream_stack, 4096);

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
static void mem_flush(uintptr_t addr, size_t size)
{
	if (size == 0U) {
		return;
	}

	(void)sys_cache_data_flush_range((void *)addr, size);
	barrier_dsync_fence_full();
}

static void mem_flush_invalidate(uintptr_t addr, size_t size)
{
	if (size == 0U) {
		return;
	}

	(void)sys_cache_data_flush_and_invd_range((void *)addr, size);
	barrier_dsync_fence_full();
}
#else
static void mem_flush(uintptr_t addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
}

static void mem_flush_invalidate(uintptr_t addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
}
#endif

static void stream_context_init(struct person_detection_mipi_stream_context *ctx)
{
	if (ctx->initialized) {
		return;
	}

	k_mutex_init(&ctx->lock);
	k_sem_init(&ctx->worker_stopped_sem, 0, 1);
	ctx->initialized = true;
}

static void drain_buffers(const struct device *dev)
{
	struct video_buffer *vbuf = NULL;

	while ((video_dequeue(dev, &vbuf, K_NO_WAIT) == 0) && (vbuf != NULL)) {
	}
}

static int prepare_encoder_locked(struct person_detection_mipi_stream_context *ctx)
{
	ctx->enc_dev = DEVICE_DT_GET(ENC_NODE);
	if (!device_is_ready(ctx->enc_dev)) {
		return -ENODEV;
	}

	memset(&ctx->enc_caps, 0, sizeof(ctx->enc_caps));
	ctx->enc_caps.type = VIDEO_BUF_TYPE_OUTPUT;
	return video_get_caps(ctx->enc_dev, &ctx->enc_caps);
}

static int configure_encoder_locked(struct person_detection_mipi_stream_context *ctx)
{
	ctx->jpeg_fmt = (struct video_format) {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width = APP_STREAM_WIDTH,
		.height = APP_STREAM_HEIGHT,
		.pitch = APP_STREAM_PITCH,
	};

	return video_set_format(ctx->enc_dev, &ctx->jpeg_fmt);
}

static int prepare_jpeg_buffers_locked(struct person_detection_mipi_stream_context *ctx)
{
	size_t align = 64U;
	size_t raw_input_size;
	size_t jpeg_region_offset;
	uint16_t imported_idx;
	int ret;

	if (ctx->jpeg_buffer_prepared) {
		return 0;
	}

	if (ctx->enc_caps.buf_align != 0U) {
		align = ctx->enc_caps.buf_align;
	}

	if (ctx->enc_caps.min_vbuf_count > APP_STREAM_JPEG_BUF_COUNT) {
		return -ENOMEM;
	}

	if (align > 64U) {
		return -EINVAL;
	}

	if ((ctx->jpeg_fmt.size == 0U) || (ctx->jpeg_fmt.size > APP_ENC_MAX_JPEG_SIZE)) {
		return -EINVAL;
	}

	raw_input_size = (size_t)APP_STREAM_WIDTH * (size_t)APP_STREAM_HEIGHT;
	jpeg_region_offset = APP_ENC_SRC_OFFSET + raw_input_size;

	if ((APP_ENC_MEM_BASE == 0U) ||
	    (APP_ENC_SRC_OFFSET > APP_ENC_MEM_SIZE) ||
	    (raw_input_size > (APP_ENC_MEM_SIZE - APP_ENC_SRC_OFFSET)) ||
	    ((size_t)APP_STREAM_JPEG_BUF_COUNT * APP_ENC_MAX_JPEG_SIZE >
	     (APP_ENC_MEM_SIZE - jpeg_region_offset))) {
		return -ENOMEM;
	}

	for (size_t i = 0U; i < APP_STREAM_JPEG_BUF_COUNT; i++) {
		struct video_buffer *vbuf = &ctx->jpeg_vbufs[i];

		memset(vbuf, 0, sizeof(*vbuf));
		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
		vbuf->buffer = (void *)(APP_ENC_MEM_BASE + jpeg_region_offset +
				     (i * (size_t)APP_ENC_MAX_JPEG_SIZE));
		vbuf->size = APP_ENC_MAX_JPEG_SIZE;
		ret = video_import_buffer(vbuf->buffer, vbuf->size, &imported_idx);
		if (ret != 0) {
			printk("person_detection_mipi: stream worker: JPEG import[%u] failed: %d\n",
			       (uint32_t)i, ret);
			return ret;
		}
		vbuf->index = imported_idx;
		mem_flush_invalidate((uintptr_t)vbuf->buffer, vbuf->size);
	}

	ctx->jpeg_buffer_prepared = true;

	return 0;
}

static int prepare_jpeg_buffers_if_needed(struct person_detection_mipi_stream_context *ctx)
{
	int ret;

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ret = prepare_jpeg_buffers_locked(ctx);
	k_mutex_unlock(&ctx->lock);

	if (ret != 0) {
		printk("person_detection_mipi: stream worker: JPEG buffers prepare failed: %d\n",
		       ret);
	}

	return ret;
}

static int prepare_stream_runtime(struct person_detection_mipi_stream_context *ctx)
{
	int ret;

	printk("person_detection_mipi: stream worker: preparing encoder\n");
	ret = prepare_encoder_locked(ctx);
	if (ret != 0) {
		printk("person_detection_mipi: stream worker: encoder prepare failed: %d\n", ret);
		return ret;
	}

	printk("person_detection_mipi: stream worker: configuring encoder\n");
	ret = configure_encoder_locked(ctx);
	if (ret != 0) {
		printk("person_detection_mipi: stream worker: encoder configure failed: %d\n", ret);
		return ret;
	}

	printk("person_detection_mipi: stream worker: initializing CDC ACM 1\n");
	ret = stream_cdc_transport_init(&ctx->stream_cdc);
	if (ret != 0) {
		printk("person_detection_mipi: stream worker: CDC ACM 1 init failed: %d\n", ret);
		return ret;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->runtime_prepared = true;
	k_mutex_unlock(&ctx->lock);

	printk("person_detection_mipi: stream worker: ready (open CDC ACM 1 viewer for live JPEG)\n");
	return 0;
}

static size_t jpeg_find_eoi_len(const uint8_t *buf, size_t len)
{
	if ((buf == NULL) || (len < 2U)) {
		return 0U;
	}

	for (size_t i = len - 2U; i > 0U; i--) {
		if ((buf[i] == 0xFFU) && (buf[i + 1U] == 0xD9U)) {
			return i + 2U;
		}
	}

	if ((buf[0] == 0xFFU) && (buf[1U] == 0xD9U)) {
		return 2U;
	}

	return 0U;
}

static int send_jpeg_frame(struct person_detection_mipi_stream_context *ctx,
			   const struct video_buffer *jpeg_out)
{
	size_t frame_len;
	int ret;

	frame_len = jpeg_find_eoi_len((const uint8_t *)jpeg_out->buffer, jpeg_out->bytesused);
	if ((frame_len == 0U) || (frame_len > jpeg_out->bytesused)) {
		printk("person_detection_mipi: stream worker: JPEG EOI not found (bytesused=%u)\n",
		       jpeg_out->bytesused);
		return -EIO;
	}

	if (frame_len > sizeof(app_stream_tx_jpeg_mem)) {
		printk("person_detection_mipi: stream worker: JPEG too large len=%u max=%u\n",
		       (uint32_t)frame_len, (uint32_t)sizeof(app_stream_tx_jpeg_mem));
		return -ENOMEM;
	}

	memcpy(app_stream_tx_jpeg_mem, jpeg_out->buffer, frame_len);
	memset(app_stream_tx_jpeg_mem + frame_len, 0, sizeof(app_stream_tx_jpeg_mem) - frame_len);
	mem_flush((uintptr_t)app_stream_tx_jpeg_mem, sizeof(app_stream_tx_jpeg_mem));

	ret = stream_cdc_transport_send_jpeg(&ctx->stream_cdc, app_stream_tx_jpeg_mem,
					     sizeof(app_stream_tx_jpeg_mem));
	if (ret != 0) {
		return ret;
	}

	ctx->frames_streamed++;
	if ((ctx->frames_streamed <= 5U) ||
	    ((ctx->frames_streamed % APP_STREAM_LOG_WINDOW_FRAMES) == 0U)) {
		printk("person_detection_mipi: stream worker: frame %u streamed jpeg_bytes=%u\n",
		       ctx->frames_streamed, (uint32_t)frame_len);
	}

	((struct video_buffer *)jpeg_out)->bytesused = 0U;
	mem_flush_invalidate((uintptr_t)jpeg_out->buffer, jpeg_out->size);
	return 0;
}

static int start_encoder_frame(struct person_detection_mipi_stream_context *ctx)
{
	int ret;

	if (ctx->enc_started) {
		return 0;
	}

	ret = video_stream_start(ctx->enc_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret != 0) {
		printk("person_detection_mipi: stream worker: encoder start failed: %d\n", ret);
		return ret;
	}

	ctx->enc_started = true;
	return 0;
}

static void stop_encoder_frame(struct person_detection_mipi_stream_context *ctx)
{
	if (!ctx->enc_started) {
		return;
	}

	(void)video_stream_stop(ctx->enc_dev, VIDEO_BUF_TYPE_OUTPUT);
	(void)video_flush(ctx->enc_dev, true);
	drain_buffers(ctx->enc_dev);
	ctx->enc_started = false;
}

static int capture_encoded_frame(struct person_detection_mipi_stream_context *ctx,
				 struct video_buffer **jpeg_out)
{
	struct video_buffer *out = NULL;
	int ret;

	if (jpeg_out == NULL) {
		return -EINVAL;
	}

	*jpeg_out = NULL;
	ret = video_dequeue(ctx->enc_dev, &out, APP_STREAM_TIMEOUT);
	if ((ret != 0) || (out == NULL)) {
		return (ret != 0) ? ret : -EIO;
	}

	if (out->bytesused == 0U) {
		return -EIO;
	}

	mem_flush_invalidate((uintptr_t)out->buffer, out->bytesused);
	*jpeg_out = out;
	return 0;
}

static int clamp_int(int value, int min_value, int max_value)
{
	if (value < min_value) {
		return min_value;
	}
	if (value > max_value) {
		return max_value;
	}
	return value;
}

static void draw_hline(uint8_t *frame, int x0, int x1, int y, uint8_t value)
{
	if ((frame == NULL) || (y < 0) || (y >= (int)APP_STREAM_HEIGHT)) {
		return;
	}

	x0 = clamp_int(x0, 0, (int)APP_STREAM_WIDTH - 1);
	x1 = clamp_int(x1, 0, (int)APP_STREAM_WIDTH - 1);
	if (x1 < x0) {
		return;
	}

	memset(frame + ((size_t)y * APP_STREAM_PITCH) + (size_t)x0,
	       value, (size_t)(x1 - x0 + 1));
}

static void draw_vline(uint8_t *frame, int x, int y0, int y1, uint8_t value)
{
	if ((frame == NULL) || (x < 0) || (x >= (int)APP_STREAM_WIDTH)) {
		return;
	}

	y0 = clamp_int(y0, 0, (int)APP_STREAM_HEIGHT - 1);
	y1 = clamp_int(y1, 0, (int)APP_STREAM_HEIGHT - 1);
	if (y1 < y0) {
		return;
	}

	for (int y = y0; y <= y1; y++) {
		frame[((size_t)y * APP_STREAM_PITCH) + (size_t)x] = value;
	}
}

static void overlay_detections(uint8_t *frame)
{
	struct person_detection_mipi_metadata_snapshot snapshot;
	const int thickness = 2;
	int ret = person_detection_mipi_app_get_latest_snapshot(&snapshot);

	if ((ret != 0) || !snapshot.valid || (snapshot.detection_count <= 0)) {
		return;
	}

	for (int i = 0; i < snapshot.detection_count; i++) {
		const detection_t *det = &snapshot.detections[i];
		int x0 = (int)(det->x + 0.5f);
		int y0 = (int)(det->y + 0.5f);
		int x1 = (int)(det->x + det->w + 0.5f) - 1;
		int y1 = (int)(det->y + det->h + 0.5f) - 1;

		if ((x1 < 0) || (y1 < 0) ||
		    (x0 >= (int)APP_STREAM_WIDTH) || (y0 >= (int)APP_STREAM_HEIGHT)) {
			continue;
		}

		for (int t = 0; t < thickness; t++) {
			draw_hline(frame, x0, x1, y0 + t, 0xFFU);
			draw_hline(frame, x0, x1, y1 - t, 0xFFU);
			draw_vline(frame, x0 + t, y0, y1, 0xFFU);
			draw_vline(frame, x1 - t, y0, y1, 0xFFU);
		}
	}
}

int person_detection_mipi_stream_init(void)
{
	struct person_detection_mipi_stream_context *ctx = &app_ctx;

	stream_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);

	if (ctx->state != STREAM_STATE_UNINITIALIZED) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	ctx->state = STREAM_STATE_READY;
	k_mutex_unlock(&ctx->lock);
	printk("person_detection_mipi: stream: deferred init ready\n");
	return 0;
}

int person_detection_mipi_stream_start(void)
{
	struct person_detection_mipi_stream_context *ctx = &app_ctx;
	int ret;

	stream_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);

	if ((ctx->state != STREAM_STATE_READY) && (ctx->state != STREAM_STATE_STOPPED)) {
		k_mutex_unlock(&ctx->lock);
		return -EPERM;
	}

	ctx->stop_requested = false;
	ctx->worker_running = false;
	ctx->enc_started = false;
	ctx->frames_encoded = 0U;
	ctx->frames_streamed = 0U;
	ctx->jpeg_free_count = 0U;
	ctx->jpeg_inflight_count = 0U;
	ctx->last_stream_sequence = 0U;
	ctx->next_jpeg_index = 0U;
	ctx->last_stream_uptime_ms = 0;
	ctx->state = STREAM_STATE_STOPPED;
	k_mutex_unlock(&ctx->lock);

	ret = prepare_stream_runtime(ctx);
	if (ret != 0) {
		return ret;
	}

	stream_cdc_transport_begin_stream(&ctx->stream_cdc);

	ret = prepare_jpeg_buffers_if_needed(ctx);
	if (ret != 0) {
		return ret;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	for (size_t i = 0U; i < APP_STREAM_JPEG_BUF_COUNT; i++) {
		ctx->jpeg_free_list[ctx->jpeg_free_count++] = &ctx->jpeg_vbufs[i];
	}
	k_mutex_unlock(&ctx->lock);

	ret = start_encoder_frame(ctx);
	if (ret != 0) {
		k_mutex_lock(&ctx->lock, K_FOREVER);
		ctx->jpeg_free_count = 0U;
		k_mutex_unlock(&ctx->lock);
		return ret;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->state = STREAM_STATE_RUNNING;
	k_mutex_unlock(&ctx->lock);

	return 0;
}

int person_detection_mipi_stream_stop(void)
{
	struct person_detection_mipi_stream_context *ctx = &app_ctx;

	stream_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->stop_requested = true;
	ctx->worker_running = false;
	ctx->jpeg_free_count = 0U;
	ctx->jpeg_inflight_count = 0U;
	ctx->state = STREAM_STATE_STOPPED;
	ctx->runtime_prepared = false;
	k_mutex_unlock(&ctx->lock);

	stream_cdc_transport_request_stop(&ctx->stream_cdc);
	stop_encoder_frame(ctx);
	return 0;
}

int person_detection_mipi_stream_pause(void)
{
	struct person_detection_mipi_stream_context *ctx = &app_ctx;

	stream_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (ctx->state != STREAM_STATE_RUNNING) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	ctx->state = STREAM_STATE_STOPPED;
	k_mutex_unlock(&ctx->lock);

	stream_cdc_transport_request_stop(&ctx->stream_cdc);
	return 0;
}

int person_detection_mipi_stream_resume(void)
{
	struct person_detection_mipi_stream_context *ctx = &app_ctx;

	stream_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (ctx->state != STREAM_STATE_STOPPED) {
		k_mutex_unlock(&ctx->lock);
		return -EPERM;
	}

	if (!ctx->enc_started || !ctx->runtime_prepared) {
		k_mutex_unlock(&ctx->lock);
		return person_detection_mipi_stream_start();
	}

	stream_cdc_transport_begin_stream(&ctx->stream_cdc);
	ctx->state = STREAM_STATE_RUNNING;
	k_mutex_unlock(&ctx->lock);
	return 0;
}

int person_detection_mipi_stream_process_frame(const uint8_t *frame_data,
	size_t frame_size, uint32_t sequence)
{
	struct person_detection_mipi_stream_context *ctx = &app_ctx;
	struct video_buffer *jpeg_in = NULL;
	struct video_buffer *jpeg_out = NULL;
	int64_t now_ms;
	int ret;

	if ((frame_data == NULL) || (frame_size < APP_STREAM_FRAME_SIZE) || (sequence == 0U)) {
		return -EINVAL;
	}

	stream_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (ctx->state != STREAM_STATE_RUNNING) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	now_ms = k_uptime_get();
	if ((sequence == ctx->last_stream_sequence) ||
	    ((ctx->last_stream_uptime_ms != 0) &&
	     ((now_ms - ctx->last_stream_uptime_ms) < APP_STREAM_FRAME_INTERVAL_MS))) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}
	k_mutex_unlock(&ctx->lock);

	if (!stream_cdc_transport_is_connected(&ctx->stream_cdc)) {
		return 0;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	if ((ctx->jpeg_free_count == 0U) && (ctx->jpeg_inflight_count > 0U)) {
		k_mutex_unlock(&ctx->lock);
		ret = capture_encoded_frame(ctx, &jpeg_out);
		if (ret != 0) {
			return ret;
		}

		ctx->frames_encoded++;
		if ((ctx->frames_encoded <= 5U) ||
		    ((ctx->frames_encoded % APP_STREAM_LOG_WINDOW_FRAMES) == 0U)) {
			printk("person_detection_mipi: stream: frame %u encoded bytes=%u seq=%u\n",
			       ctx->frames_encoded, jpeg_out->bytesused, sequence);
		}

		ret = send_jpeg_frame(ctx, jpeg_out);
		k_mutex_lock(&ctx->lock, K_FOREVER);
		if (ctx->jpeg_inflight_count > 0U) {
			ctx->jpeg_inflight_count--;
		}
		ctx->jpeg_free_list[ctx->jpeg_free_count++] = jpeg_out;
		k_mutex_unlock(&ctx->lock);

		if ((ret != 0) && (ret != -ENOTCONN)) {
			return ret;
		}

		k_mutex_lock(&ctx->lock, K_FOREVER);
	}

	if (ctx->jpeg_free_count == 0U) {
		k_mutex_unlock(&ctx->lock);
		return -EAGAIN;
	}

	jpeg_in = ctx->jpeg_free_list[--ctx->jpeg_free_count];
	k_mutex_unlock(&ctx->lock);

	memcpy((void *)(APP_ENC_MEM_BASE + APP_ENC_SRC_OFFSET), frame_data, APP_STREAM_FRAME_SIZE);
	overlay_detections((uint8_t *)(APP_ENC_MEM_BASE + APP_ENC_SRC_OFFSET));
	mem_flush((uintptr_t)(APP_ENC_MEM_BASE + APP_ENC_SRC_OFFSET), APP_STREAM_FRAME_SIZE);

	jpeg_in->bytesused = 0U;
	mem_flush_invalidate((uintptr_t)jpeg_in->buffer, jpeg_in->size);

	ret = video_enqueue(ctx->enc_dev, jpeg_in);
	if (ret != 0) {
		k_mutex_lock(&ctx->lock, K_FOREVER);
		ctx->jpeg_free_list[ctx->jpeg_free_count++] = jpeg_in;
		k_mutex_unlock(&ctx->lock);
		return ret;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->jpeg_inflight_count++;
	if (ctx->jpeg_inflight_count < APP_STREAM_JPEG_BUF_COUNT) {
		ctx->last_stream_sequence = sequence;
		ctx->last_stream_uptime_ms = now_ms;
		k_mutex_unlock(&ctx->lock);
		return 0;
	}
	k_mutex_unlock(&ctx->lock);

	ret = capture_encoded_frame(ctx, &jpeg_out);
	if (ret != 0) {
		return ret;
	}

	ctx->frames_encoded++;
	if ((ctx->frames_encoded <= 5U) ||
	    ((ctx->frames_encoded % APP_STREAM_LOG_WINDOW_FRAMES) == 0U)) {
		printk("person_detection_mipi: stream: frame %u encoded bytes=%u seq=%u\n",
		       ctx->frames_encoded, jpeg_out->bytesused, sequence);
	}

	ret = send_jpeg_frame(ctx, jpeg_out);
	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (ctx->jpeg_inflight_count > 0U) {
		ctx->jpeg_inflight_count--;
	}
	ctx->jpeg_free_list[ctx->jpeg_free_count++] = jpeg_out;
	ctx->last_stream_sequence = sequence;
	ctx->last_stream_uptime_ms = now_ms;
	k_mutex_unlock(&ctx->lock);

	if ((ret != 0) && (ret != -ENOTCONN)) {
		return ret;
	}

	return 0;
}
