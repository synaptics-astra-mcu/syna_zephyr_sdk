/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/port-endpoint.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(video_sample_app, CONFIG_LOG_DEFAULT_LEVEL);

#define VIDEO_SAMPLE_FRAME_WIDTH      ((uint32_t)CONFIG_VIDEO_SAMPLE_FRAME_WIDTH)
#define VIDEO_SAMPLE_FRAME_HEIGHT     ((uint32_t)CONFIG_VIDEO_SAMPLE_FRAME_HEIGHT)
#define VIDEO_SAMPLE_FRAME_PITCH      ((uint32_t)CONFIG_VIDEO_SAMPLE_FRAME_PITCH)
#define VIDEO_SAMPLE_BUFFER_COUNT     ((uint32_t)CONFIG_VIDEO_SAMPLE_BUFFER_COUNT)
#define VIDEO_SAMPLE_CAPTURE_TIMEOUT  K_MSEC(CONFIG_VIDEO_SAMPLE_CAPTURE_TIMEOUT_MS)
#define VIDEO_SAMPLE_WARMUP_DELAY_MS  ((uint32_t)CONFIG_VIDEO_SAMPLE_WARMUP_DELAY_MS)
#define VIDEO_SAMPLE_TRIGGER_RETRIES  ((uint32_t)CONFIG_VIDEO_SAMPLE_TRIGGER_RETRIES)
#define VIDEO_SAMPLE_TRIGGER_RETRY_MS ((uint32_t)CONFIG_VIDEO_SAMPLE_TRIGGER_RETRY_MS)
#define VIDEO_SAMPLE_CAPTURE_COUNT    ((uint32_t)CONFIG_VIDEO_SAMPLE_CAPTURE_COUNT)

#define VIDEO_SAMPLE_PIXEL_FORMAT     VIDEO_PIX_FMT_SBGGR8

#define VIDEO_SAMPLE_SENSOR_PIXEL_FORMAT VIDEO_PIX_FMT_SBGGR10P

#define VIDEO_SAMPLE_CAPTURE_PIXEL_FORMAT VIDEO_SAMPLE_PIXEL_FORMAT

#define VIDEO_DEV_NODE DT_NODELABEL(video_syna0)

#if !DT_NODE_EXISTS(VIDEO_DEV_NODE)
#error "Devicetree node 'video_syna0' is required for this sample"
#endif

#define VIDEO_SAMPLE_ENDPOINT DT_CHILD(DT_CHILD(VIDEO_DEV_NODE, port), endpoint)
#define VIDEO_SAMPLE_SENSOR_NODE DT_NODE_REMOTE_DEVICE(VIDEO_SAMPLE_ENDPOINT)

BUILD_ASSERT(DT_NODE_HAS_STATUS(VIDEO_SAMPLE_SENSOR_NODE, okay),
		"Enable the sensor connected to video_syna0 before building this sample");

#if DT_NODE_HAS_PROP(VIDEO_DEV_NODE, memory_region)
#define VIDEO_SAMPLE_SHM_ADDR ((uintptr_t)DT_REG_ADDR(DT_PHANDLE(VIDEO_DEV_NODE, memory_region)))
#define VIDEO_SAMPLE_SHM_SIZE ((size_t)DT_REG_SIZE(DT_PHANDLE(VIDEO_DEV_NODE, memory_region)))
#elif DT_NODE_HAS_PROP(VIDEO_DEV_NODE, shm_pool_addr) && DT_NODE_HAS_PROP(VIDEO_DEV_NODE, shm_pool_size)
#define VIDEO_SAMPLE_SHM_ADDR ((uintptr_t)DT_PROP(VIDEO_DEV_NODE, shm_pool_addr))
#define VIDEO_SAMPLE_SHM_SIZE ((size_t)DT_PROP(VIDEO_DEV_NODE, shm_pool_size))
#else
#define VIDEO_SAMPLE_SHM_ADDR ((uintptr_t)0U)
#define VIDEO_SAMPLE_SHM_SIZE ((size_t)0U)
#endif

static const struct device *sensor_dev = DEVICE_DT_GET(VIDEO_SAMPLE_SENSOR_NODE);
static uint8_t *stored_frame;
static size_t stored_frame_size;

#if !IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD)
static uint8_t stored_frame_buf[VIDEO_SAMPLE_FRAME_PITCH * VIDEO_SAMPLE_FRAME_HEIGHT];
#endif

extern char _image_ram_start[];
extern char __kernel_ram_end[];

static int sensor_stream_start(void);
static int sensor_stop_control(void);
static int store_captured_frame(const struct video_buffer *vbuf);
static int configure_video_format(const struct device *video_dev, struct video_format *fmt);
static void drain_returned_buffers(const struct device *video_dev, size_t expected_count);
#if !IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD)
static int enqueue_capture_buffers(const struct device *video_dev, const struct video_caps *caps,
				   const struct video_format *fmt,
				   struct video_buffer **buffers, size_t count,
				   size_t *driver_owned_count);
#endif

static int sensor_stream_start(void)
{
	int ret;
	struct video_format sensor_fmt = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_SAMPLE_SENSOR_PIXEL_FORMAT,
		.width = VIDEO_SAMPLE_FRAME_WIDTH,
		.height = VIDEO_SAMPLE_FRAME_HEIGHT,
	};

	if (!device_is_ready(sensor_dev)) {
		LOG_ERR("Sensor device %s is not ready", sensor_dev->name);
		return -ENODEV;
	}

	ret = video_set_format(sensor_dev, &sensor_fmt);
	if (ret != 0) {
		LOG_ERR("Sensor video_set_format failed: %d", ret);
		return ret;
	}

	ret = video_stream_start(sensor_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret != 0) {
		LOG_ERR("Sensor video_stream_start failed: %d", ret);
		return ret;
	}

	LOG_INF("Sensor %s stream enabled for capture", sensor_dev->name);
	return 0;
}

static int sensor_stop_control(void)
{
	int ret;

	if (!device_is_ready(sensor_dev)) {
		LOG_ERR("Sensor device %s is not ready", sensor_dev->name);
		return -ENODEV;
	}

	ret = video_stream_stop(sensor_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret != 0) {
		LOG_ERR("video_stream_stop failed: %d", ret);
		return ret;
	}

	LOG_INF("Sensor %s stream stopped", sensor_dev->name);
	return 0;
}

static int store_captured_frame(const struct video_buffer *vbuf)
{
	if ((vbuf == NULL) || (vbuf->buffer == NULL) || (vbuf->bytesused == 0U)) {
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD)
	/*
	 * For 1080p, keep the captured frame in-place (DT-provided SHM pool).
	 * Copying it into the kernel heap is not practical on SRAM-only builds.
	 * This pointer is for immediate inspection only; after the SHM buffer is
	 * returned to the driver via stop/flush, do not retain or reuse it.
	 */
	stored_frame = vbuf->buffer;
	stored_frame_size = vbuf->bytesused;
	LOG_INF("Stored captured frame: %u bytes at %p",
		(uint32_t)stored_frame_size, stored_frame);
	return 0;
#else

	if (vbuf->bytesused > sizeof(stored_frame_buf)) {
		return -ENOMEM;
	}

	memcpy(stored_frame_buf, vbuf->buffer, vbuf->bytesused);
	stored_frame = stored_frame_buf;
	stored_frame_size = vbuf->bytesused;

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
	sys_cache_data_flush_range(stored_frame_buf, stored_frame_size);
	barrier_dsync_fence_full();
#endif

	LOG_INF("Stored captured frame: %u bytes at %p",
		(uint32_t)stored_frame_size, stored_frame);

	return 0;
#endif
}

static int configure_video_format(const struct device *video_dev, struct video_format *fmt)
{
	int ret;

	*fmt = (struct video_format) {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_SAMPLE_CAPTURE_PIXEL_FORMAT,
		.width = VIDEO_SAMPLE_FRAME_WIDTH,
		.height = VIDEO_SAMPLE_FRAME_HEIGHT,
		.pitch = VIDEO_SAMPLE_FRAME_PITCH,
	};

	ret = video_set_format(video_dev, fmt);
	if (ret < 0) {
		LOG_ERR("video_set_format failed: %d", ret);
		return ret;
	}

	LOG_INF("Video format configured: %ux%u pitch=%u size=%u",
		fmt->width, fmt->height, fmt->pitch, fmt->size);

	return 0;
}

static void drain_returned_buffers(const struct device *video_dev, size_t expected_count)
{
	while (expected_count > 0U) {
		struct video_buffer *vbuf = NULL;
		int ret;

		ret = video_dequeue(video_dev, &vbuf, K_NO_WAIT);
		if ((ret != 0) || (vbuf == NULL)) {
			LOG_WRN("Expected %u more returned buffers, dequeue ret=%d",
				(uint32_t)expected_count, ret);
			break;
		}

		vbuf->bytesused = 0U;
		expected_count--;
	}
}

#if !IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD)
static int enqueue_capture_buffers(const struct device *video_dev, const struct video_caps *caps,
				   const struct video_format *fmt,
				   struct video_buffer **buffers, size_t count,
				   size_t *driver_owned_count)
{
	int ret;
	size_t align = MAX(caps->buf_align, 1U);

	for (size_t i = 0; i < count; i++) {
		buffers[i] = video_buffer_aligned_alloc(fmt->size, align, K_NO_WAIT);
		if (buffers[i] == NULL) {
			LOG_ERR("Buffer alloc failed. Increase CONFIG_VIDEO_BUFFER_POOL_HEAP_SIZE");
			return -ENOMEM;
		}

		buffers[i]->type = VIDEO_BUF_TYPE_OUTPUT;
		buffers[i]->bytesused = 0U;

		ret = video_enqueue(video_dev, buffers[i]);
		if (ret < 0) {
			LOG_ERR("video_enqueue failed: %d", ret);
			return ret;
		}

		(*driver_owned_count)++;
	}

	return 0;
}
#endif

int main(void)
{
	const struct device *video_dev = DEVICE_DT_GET(VIDEO_DEV_NODE);
	struct video_caps caps = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	struct video_format fmt;
	struct video_buffer *queued_buffers[VIDEO_SAMPLE_BUFFER_COUNT] = { 0 };
	struct video_buffer *captured = NULL;
#if IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD)
	struct video_buffer shm_vbuf = { 0 };
#endif
	int ret;
	int final_ret = 0;
	size_t driver_owned_buffer_count = 0U;
	bool sensor_started = false;
	bool video_started = false;
	bool video_stopped = false;

	LOG_INF("Zephyr RAM window: [0x%08x..0x%08x)",
		(uint32_t)(uintptr_t)_image_ram_start,
		(uint32_t)(uintptr_t)__kernel_ram_end);
#if !IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD)
	LOG_INF("Sample frame dump buffer: %u bytes at %p",
		(uint32_t)sizeof(stored_frame_buf), stored_frame_buf);
#endif

	if (!device_is_ready(video_dev)) {
		LOG_ERR("Video device %s is not ready", video_dev->name);
		return -ENODEV;
	}

	ret = video_get_caps(video_dev, &caps);
	if (ret < 0) {
		LOG_ERR("video_get_caps failed: %d", ret);
		final_ret = ret;
		goto out_cleanup_sensor;
	}

	LOG_INF("Video caps: min_vbuf_count=%u align=%u", caps.min_vbuf_count,
		(uint32_t)caps.buf_align);

	if (!IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD) &&
	    (ARRAY_SIZE(queued_buffers) < caps.min_vbuf_count)) {
		LOG_ERR("Sample provides %u buffers but driver requires %u",
			(uint32_t)ARRAY_SIZE(queued_buffers), caps.min_vbuf_count);
		final_ret = -ENOMEM;
		goto out_cleanup_sensor;
	}

	ret = configure_video_format(video_dev, &fmt);
	if (ret < 0) {
		final_ret = ret;
		goto out_cleanup_sensor;
	}

#if IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD)
	uintptr_t shm_addr = VIDEO_SAMPLE_SHM_ADDR;
	size_t shm_size = VIDEO_SAMPLE_SHM_SIZE;
	size_t frame_size = (fmt.size != 0U) ? (size_t)fmt.size :
			    ((size_t)VIDEO_SAMPLE_FRAME_PITCH *
			     (size_t)VIDEO_SAMPLE_FRAME_HEIGHT);

	if ((shm_addr == 0U) || (shm_size < frame_size)) {
		LOG_ERR("DT SHM pool too small for %ux%u: addr=0x%x size=0x%x need=%u",
			VIDEO_SAMPLE_FRAME_WIDTH, VIDEO_SAMPLE_FRAME_HEIGHT,
			(uint32_t)shm_addr, (uint32_t)shm_size, (uint32_t)frame_size);
		final_ret = -ENOMEM;
		goto out_cleanup_sensor;
	}

	if ((shm_addr % MAX(caps.buf_align, 1U)) != 0U) {
		LOG_ERR("Capture buffer addr 0x%x is not aligned to %u",
			(uint32_t)shm_addr, (uint32_t)MAX(caps.buf_align, 1U));
		final_ret = -EINVAL;
		goto out_cleanup_sensor;
	}

	shm_vbuf.type = VIDEO_BUF_TYPE_OUTPUT;
	shm_vbuf.buffer = (uint8_t *)shm_addr;
	shm_vbuf.size = (uint32_t)frame_size;
	shm_vbuf.bytesused = 0U;

	ret = video_enqueue(video_dev, &shm_vbuf);
	if (ret < 0) {
		LOG_ERR("video_enqueue(SHM) failed: %d", ret);
		final_ret = ret;
		goto out_cleanup_buffers;
	}
	driver_owned_buffer_count++;
#else
	ret = enqueue_capture_buffers(video_dev, &caps, &fmt, queued_buffers,
					ARRAY_SIZE(queued_buffers),
					&driver_owned_buffer_count);
	if (ret < 0) {
		final_ret = ret;
		goto out_cleanup_buffers;
	}
#endif

	ret = sensor_stream_start();
	if (ret < 0) {
		LOG_ERR("Sensor stream start failed: %d", ret);
		final_ret = ret;
		goto out_cleanup_buffers;
	}
	sensor_started = true;

	ret = video_stream_start(video_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		LOG_ERR("video_stream_start failed: %d", ret);
		final_ret = ret;
		goto out_cleanup_buffers;
	}
	video_started = true;

	k_msleep(VIDEO_SAMPLE_WARMUP_DELAY_MS);

	for (uint32_t frame_idx = 0U; frame_idx < VIDEO_SAMPLE_CAPTURE_COUNT; frame_idx++) {
		bool capture_kicked = false;

		for (size_t attempt = 0; attempt < VIDEO_SAMPLE_TRIGGER_RETRIES; attempt++) {
			ret = video_flush(video_dev, false);
			if (ret == 0) {
				capture_kicked = true;
				break;
			}

			LOG_WRN("Frame %u kick attempt %u/%u failed: %d",
				(uint32_t)(frame_idx + 1U),
				(uint32_t)(attempt + 1U),
				(uint32_t)VIDEO_SAMPLE_TRIGGER_RETRIES,
				ret);
			k_msleep(VIDEO_SAMPLE_TRIGGER_RETRY_MS);
		}

		if (!capture_kicked) {
			LOG_ERR("Unable to trigger frame %u after %u attempts",
				(uint32_t)(frame_idx + 1U),
				(uint32_t)VIDEO_SAMPLE_TRIGGER_RETRIES);
			final_ret = (ret < 0) ? ret : -EIO;
			goto out_stop_video;
		}

		LOG_INF("Waiting for frame %u/%u...", (uint32_t)(frame_idx + 1U),
			(uint32_t)VIDEO_SAMPLE_CAPTURE_COUNT);

		ret = video_dequeue(video_dev, &captured, VIDEO_SAMPLE_CAPTURE_TIMEOUT);
		if (ret < 0) {
			LOG_ERR("Capture timed out or failed (frame %u): %d",
				(uint32_t)(frame_idx + 1U), ret);
			final_ret = ret;
			goto out_stop_video;
		}

		if (driver_owned_buffer_count > 0U) {
			driver_owned_buffer_count--;
		}

		LOG_DBG("Captured frame %u at buffer %p", (uint32_t)(frame_idx + 1U),
			captured->buffer);
		if (!IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD)) {
			LOG_DBG("First 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
				captured->buffer[0], captured->buffer[1],
				captured->buffer[2], captured->buffer[3],
				captured->buffer[4], captured->buffer[5],
				captured->buffer[6], captured->buffer[7]);
		}

		LOG_INF("Frame %u captured: bytesused=%u timestamp=%u buffer=%p",
			(uint32_t)(frame_idx + 1U), captured->bytesused, captured->timestamp,
			captured->buffer);

		ret = store_captured_frame(captured);
		if (ret < 0) {
			LOG_ERR("Failed to store captured frame %u: %d",
				(uint32_t)(frame_idx + 1U), ret);
			final_ret = ret;
			goto out_stop_video;
		}

		captured->bytesused = 0U;
		ret = video_enqueue(video_dev, captured);
		if (ret < 0) {
			LOG_ERR("video_enqueue failed after frame %u: %d",
				(uint32_t)(frame_idx + 1U), ret);
			final_ret = ret;
			goto out_stop_video;
		}
		driver_owned_buffer_count++;
	}

out_stop_video:
	if (video_started) {
		ret = video_stream_stop(video_dev, VIDEO_BUF_TYPE_OUTPUT);
		if (ret < 0) {
			LOG_WRN("video_stream_stop failed: %d", ret);
			if (final_ret == 0) {
				final_ret = ret;
			}
		} else {
			video_stopped = true;
		}
		video_started = false;
	}

	if (video_stopped && (driver_owned_buffer_count > 0U)) {
		drain_returned_buffers(video_dev, driver_owned_buffer_count);
		driver_owned_buffer_count = 0U;
	}

out_cleanup_buffers:
	if (driver_owned_buffer_count > 0U) {
		ret = video_flush(video_dev, true);
		if (ret < 0) {
			LOG_WRN("video_flush(cancel=true) failed during buffer reclaim: %d", ret);
		} else {
			drain_returned_buffers(video_dev, driver_owned_buffer_count);
			driver_owned_buffer_count = 0U;
		}
	}

	if (!IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD)) {
		for (size_t i = 0; i < ARRAY_SIZE(queued_buffers); i++) {
			if (queued_buffers[i] != NULL) {
				video_buffer_release(queued_buffers[i]);
				queued_buffers[i] = NULL;
			}
		}
	}

out_cleanup_sensor:
	if (sensor_started) {
		ret = sensor_stop_control();
		if (ret < 0) {
			LOG_WRN("sensor_stop_control failed: %d", ret);
			if (final_ret == 0) {
				final_ret = ret;
			}
		}
		sensor_started = false;
	}

	LOG_INF("Video sample finished");
	return final_ret;
}
