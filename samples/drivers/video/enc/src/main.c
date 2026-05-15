/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(enc_video_sample, LOG_LEVEL_INF);

#define ENC_NODE DT_NODELABEL(lp_jpeg0)

#if !DT_NODE_EXISTS(ENC_NODE)
#error "No lp_jpeg0 devicetree node found"
#endif

#if !DT_NODE_HAS_COMPAT(ENC_NODE, syna_enc_video)
#error "lp_jpeg0 must use compatible = \"syna,enc-video\""
#endif

#define APP_MODE_SENSOR_TO_MEMORY 0U
#define APP_MODE_MEMORY_INPUT 1U
#define APP_PIPELINE_MODE DT_PROP_OR(ENC_NODE, mode, APP_MODE_SENSOR_TO_MEMORY)

#define APP_NUM_BUFS 2U
#define APP_JPEG_BUF_ALIGN 64U
#define APP_MAX_JPEG_BYTES (128U * 1024U)
#define APP_DEQUEUE_TIMEOUT K_SECONDS(5)

#define APP_LIVE_WIDTH 480U
#define APP_LIVE_HEIGHT 270U

#define APP_MEMORY_INPUT_WIDTH 960U
#define APP_MEMORY_INPUT_HEIGHT 540U
#define APP_MEMORY_INPUT_RAW_SIZE ((size_t)APP_MEMORY_INPUT_WIDTH * (size_t)APP_MEMORY_INPUT_HEIGHT)

#define APP_MEM_REGION_PRESENT DT_NODE_HAS_PROP(ENC_NODE, memory_region)
BUILD_ASSERT(APP_MEM_REGION_PRESENT,
	     "JPEG encoder sample requires a memory-region devicetree phandle");
#if APP_MEM_REGION_PRESENT
#define APP_MEM_NODE DT_PHANDLE(ENC_NODE, memory_region)
#define APP_MEM_BASE ((uintptr_t)DT_REG_ADDR(APP_MEM_NODE))
#define APP_MEM_SIZE ((size_t)DT_REG_SIZE(APP_MEM_NODE))
#else
#define APP_MEM_BASE ((uintptr_t)0U)
#define APP_MEM_SIZE 0U
#endif

#define APP_RAW_OFFSET ((size_t)DT_PROP_OR(ENC_NODE, frame_raw_offset, 0))
#define APP_SRC_OFFSET ((size_t)DT_PROP_OR(ENC_NODE, lp_src_offset, 0))
#define APP_DST_OFFSET ((size_t)DT_PROP_OR(ENC_NODE, lp_dst_offset, 0))

static uint8_t app_jpeg_buf_mem[APP_NUM_BUFS][APP_MAX_JPEG_BYTES] __aligned(APP_JPEG_BUF_ALIGN);
static struct video_buffer app_jpeg_vbufs[APP_NUM_BUFS];

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
static void enc_mem_flush(uintptr_t addr, size_t size)
{
	int ret;

	if (size == 0U) {
		return;
	}

	ret = sys_cache_data_flush_range((void *)addr, size);
	if (ret != 0) {
		LOG_WRN("Encoder memory cache flush failed ret=%d addr=%p size=%u",
			ret, (void *)addr, (unsigned int)size);
	}
	barrier_dsync_fence_full();
}
#else
static void enc_mem_flush(uintptr_t addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
}
#endif

/* Return a human-readable name for the selected run mode. */
static const char *mode_name(uint32_t mode)
{
	if (mode == APP_MODE_MEMORY_INPUT) {
		return "enc-memory-input";
	}

	return "live-to-memory";
}

/* Drain any returned buffers after stopping the stream. */
static void drain_buffers(const struct device *dev)
{
	struct video_buffer *vbuf = NULL;

	while ((video_dequeue(dev, &vbuf, K_NO_WAIT) == 0) && (vbuf != NULL)) {
		vbuf->bytesused = 0U;
	}
}

/* Video helpers */

/*
 * Configure the JPEG output format.
 *
 * The negotiated `jpeg_fmt->size` is the required output-buffer capacity.
 * The actual JPEG size is returned later in `video_buffer.bytesused`.
 */
static int enc_configure(const struct device *enc,
			 struct video_format *jpeg_fmt,
			 uint32_t width,
			 uint32_t height)
{
	int ret;

	*jpeg_fmt = (struct video_format) {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width = width,
		.height = height,
	};

	ret = video_set_format(enc, jpeg_fmt);
	LOG_INF("enc video_set_format ret=%d capacity=%u pitch=%u (%ux%u)",
		ret, jpeg_fmt->size, jpeg_fmt->pitch,
		jpeg_fmt->width, jpeg_fmt->height);
	return ret;
}

/* Allocate and enqueue destination buffers for JPEG output. */
static int jpeg_buffers_queue(const struct device *enc,
			      const struct video_format *jpeg_fmt)
{
	struct video_caps caps = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	size_t align = APP_JPEG_BUF_ALIGN;
	int ret;

	if ((jpeg_fmt == NULL) || (jpeg_fmt->size > APP_MAX_JPEG_BYTES)) {
		LOG_ERR("JPEG buffer pool too small: required capacity=%u max=%u",
			jpeg_fmt != NULL ? jpeg_fmt->size : 0U,
			(unsigned int)APP_MAX_JPEG_BYTES);
		return -EINVAL;
	}

	ret = video_get_caps(enc, &caps);
	LOG_INF("enc video_get_caps ret=%d min_vbuf=%u align=%zu",
		ret, caps.min_vbuf_count, caps.buf_align);
	if (ret != 0) {
		return ret;
	}

	if (APP_NUM_BUFS < caps.min_vbuf_count) {
		LOG_ERR("APP_NUM_BUFS too small: available=%u required(min_vbuf_count)=%u",
			(unsigned int)APP_NUM_BUFS, caps.min_vbuf_count);
		return -EINVAL;
	}

	if (caps.buf_align != 0U) {
		align = caps.buf_align;
	}

	if (align > APP_JPEG_BUF_ALIGN) {
		LOG_ERR("JPEG buffer alignment too small: required=%zu available=%u",
			align, (unsigned int)APP_JPEG_BUF_ALIGN);
		return -EINVAL;
	}

	for (size_t i = 0; i < APP_NUM_BUFS; i++) {
		struct video_buffer *vbuf = &app_jpeg_vbufs[i];

		memset(vbuf, 0, sizeof(*vbuf));
		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
		vbuf->buffer = app_jpeg_buf_mem[i];
		vbuf->size = APP_MAX_JPEG_BYTES;

		LOG_INF("enc app_buf[%u] addr=%p size=%u align=%zu",
			(unsigned int)i, vbuf->buffer, (unsigned int)vbuf->size, align);

		ret = video_enqueue(enc, vbuf);
		LOG_INF("enc video_enqueue[%u] ret=%d", (unsigned int)i, ret);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

/* Static reserved-memory input frame generation */

static uint8_t bayer_from_rgb(uint16_t x, uint16_t y, uint8_t red,
			      uint8_t green, uint8_t blue)
{
	if (((y & 1U) == 0U) && ((x & 1U) == 0U)) {
		return blue;
	}

	if (((y & 1U) == 1U) && ((x & 1U) == 1U)) {
		return red;
	}

	return green;
}

static void fill_bayer_pattern(uint8_t *dst, uint16_t width, uint16_t height)
{
	static const uint8_t color_bars[][3] = {
		{ 0x00, 0x00, 0x00 }, /* black */
		{ 0x00, 0x00, 0xff }, /* blue */
		{ 0x00, 0xff, 0xff }, /* cyan */
		{ 0x00, 0xff, 0x00 }, /* green */
		{ 0xff, 0x00, 0xff }, /* magenta */
		{ 0xff, 0x00, 0x00 }, /* red */
		{ 0xff, 0xff, 0x00 }, /* yellow */
		{ 0xff, 0xff, 0xff }, /* white */
	};
	const uint16_t bar_count = ARRAY_SIZE(color_bars);

	for (uint16_t y = 0; y < height; y++) {
		for (uint16_t x = 0; x < width; x++) {
			uint16_t bar = (uint16_t)(((uint32_t)x * bar_count) /
						  MAX(width, (uint16_t)1U));
			uint8_t red;
			uint8_t green;
			uint8_t blue;

			if (bar >= bar_count) {
				bar = bar_count - 1U;
			}

			red = color_bars[bar][0];
			green = color_bars[bar][1];
			blue = color_bars[bar][2];

			if ((y > (height * 3U) / 4U) &&
			    (((x / 16U) & 1U) != ((y / 16U) & 1U))) {
				red >>= 1;
				green >>= 1;
				blue >>= 1;
			}

			dst[(size_t)y * width + x] =
				bayer_from_rgb(x, y, red, green, blue);
		}
	}
}

/* Seed the raw reserved-memory input frame into reserved memory based on DTS offsets. */
static int seed_memory_frame(void)
{
	uint8_t *raw;
	size_t raw_end_offset;

	if ((APP_MEM_BASE == 0U) || (APP_MEM_SIZE == 0U)) {
		LOG_ERR("Encoder memory base/size missing in DTS");
		return -EINVAL;
	}

	if ((APP_MEMORY_INPUT_RAW_SIZE > APP_MEM_SIZE) ||
	    (APP_SRC_OFFSET > (APP_MEM_SIZE - APP_MEMORY_INPUT_RAW_SIZE))) {
		LOG_ERR("reserved-memory raw source buffer does not fit reserved memory");
		return -ENOMEM;
	}

	raw_end_offset = APP_SRC_OFFSET + APP_MEMORY_INPUT_RAW_SIZE;

	if (APP_DST_OFFSET < raw_end_offset) {
		LOG_ERR("reserved-memory JPEG destination overlaps raw source");
		return -ENOMEM;
	}

	raw = (uint8_t *)(uintptr_t)(APP_MEM_BASE + APP_SRC_OFFSET);
	fill_bayer_pattern(raw, APP_MEMORY_INPUT_WIDTH, APP_MEMORY_INPUT_HEIGHT);
	enc_mem_flush((uintptr_t)raw, APP_MEMORY_INPUT_RAW_SIZE);

	LOG_INF("Seeded encoder-memory raw Bayer color-bar pattern at %p size=%u (%ux%u)",
		raw, (unsigned int)APP_MEMORY_INPUT_RAW_SIZE,
		APP_MEMORY_INPUT_WIDTH, APP_MEMORY_INPUT_HEIGHT);
	return 0;
}

/* Capture execution paths */

static int wait_and_report(const struct device *enc, uint32_t mode)
{
	struct video_buffer *out = NULL;
	int ret;

	ret = video_dequeue(enc, &out, APP_DEQUEUE_TIMEOUT);
	LOG_INF("enc video_dequeue ret=%d", ret);
	if (ret != 0) {
		LOG_ERR("failed to dequeue JPEG output ret=%d", ret);
		return ret;
	}

	LOG_INF("JPEG output: addr=%p bytesused=%u",
		out->buffer, (unsigned int)out->bytesused);

	if (out->bytesused != 0U) {
#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
        (void)sys_cache_data_flush_range(out->buffer, out->bytesused);
#endif
		LOG_INF("GDB dump (JPEG): dump binary memory out.jpg %p (%p + %u)",
			out->buffer, out->buffer, (unsigned int)out->bytesused);
	}

	if ((APP_MEM_BASE != 0U) && (APP_MEM_SIZE != 0U)) {
		uintptr_t raw_addr;
		size_t raw_size;

		if (mode == APP_MODE_MEMORY_INPUT) {
			raw_addr = APP_MEM_BASE + (uintptr_t)APP_SRC_OFFSET;
			raw_size = APP_MEMORY_INPUT_RAW_SIZE;
		} else {
			raw_addr = APP_MEM_BASE + (uintptr_t)APP_RAW_OFFSET;
			raw_size = (size_t)APP_LIVE_WIDTH * (size_t)APP_LIVE_HEIGHT;
		}

		if (raw_size != 0U) {
			LOG_INF("GDB dump (raw): dump binary memory frame_dump.raw 0x%lx (0x%lx + 0x%lx)",
				(unsigned long)raw_addr,
				(unsigned long)raw_addr,
				(unsigned long)raw_size);
		}
	}

	return 0;
}

/* Stop streams and return any queued buffers back to the app. */
static void cleanup_capture(const struct device *enc,
			    bool jpeg_buffers_queued,
			    bool enc_started)
{
	if (enc_started) {
		/*
		 * The wrapper owns live-path shutdown ordering in mode 0 and
		 * stops the live input before dismantling the JPEG encoder pipeline.
		 */
		(void)video_stream_stop(enc, VIDEO_BUF_TYPE_OUTPUT);
	} else if (jpeg_buffers_queued) {
		/*
		 * If stream start never succeeded, use flush(cancel=true) to reclaim
		 * any queued output buffers without issuing stream_stop().
		 */
		(void)video_flush(enc, true);
	}

	if (jpeg_buffers_queued) {
		drain_buffers(enc);
	}
}

/* Run mode 0: live input-to-memory capture through the JPEG encoder. */
static int run_live_mode(const struct device *enc)
{
	struct video_format jpeg_fmt;
	bool jpeg_buffers_queued = false;
	bool enc_started = false;
	int ret;

	ret = enc_configure(enc, &jpeg_fmt,
			    APP_LIVE_WIDTH, APP_LIVE_HEIGHT);
	if (ret != 0) {
		LOG_ERR("JPEG encoder format configuration failed ret=%d", ret);
		goto out;
	}

	ret = jpeg_buffers_queue(enc, &jpeg_fmt);
	if (ret != 0) {
		LOG_ERR("failed to prepare JPEG encoder buffers ret=%d", ret);
		goto out;
	}
	jpeg_buffers_queued = true;

	ret = video_stream_start(enc, VIDEO_BUF_TYPE_OUTPUT);
	LOG_INF("enc video_stream_start ret=%d", ret);
	if (ret != 0) {
		LOG_ERR("JPEG encoder stream start failed ret=%d", ret);
		goto out;
	}
	enc_started = true;

	ret = wait_and_report(enc, APP_MODE_SENSOR_TO_MEMORY);
	if (ret != 0) {
		LOG_ERR("live-mode JPEG capture failed ret=%d", ret);
	}

out:
	cleanup_capture(enc, jpeg_buffers_queued, enc_started);
	return ret;
}

/* Run mode 1: reserved-memory input test pattern through the JPEG encoder. */
static int run_memory_mode(const struct device *enc)
{
	struct video_format jpeg_fmt;
	bool jpeg_buffers_queued = false;
	bool enc_started = false;
	int ret;

	ret = enc_configure(enc, &jpeg_fmt,
			    APP_MEMORY_INPUT_WIDTH, APP_MEMORY_INPUT_HEIGHT);
	if (ret != 0) {
		LOG_ERR("JPEG encoder format configuration failed ret=%d", ret);
		return ret;
	}

	ret = seed_memory_frame();
	if (ret != 0) {
		LOG_ERR("failed to seed reserved-memory input frame ret=%d", ret);
		return ret;
	}

	ret = jpeg_buffers_queue(enc, &jpeg_fmt);
	if (ret != 0) {
		LOG_ERR("failed to prepare JPEG encoder buffers ret=%d", ret);
		goto out;
	}
	jpeg_buffers_queued = true;

	ret = video_stream_start(enc, VIDEO_BUF_TYPE_OUTPUT);
	LOG_INF("enc video_stream_start ret=%d", ret);
	if (ret != 0) {
		LOG_ERR("JPEG encoder stream start failed ret=%d", ret);
		goto out;
	}
	enc_started = true;

	ret = wait_and_report(enc, APP_MODE_MEMORY_INPUT);
	if (ret != 0) {
		LOG_ERR("reserved-memory JPEG capture failed ret=%d", ret);
	}

out:
	cleanup_capture(enc, jpeg_buffers_queued, enc_started);
	return ret;
}

/* Application entry point: select the devicetree-configured run mode. */
int main(void)
{
	const struct device *enc = DEVICE_DT_GET(ENC_NODE);
	uint32_t mode = APP_PIPELINE_MODE;
	int ret;

	LOG_INF("JPEG encoder validator start (%s, mode=%u)",
		mode_name(mode), (unsigned int)mode);

	if (!device_is_ready(enc)) {
		LOG_ERR("JPEG encoder device not ready");
		return -ENODEV;
	}

	if (mode == APP_MODE_MEMORY_INPUT) {
		ret = run_memory_mode(enc);
	} else if (mode == APP_MODE_SENSOR_TO_MEMORY) {
		ret = run_live_mode(enc);
	} else {
		LOG_ERR("unsupported sample mode %u", (unsigned int)mode);
		return -EINVAL;
	}

	if (ret != 0) {
		LOG_ERR("JPEG encoder validator failed (%s, ret=%d)",
			mode_name(mode), ret);
	}
	LOG_INF("JPEG encoder validator done ret=%d", ret);
	return ret;
}
