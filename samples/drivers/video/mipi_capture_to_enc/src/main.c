/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
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

#if IS_ENABLED(CONFIG_USB_TRANSPORT_CDC_ACM)
#include "usb_cdc_transport.h"
#endif

#if IS_ENABLED(CONFIG_STORE_TO_XSPI)
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#endif

LOG_MODULE_REGISTER(mipi_capture_to_enc, LOG_LEVEL_INF);

#if IS_ENABLED(CONFIG_USB_TRANSPORT_CDC_ACM)
static struct cdc_transport_ctx cdc_ctx;
#endif

#define MIPI_DEV_NODE DT_NODELABEL(video_syna0)
#define ENC_NODE DT_NODELABEL(enc0)

#if !DT_NODE_EXISTS(MIPI_DEV_NODE)
#error "Devicetree node 'video_syna0' is required for this sample"
#endif

#if !DT_NODE_EXISTS(ENC_NODE)
#error "Devicetree node 'enc0' is required for this sample"
#endif

#if !DT_NODE_HAS_COMPAT(ENC_NODE, syna_enc_video)
#error "enc0 must use compatible = \"syna,enc-video\""
#endif

BUILD_ASSERT(DT_PROP_OR(ENC_NODE, mode, 0) == 1,
	     "This sample requires enc0 mode = <1> (reserved-memory input)");
BUILD_ASSERT(DT_NODE_HAS_PROP(MIPI_DEV_NODE, memory_region),
	     "video_syna0 must provide a memory-region for capture");
BUILD_ASSERT(DT_NODE_HAS_PROP(ENC_NODE, memory_region),
	     "enc0 must provide a memory-region for encoder raw/JPEG memory");

#define APP_CAPTURE_WIDTH 1920U
#define APP_CAPTURE_HEIGHT 1080U
#define APP_CAPTURE_PITCH APP_CAPTURE_WIDTH
#define APP_CAPTURE_FRAME_SIZE ((size_t)APP_CAPTURE_WIDTH * (size_t)APP_CAPTURE_HEIGHT)
#define APP_CAPTURE_PIXEL_FORMAT VIDEO_PIX_FMT_SRGGB8
#define APP_CAPTURE_TIMEOUT K_SECONDS(5)
#define APP_CAPTURE_WARMUP_DELAY_MS 1000U

#define APP_QUADRANT_WIDTH 960U
#define APP_QUADRANT_HEIGHT 540U
#define APP_QUADRANT_RAW_SIZE ((size_t)APP_QUADRANT_WIDTH * (size_t)APP_QUADRANT_HEIGHT)

#define APP_NUM_JPEG_BUFS 1U
#define APP_JPEG_BUF_ALIGN 64U
#define APP_JPEG_DEQUEUE_TIMEOUT K_SECONDS(5)

#define APP_MIPI_MEM_NODE DT_PHANDLE(MIPI_DEV_NODE, memory_region)
#define APP_MIPI_MEM_BASE ((uintptr_t)DT_REG_ADDR(APP_MIPI_MEM_NODE))
#define APP_MIPI_MEM_SIZE ((size_t)DT_REG_SIZE(APP_MIPI_MEM_NODE))

#define APP_ENC_MEM_NODE DT_PHANDLE(ENC_NODE, memory_region)
#define APP_ENC_MEM_BASE ((uintptr_t)DT_REG_ADDR(APP_ENC_MEM_NODE))
#define APP_ENC_MEM_SIZE ((size_t)DT_REG_SIZE(APP_ENC_MEM_NODE))

#define APP_ENC_SRC_OFFSET ((size_t)DT_PROP_OR(ENC_NODE, frame_raw_offset, 0))
#define APP_ENC_MAX_JPEG_SIZE ((size_t)DT_PROP_OR(ENC_NODE, max_jpeg_size, (128U * 1024U)))

#define APP_NUM_QUADRANTS 4U

#if DT_NODE_EXISTS(DT_NODELABEL(xspi_stig))
#define APP_XSPI_XIP_BASE ((uintptr_t)DT_REG_ADDR(DT_NODELABEL(xspi_stig)))
#else
#define APP_XSPI_XIP_BASE ((uintptr_t)0U)
#endif

static uint8_t app_jpeg_buf_mem[APP_NUM_JPEG_BUFS][APP_ENC_MAX_JPEG_SIZE] __aligned(APP_JPEG_BUF_ALIGN);
static struct video_buffer app_jpeg_vbufs[APP_NUM_JPEG_BUFS];

#if IS_ENABLED(CONFIG_STORE_TO_XSPI)
#define APP_FLASH_MAGIC UINT32_C(0x514A5047) /* "QJPG" */
#define APP_FLASH_VERSION 1U

struct app_flash_entry {
	uint32_t offset;
	uint32_t length;
	uint32_t crc32;
} __packed;

struct app_flash_header {
	uint32_t magic;
	uint32_t version;
	uint32_t count;
	uint32_t reserved;
	struct app_flash_entry entry[APP_NUM_QUADRANTS];
} __packed;

static int flash_write_verify_retry(const struct flash_area *fa, size_t erase_size, size_t offset,
				   const uint8_t *data, size_t len)
{
	uint8_t verify_buf[64];
	size_t verify_len;
	size_t erase_len;
	int ret;
	int last_ret = -EIO;

	if ((fa == NULL) || (data == NULL) || (len == 0U) || (erase_size == 0U)) {
		return -EINVAL;
	}

	/* Writes are erase-block aligned since we erase+rewrite per stored JPEG chunk. */
	if ((offset % erase_size) != 0U) {
		return -EINVAL;
	}

	verify_len = MIN(sizeof(verify_buf), len);
	erase_len = ROUND_UP(len, erase_size);

	for (int attempt = 0; attempt < 3; attempt++) {
		/*
		 * XSPI operations can fail transiently (e.g. controller busy after prior
		 * erase/write). Use a small backoff + optional re-erase on retries.
		 */
		if (attempt > 0) {
			k_sleep(K_MSEC((attempt == 1) ? 50 : 200));
			ret = flash_area_erase(fa, offset, erase_len);
			if (ret != 0) {
				last_ret = ret;
				continue;
			}
			/* Give the controller/flash time to settle after erase. */
			k_sleep(K_MSEC(5));
		}

		ret = flash_area_write(fa, offset, data, len);
		if (ret != 0) {
			last_ret = ret;
			continue;
		}

		ret = flash_area_read(fa, offset, verify_buf, verify_len);
		if (ret != 0) {
			last_ret = ret;
			continue;
		}

		if (memcmp(verify_buf, data, verify_len) == 0) {
			return 0;
		}

		last_ret = -EIO;
	}

	return last_ret;
}

static int flash_store_jpeg(const struct flash_area *fa, size_t erase_size,
			    size_t offset, const uint8_t *data, size_t len, uint32_t *crc_out,
			    const char *tag)
{
	size_t erase_len;
	int ret;

	if ((fa == NULL) || (data == NULL) || (len == 0U) || (erase_size == 0U)) {
		return -EINVAL;
	}

	/* Enforce erase-block alignment so each JPEG can be erased/replaced independently. */
	if ((offset % erase_size) != 0U) {
		return -EINVAL;
	}

	erase_len = ROUND_UP(len, erase_size);
	if ((offset > fa->fa_size) || (erase_len > (fa->fa_size - offset))) {
		LOG_ERR("jpeg_store overflow: off=0x%x len=0x%x area=0x%x",
			(unsigned int)offset, (unsigned int)len, (unsigned int)fa->fa_size);
		return -ENOMEM;
	}

	for (int attempt = 0; attempt < 3; attempt++) {
		if (attempt > 0) {
			k_sleep(K_MSEC((attempt == 1) ? 50 : 200));
		}

		ret = flash_area_erase(fa, offset, erase_len);
		if (ret != 0) {
			LOG_WRN("flash_area_erase off=0x%x len=0x%x failed: %d",
				(unsigned int)offset, (unsigned int)erase_len, ret);
			continue;
		}

		/* Some flashes/controllers need breathing room after erase. */
		k_sleep(K_MSEC(5));
		break;
	}

	if (ret != 0) {
		LOG_ERR("flash_area_erase off=0x%x len=0x%x failed after retries: %d",
			(unsigned int)offset, (unsigned int)erase_len, ret);
		return ret;
	}

	ret = flash_write_verify_retry(fa, erase_size, offset, data, len);
	if (ret != 0) {
		LOG_ERR("XSPI write/verify failed off=0x%x len=0x%x: %d",
			(unsigned int)offset, (unsigned int)len, ret);
		return ret;
	}

	uint32_t crc = 0U;

#if IS_ENABLED(CONFIG_CRC)
	crc = crc32_ieee(data, len);
#endif

	if (crc_out != NULL) {
		*crc_out = crc;
	}

	LOG_INF("Stored JPEG to XSPI off=0x%x bytes=%u erase=%u crc32=0x%08x",
		(unsigned int)offset, (uint32_t)len, (uint32_t)erase_size, crc);

	if (APP_XSPI_XIP_BASE != 0U) {
		uintptr_t xip_addr = APP_XSPI_XIP_BASE + (uintptr_t)fa->fa_off + (uintptr_t)offset;
		LOG_INF("GDB dump command: dump binary memory %s.jpg 0x%x (0x%x + %u)",
			tag != NULL ? tag : "quadrant",
			(unsigned int)xip_addr, (unsigned int)xip_addr, (unsigned int)len);
	} else {
		LOG_INF("GDB dump command: dump binary memory %s.jpg (XIP_BASE + 0x%x + 0x%x) ((XIP_BASE + 0x%x + 0x%x) + %u)",
			tag != NULL ? tag : "quadrant",
			(unsigned int)fa->fa_off, (unsigned int)offset,
			(unsigned int)fa->fa_off, (unsigned int)offset, (unsigned int)len);
	}
	return 0;
}
#endif /* CONFIG_STORE_TO_XSPI */

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
static void mem_flush(uintptr_t addr, size_t size)
{
	int ret;

	if (size == 0U) {
		return;
	}

	ret = sys_cache_data_flush_range((void *)addr, size);
	if (ret != 0) {
		LOG_WRN("Cache flush failed ret=%d addr=%p size=%u",
			ret, (void *)addr, (unsigned int)size);
	}
	barrier_dsync_fence_full();
}

static void mem_flush_invalidate(uintptr_t addr, size_t size)
{
	int ret;

	if (size == 0U) {
		return;
	}

	ret = sys_cache_data_flush_and_invd_range((void *)addr, size);
	if (ret != 0) {
		LOG_WRN("Cache flush+invalidate failed ret=%d addr=%p size=%u",
			ret, (void *)addr, (unsigned int)size);
	}
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

static void drain_buffers(const struct device *dev)
{
	struct video_buffer *vbuf = NULL;
	int ret;

	while (true) {
		ret = video_dequeue(dev, &vbuf, K_NO_WAIT);
		if (ret != 0) {
			LOG_DBG("drain_buffers: video_dequeue failed: %d", ret);
			break;
		}

		if (vbuf == NULL) {
			break;
		}

		/* Drain all returned buffers during cleanup. */
	}
}

static int mipi_configure(const struct device *mipi_dev, struct video_format *fmt)
{
	int ret;

	*fmt = (struct video_format) {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = APP_CAPTURE_PIXEL_FORMAT,
		.width = APP_CAPTURE_WIDTH,
		.height = APP_CAPTURE_HEIGHT,
		.pitch = APP_CAPTURE_PITCH,
	};

	ret = video_set_format(mipi_dev, fmt);
	LOG_INF("mipi format configured width=%u height=%u pitch=%u size=%u",
		fmt->width, fmt->height, fmt->pitch, fmt->size);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

static int mipi_buffer_prepare(const struct video_caps *caps,
			       const struct video_format *fmt,
			       struct video_buffer *vbuf)
{
	size_t frame_size;

	frame_size = (fmt->size != 0U) ? (size_t)fmt->size : APP_CAPTURE_FRAME_SIZE;

	if ((APP_MIPI_MEM_BASE == 0U) || (APP_MIPI_MEM_SIZE < frame_size)) {
		LOG_ERR("Capture memory-region too small: size=0x%x need=0x%x",
			(unsigned int)APP_MIPI_MEM_SIZE, (unsigned int)frame_size);
		return -ENOMEM;
	}

	if ((APP_MIPI_MEM_BASE % MAX(caps->buf_align, 1U)) != 0U) {
		LOG_ERR("Capture SHM addr 0x%x is not aligned to %u",
			(unsigned int)APP_MIPI_MEM_BASE,
			(unsigned int)MAX(caps->buf_align, 1U));
		return -EINVAL;
	}

	memset(vbuf, 0, sizeof(*vbuf));
	vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
	vbuf->buffer = (void *)APP_MIPI_MEM_BASE;
	vbuf->size = frame_size;
	return 0;
}

struct app_quadrant {
	const char *name;
	uint32_t x_offset;
	uint32_t y_offset;
};

static const struct app_quadrant app_quadrants[APP_NUM_QUADRANTS] = {
	{ .name = "top-left", .x_offset = 0U, .y_offset = 0U },
	{ .name = "top-right", .x_offset = APP_QUADRANT_WIDTH, .y_offset = 0U },
	{ .name = "bottom-left", .x_offset = 0U, .y_offset = APP_QUADRANT_HEIGHT },
	{ .name = "bottom-right", .x_offset = APP_QUADRANT_WIDTH, .y_offset = APP_QUADRANT_HEIGHT },
};

static int quadrant_copy_to_encoder(const struct video_buffer *captured,
				    uint32_t quadrant_x_offset, uint32_t quadrant_y_offset,
				    const char *quadrant_name)
{
	const uint8_t *src;
	uint8_t *dst;
	size_t src_stride = APP_CAPTURE_PITCH;
	size_t dst_stride = APP_QUADRANT_WIDTH;
	size_t src_x;
	size_t src_y;
	size_t src_start;
	size_t src_end;
	size_t dst_end;

	if ((captured == NULL) || (captured->buffer == NULL)) {
		return -EINVAL;
	}

	src_x = (size_t)quadrant_x_offset;
	src_y = (size_t)quadrant_y_offset;
	src_start = (src_y * src_stride) + src_x;
	src_end = src_start + ((size_t)(APP_QUADRANT_HEIGHT - 1U) * src_stride) + APP_QUADRANT_WIDTH;
	if (captured->bytesused < src_end) {
		LOG_ERR("MIPI frame too small: bytes=%u need=%u",
			(unsigned int)captured->bytesused, (unsigned int)src_end);
		return -EINVAL;
	}

	dst_end = APP_ENC_SRC_OFFSET + APP_QUADRANT_RAW_SIZE;
	if ((APP_ENC_MEM_BASE == 0U) ||
	    (APP_ENC_SRC_OFFSET > APP_ENC_MEM_SIZE) ||
	    (APP_QUADRANT_RAW_SIZE > (APP_ENC_MEM_SIZE - APP_ENC_SRC_OFFSET))) {
		LOG_ERR("Encoder raw source does not fit reserved memory");
		return -ENOMEM;
	}

	src = (const uint8_t *)captured->buffer;
	dst = (uint8_t *)(APP_ENC_MEM_BASE + APP_ENC_SRC_OFFSET);

	for (size_t row = 0; row < APP_QUADRANT_HEIGHT; row++) {
		const uint8_t *src_row = src + src_start + (row * src_stride);
		uint8_t *dst_row = dst + (row * dst_stride);

		memcpy(dst_row, src_row, dst_stride);
	}

	mem_flush((uintptr_t)dst, APP_QUADRANT_RAW_SIZE);

	LOG_INF("Copied %s quadrant to encoder memory at %p (from x=%u y=%u)",
		quadrant_name != NULL ? quadrant_name : "unknown", (void *)dst,
		(unsigned int)src_x, (unsigned int)src_y);
	return 0;
}

static int enc_configure(const struct device *enc, struct video_format *jpeg_fmt)
{
	int ret;

	*jpeg_fmt = (struct video_format) {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width = APP_QUADRANT_WIDTH,
		.height = APP_QUADRANT_HEIGHT,
	};

	ret = video_set_format(enc, jpeg_fmt);
	return ret;
}

static int jpeg_buffers_queue(const struct device *enc, const struct video_format *jpeg_fmt)
{
	struct video_caps caps = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	size_t align = APP_JPEG_BUF_ALIGN;
	int ret;

	if ((jpeg_fmt == NULL) || (jpeg_fmt->size > APP_ENC_MAX_JPEG_SIZE)) {
		LOG_ERR("JPEG output capacity too large: need=%u max=%u",
			jpeg_fmt != NULL ? jpeg_fmt->size : 0U,
			(unsigned int)APP_ENC_MAX_JPEG_SIZE);
		return -EINVAL;
	}

	ret = video_get_caps(enc, &caps);
	LOG_INF("enc video_get_caps ret=%d min_vbuf=%u align=%zu",
		ret, caps.min_vbuf_count, caps.buf_align);
	if (ret != 0) {
		return ret;
	}

	if (APP_NUM_JPEG_BUFS < caps.min_vbuf_count) {
		LOG_ERR("APP_NUM_JPEG_BUFS too small: available=%u required=%u",
			(unsigned int)APP_NUM_JPEG_BUFS, caps.min_vbuf_count);
		return -EINVAL;
	}

	if (caps.buf_align != 0U) {
		align = caps.buf_align;
	}

	if (align > APP_JPEG_BUF_ALIGN) {
		LOG_ERR("JPEG buffer alignment too small: required=%u available=%u",
			(unsigned int)align, (unsigned int)APP_JPEG_BUF_ALIGN);
		return -EINVAL;
	}

	for (size_t i = 0; i < APP_NUM_JPEG_BUFS; i++) {
		struct video_buffer *vbuf = &app_jpeg_vbufs[i];

		memset(vbuf, 0, sizeof(*vbuf));
		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
		vbuf->buffer = app_jpeg_buf_mem[i];
		vbuf->size = APP_ENC_MAX_JPEG_SIZE;

		/*
		 * Output buffer is written by the encoder; clear any dirty cache lines
		 * before DMA so they cannot be written back later and corrupt output.
		 */
		mem_flush_invalidate((uintptr_t)vbuf->buffer, vbuf->size);

		LOG_INF("enc app_buf[%u] addr=%p size=%u align=%zu",
			(unsigned int)i, (void *)vbuf->buffer, (unsigned int)vbuf->size, align);
		ret = video_enqueue(enc, vbuf);
		LOG_INF("enc video_enqueue[%u] ret=%d", (unsigned int)i, ret);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int capture_jpeg(const struct device *enc, struct video_buffer **out_buf)
{
	struct video_buffer *out = NULL;
	int ret;

	if (out_buf == NULL) {
		return -EINVAL;
	}
	*out_buf = NULL;

	ret = video_dequeue(enc, &out, APP_JPEG_DEQUEUE_TIMEOUT);
	if ((ret != 0) || (out == NULL)) {
		LOG_ERR("failed to dequeue JPEG output ret=%d", ret);
		return (ret != 0) ? ret : -EIO;
	}

	if (out->bytesused == 0U) {
		LOG_ERR("JPEG dequeue returned an empty buffer");
		return -EIO;
	}

	/*
	 * The encoder driver may populate the output buffer via DMA or by CPU copy.
	 * Use flush+invalidate so dirty CPU-written cache lines are preserved while
	 * still handling DMA-written buffers correctly.
	 */
	mem_flush_invalidate((uintptr_t)out->buffer, out->bytesused);
	LOG_INF("Frame captured at addr=%p bytesused=%u",
		(void *)out->buffer, (unsigned int)out->bytesused);
	LOG_INF("Dump JPEG from %p (%p + %u)",
		(void *)out->buffer, (void *)out->buffer, (unsigned int)out->bytesused);

	*out_buf = out;
	return 0;
}

static size_t jpeg_find_eoi_len(const uint8_t *buf, size_t len)
{
	if ((buf == NULL) || (len < 2U)) {
		return 0U;
	}

	/* Search backwards for the JPEG EOI marker (0xFF 0xD9). */
	for (size_t i = len - 2U; i > 0U; i--) {
		if ((buf[i] == 0xFF) && (buf[i + 1U] == 0xD9)) {
			return i + 2U;
		}
	}

	/* Also check the first possible position. */
	if ((buf[0] == 0xFF) && (buf[1U] == 0xD9)) {
		return 2U;
	}

	return 0U;
}

int main(void)
{
	const struct device *mipi_dev = DEVICE_DT_GET(MIPI_DEV_NODE);
	const struct device *enc = DEVICE_DT_GET(ENC_NODE);
#if IS_ENABLED(CONFIG_STORE_TO_XSPI)
	const struct flash_area *fa = NULL;
	struct flash_pages_info page;
	size_t flash_erase_size = 0U;
	struct app_flash_header hdr;
	size_t flash_write_offset = 0U;
#endif
	struct video_caps mipi_caps = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	struct video_format mipi_fmt;
	struct video_format jpeg_fmt;
	struct video_buffer mipi_vbuf;
	struct video_buffer *mipi_frame = NULL;
	struct video_buffer *jpeg_out = NULL;
	bool mipi_buffer_queued = false;
	bool mipi_started = false;
	bool enc_buffer_queued = false;
	int ret;
	int final_ret = 0;

	LOG_INF("MIPI capture-to-encoder sample start (encode 4 quadrants)");

	if (!device_is_ready(mipi_dev)) {
		LOG_ERR("MIPI device %s is not ready", mipi_dev->name);
		return -ENODEV;
	}

	if (!device_is_ready(enc)) {
		LOG_ERR("Encoder device %s is not ready", enc->name);
		return -ENODEV;
	}

	ret = video_get_caps(mipi_dev, &mipi_caps);
	LOG_INF("mipi video_get_caps ret=%d min_vbuf=%u align=%u",
		ret, mipi_caps.min_vbuf_count, (unsigned int)mipi_caps.buf_align);
	if (ret != 0) {
		LOG_ERR("mipi video_get_caps failed: %d", ret);
		return ret;
	}

	ret = mipi_configure(mipi_dev, &mipi_fmt);
	LOG_INF("mipi video_set_format ret=%d size=%u pitch=%u (%ux%u)",
		ret, mipi_fmt.size, mipi_fmt.pitch, mipi_fmt.width, mipi_fmt.height);
	if (ret != 0) {
		LOG_ERR("mipi video_set_format failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}

	ret = mipi_buffer_prepare(&mipi_caps, &mipi_fmt, &mipi_vbuf);
	if (ret != 0) {
		final_ret = ret;
		goto out_mipi_cleanup;
	}

	LOG_INF("mipi app_buf addr=%p size=%u", (void *)mipi_vbuf.buffer, (unsigned int)mipi_vbuf.size);
	ret = video_enqueue(mipi_dev, &mipi_vbuf);
	LOG_INF("mipi video_enqueue ret=%d", ret);
	if (ret != 0) {
		LOG_ERR("mipi video_enqueue failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}
	mipi_buffer_queued = true;

	ret = video_stream_start(mipi_dev, VIDEO_BUF_TYPE_OUTPUT);
	LOG_INF("mipi video_stream_start ret=%d", ret);
	if (ret != 0) {
		LOG_ERR("mipi video_stream_start failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}
	mipi_started = true;

	k_msleep(APP_CAPTURE_WARMUP_DELAY_MS);

	ret = video_dequeue(mipi_dev, &mipi_frame, APP_CAPTURE_TIMEOUT);
	LOG_INF("mipi video_dequeue ret=%d frame=%p bytesused=%u",
		ret, (void *)mipi_frame, mipi_frame != NULL ? (unsigned int)mipi_frame->bytesused : 0U);
	if ((ret != 0) || (mipi_frame == NULL)) {
		LOG_ERR("mipi video_dequeue failed: %d", ret);
		final_ret = (ret != 0) ? ret : -EIO;
		goto out_mipi_cleanup;
	}

#if IS_ENABLED(CONFIG_USB_TRANSPORT_CDC_ACM) &&                                           \
	!IS_ENABLED(CONFIG_STORE_TO_XSPI)
	/*
	 * USB-only flow: capture the full frame first, then enumerate/wait for the
	 * host, and only then run the encode+send loop.
	 *
	 * Note: keep MIPI running; stopping it can disable encoder clock/reset domains.
	 */
	ret = cdc_transport_init(&cdc_ctx);
	if (ret != 0) {
		LOG_ERR("USB CDC init failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}

	LOG_INF("USB CDC ready; waiting for host DTR...");
	ret = cdc_transport_wait_dtr(&cdc_ctx, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("USB CDC wait DTR failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}
	LOG_INF("Host DTR set");
#endif

#if IS_ENABLED(CONFIG_STORE_TO_XSPI)
	ret = flash_area_open(FIXED_PARTITION_ID(jpeg_store), &fa);
	if (ret != 0) {
		LOG_ERR("flash_area_open(jpeg_store) failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}

	ret = flash_get_page_info_by_offs(fa->fa_dev, fa->fa_off, &page);
	if (ret != 0) {
		LOG_ERR("flash_get_page_info_by_offs failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}

	flash_erase_size = page.size;
	if (flash_erase_size == 0U) {
		LOG_ERR("Invalid flash erase size");
		final_ret = -EINVAL;
		goto out_mipi_cleanup;
	}

	memset(&hdr, 0, sizeof(hdr));
	hdr.magic = APP_FLASH_MAGIC;
	hdr.version = APP_FLASH_VERSION;
	hdr.count = APP_NUM_QUADRANTS;

	flash_write_offset = ROUND_UP(sizeof(hdr), flash_erase_size);

	for (size_t i = 0; i < APP_NUM_QUADRANTS; i++) {
		hdr.entry[i].offset = UINT32_MAX;
		hdr.entry[i].length = UINT32_MAX;
		hdr.entry[i].crc32 = UINT32_MAX;
	}
#endif

	LOG_INF("Configuring encoder JPEG output");
	ret = enc_configure(enc, &jpeg_fmt);
	LOG_INF("enc video_set_format ret=%d capacity=%u pitch=%u (%ux%u)",
		ret, jpeg_fmt.size, jpeg_fmt.pitch, jpeg_fmt.width, jpeg_fmt.height);
	if (ret != 0) {
		LOG_ERR("enc video_set_format failed: %d", ret);
		final_ret = ret;
		goto out_enc_cleanup;
	}

	ret = jpeg_buffers_queue(enc, &jpeg_fmt);
	if (ret != 0) {
		LOG_ERR("enc buffer queue failed: %d", ret);
		final_ret = ret;
		goto out_enc_cleanup;
	}
	enc_buffer_queued = true;

	for (size_t i = 0; i < APP_NUM_QUADRANTS; i++) {
		const struct app_quadrant *q = &app_quadrants[i];
		size_t jpeg_len;

		LOG_INF("Encoding quadrant[%u]=%s", (unsigned int)i, q->name);

		ret = quadrant_copy_to_encoder(mipi_frame, q->x_offset, q->y_offset, q->name);
		if (ret != 0) {
			final_ret = ret;
			goto out_enc_cleanup;
		}

		ret = video_stream_start(enc, VIDEO_BUF_TYPE_OUTPUT);
		LOG_INF("enc video_stream_start ret=%d", ret);
		if (ret != 0) {
			LOG_ERR("enc video_stream_start failed: %d", ret);
			final_ret = ret;
			goto out_enc_cleanup;
		}

		ret = capture_jpeg(enc, &jpeg_out);
		if (ret != 0) {
			final_ret = ret;
			(void)video_stream_stop(enc, VIDEO_BUF_TYPE_OUTPUT);
			goto out_enc_cleanup;
		}

		ret = video_stream_stop(enc, VIDEO_BUF_TYPE_OUTPUT);
		if (ret != 0) {
			LOG_WRN("enc video_stream_stop failed: %d", ret);
		}

		jpeg_len = jpeg_find_eoi_len((const uint8_t *)jpeg_out->buffer, jpeg_out->bytesused);
		if ((jpeg_len == 0U) || (jpeg_len > jpeg_out->bytesused)) {
			const uint8_t *b = (const uint8_t *)jpeg_out->buffer;
			size_t n = (size_t)jpeg_out->bytesused;
			if ((b != NULL) && (n >= 2U)) {
				LOG_ERR("JPEG header bytes: %02x %02x ... tail: %02x %02x",
					b[0], b[1], b[n - 2U], b[n - 1U]);
			}
			LOG_ERR("JPEG EOI not found (bytesused=%u)", (unsigned int)jpeg_out->bytesused);
			final_ret = -EIO;
			goto out_enc_cleanup;
		}

#if IS_ENABLED(CONFIG_USB_TRANSPORT_CDC_ACM) &&                                           \
	!IS_ENABLED(CONFIG_STORE_TO_XSPI)
		ret = cdc_transport_send_jpeg(&cdc_ctx, (uint16_t)i, (const uint8_t *)jpeg_out->buffer,
					      jpeg_len);
		if (ret != 0) {
			LOG_ERR("CDC send quadrant[%u] failed: %d", (unsigned int)i, ret);
			final_ret = ret;
			goto out_enc_cleanup;
		}
#endif
#if IS_ENABLED(CONFIG_STORE_TO_XSPI)
		uint32_t crc32 = 0U;

		hdr.entry[i].offset = (uint32_t)flash_write_offset;
		hdr.entry[i].length = (uint32_t)jpeg_len;

		ret = flash_store_jpeg(fa, flash_erase_size, flash_write_offset,
				       (const uint8_t *)jpeg_out->buffer, jpeg_len,
				       &crc32, q->name);
		if (ret != 0) {
			final_ret = ret;
			goto out_enc_cleanup;
		}

		hdr.entry[i].crc32 = crc32;
		/* Advance by erase-size rounding to keep each JPEG erase-block aligned. */
		flash_write_offset += ROUND_UP(jpeg_len, flash_erase_size);
#endif

		jpeg_out->bytesused = 0U;
		mem_flush_invalidate((uintptr_t)jpeg_out->buffer, jpeg_out->size);
		ret = video_enqueue(enc, jpeg_out);
		if (ret != 0) {
			LOG_ERR("enc video_enqueue recycle failed: %d", ret);
			final_ret = ret;
			goto out_enc_cleanup;
		}
		jpeg_out = NULL;

	}

#if IS_ENABLED(CONFIG_STORE_TO_XSPI)
	ret = flash_area_erase(fa, 0, flash_erase_size);
	if (ret != 0) {
		LOG_ERR("flash_area_erase header failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}

	k_sleep(K_MSEC(2));
	ret = flash_write_verify_retry(fa, flash_erase_size, 0,
						(const uint8_t *)&hdr, sizeof(hdr));
	if (ret != 0) {
		LOG_ERR("XSPI header write/verify failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}

	LOG_INF("Updated XSPI header entries: magic=0x%08x count=%u",
		hdr.magic, (unsigned int)hdr.count);
#endif

#if IS_ENABLED(CONFIG_USB_TRANSPORT_CDC_ACM) &&                                           \
	IS_ENABLED(CONFIG_STORE_TO_XSPI)
	/* XSPI+CDC: store at boot, then stream after host asserts DTR. */
	if (enc_buffer_queued) {
		/* Release encoder buffers before reusing the same memory for flash readback. */
		(void)video_flush(enc, true);
		drain_buffers(enc);
		enc_buffer_queued = false;
	}

	if (mipi_started) {
		/* Stop capture while we wait for the host. */
		(void)video_stream_stop(mipi_dev, VIDEO_BUF_TYPE_OUTPUT);
		mipi_started = false;
	}

	ret = cdc_transport_init(&cdc_ctx);
	if (ret != 0) {
		LOG_ERR("USB CDC init failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}

	LOG_INF("USB CDC ready; waiting for host DTR...");
	ret = cdc_transport_wait_dtr(&cdc_ctx, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("USB CDC wait DTR failed: %d", ret);
		final_ret = ret;
		goto out_mipi_cleanup;
	}
	LOG_INF("Host DTR set");

	/* Give host a moment to start reading after asserting DTR. */
	k_sleep(K_MSEC(200));

	for (size_t i = 0; i < APP_NUM_QUADRANTS; i++) {
		size_t len = (size_t)hdr.entry[i].length;
		size_t off = (size_t)hdr.entry[i].offset;

		if ((len == 0U) || (len == UINT32_MAX) || (off == UINT32_MAX)) {
			LOG_ERR("Invalid XSPI header entry[%u]: off=0x%x len=0x%x",
				(unsigned int)i, (unsigned int)off, (unsigned int)len);
			final_ret = -EINVAL;
			goto out_mipi_cleanup;
		}

		if (len > APP_ENC_MAX_JPEG_SIZE) {
			LOG_ERR("XSPI entry[%u] too large: %u > %u",
				(unsigned int)i, (unsigned int)len, (unsigned int)APP_ENC_MAX_JPEG_SIZE);
			final_ret = -ENOMEM;
			goto out_mipi_cleanup;
		}

		/*
		 * Encoder has been flushed/drained above; reuse the encoder output buffer
		 * as a temporary readback buffer for XSPI replay.
		 */
		ret = flash_area_read(fa, off, app_jpeg_buf_mem[0], len);
		if (ret != 0) {
			LOG_ERR("flash_area_read entry[%u] off=0x%x len=0x%x failed: %d",
				(unsigned int)i, (unsigned int)off, (unsigned int)len, ret);
			final_ret = ret;
			goto out_mipi_cleanup;
		}

		mem_flush_invalidate((uintptr_t)app_jpeg_buf_mem[0], len);
		ret = cdc_transport_send_jpeg(&cdc_ctx, (uint16_t)i, app_jpeg_buf_mem[0], len);
		if (ret != 0) {
			LOG_ERR("CDC send XSPI quadrant[%u] failed: %d", (unsigned int)i, ret);
			final_ret = ret;
			goto out_mipi_cleanup;
		}
	}

	LOG_INF("XSPI-to-USB CDC replay complete");
	goto out_mipi_cleanup;
#endif

out_enc_cleanup:
	if (enc_buffer_queued) {
		ret = video_flush(enc, true);
		if (ret != 0) {
			LOG_WRN("enc video_flush cleanup failed: %d", ret);
		}
	}

	if (enc_buffer_queued) {
		drain_buffers(enc);
	}

out_mipi_cleanup:
#if IS_ENABLED(CONFIG_STORE_TO_XSPI)
	if (fa != NULL) {
		flash_area_close(fa);
		fa = NULL;
	}
#endif

	if (mipi_started) {
		ret = video_stream_stop(mipi_dev, VIDEO_BUF_TYPE_OUTPUT);
		if (ret != 0) {
			LOG_WRN("mipi video_stream_stop cleanup failed: %d", ret);
		}
	} else if (mipi_buffer_queued) {
		ret = video_flush(mipi_dev, true);
		if (ret != 0) {
			LOG_WRN("mipi video_flush cleanup failed: %d", ret);
		}
	}

	if (mipi_buffer_queued) {
		drain_buffers(mipi_dev);
	}

	if (final_ret != 0) {
		LOG_ERR("MIPI capture-to-encoder sample failed: %d", final_ret);
		return final_ret;
	}

	LOG_INF("MIPI capture-to-encoder sample finished");
	return 0;
}
