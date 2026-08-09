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
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/usb/class/usbd_uvc.h>
#include <zephyr/usb/usbd.h>

#include <sample_usbd.h>

#include "usb_uvc_transport.h"

#define QUAD_UVC_MAX_FRAME_SIZE (128U * 1024U)
#define QUAD_UVC_BUF_ALIGN 64U
#define UVC_TX_BUF_COUNT 2U
#define UVC_STREAM_POLL_INTERVAL_MS 1U

BUILD_ASSERT(QUAD_UVC_MAX_FRAME_SIZE <= UINT32_MAX, "UVC frame size must fit in video_buffer.bytesused");

static void uvc_transport_ensure_lock(struct uvc_transport_ctx *ctx)
{
	if (!ctx->lock_initialized) {
		k_mutex_init(&ctx->lock);
		ctx->lock_initialized = true;
	}
}

static void uvc_transport_release_buffers_locked(struct uvc_transport_ctx *ctx)
{
	for (size_t i = 0; i < UVC_TX_BUF_COUNT; i++) {
		if (ctx->tx_vbuf[i] != NULL) {
			(void)video_buffer_release(ctx->tx_vbuf[i]);
			ctx->tx_vbuf[i] = NULL;
		}
		ctx->tx_busy[i] = false;
	}
}

static void uvc_transport_reset_locked(struct uvc_transport_ctx *ctx)
{
	if (ctx->usbd_ctx != NULL) {
		if (ctx->usbd_enabled) {
			(void)usbd_disable(ctx->usbd_ctx);
		}
		(void)usbd_shutdown(ctx->usbd_ctx);
	}

	if (ctx->uvc_dev != NULL) {
		(void)uvc_device_shutdown(ctx->uvc_dev);
	}

	uvc_transport_release_buffers_locked(ctx);
	ctx->usbd_ctx = NULL;
	ctx->uvc_dev = NULL;
	ctx->max_frame_size = 0U;
	ctx->tx_idx = 0U;
	ctx->usbd_enabled = false;
	ctx->initialized = false;
}

static void uvc_transport_reclaim_done(const struct device *uvc_dev, struct uvc_transport_ctx *ctx)
{
	struct video_buffer *done = NULL;

	for (size_t i = 0; i < UVC_TX_BUF_COUNT; i++) {
		if ((video_dequeue(uvc_dev, &done, K_NO_WAIT) != 0) || (done == NULL)) {
			break;
		}

		for (size_t j = 0; j < UVC_TX_BUF_COUNT; j++) {
			if (ctx->tx_vbuf[j] == done) {
				ctx->tx_busy[j] = false;
				break;
			}
		}
	}
}

static int uvc_transport_stream_format_ready(const struct device *uvc_dev)
{
	struct video_format fmt = {
		.type = VIDEO_BUF_TYPE_INPUT,
	};

	if (uvc_dev == NULL) {
		return -ENODEV;
	}

	return video_get_format(uvc_dev, &fmt);
}

static int64_t uvc_transport_timeout_ms(k_timeout_t timeout)
{
	int64_t timeout_ms = (int64_t)k_ticks_to_ms_ceil64(timeout.ticks);

	return MAX(timeout_ms, 1);
}

int uvc_transport_init(struct uvc_transport_ctx *ctx, const struct device *video_ctrl_dev,
		       uint32_t width, uint32_t height, uint32_t max_frame_size)
{
	struct video_format fmt = {
		.type = VIDEO_BUF_TYPE_INPUT,
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width = width,
		.height = height,
		.size = max_frame_size,
	};
	int ret;

	if ((ctx == NULL) || (video_ctrl_dev == NULL) || (max_frame_size == 0U)) {
		return -EINVAL;
	}

	if (!device_is_ready(video_ctrl_dev)) {
		return -ENODEV;
	}

	uvc_transport_ensure_lock(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);

	if (ctx->initialized) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	if (max_frame_size > QUAD_UVC_MAX_FRAME_SIZE) {
		k_mutex_unlock(&ctx->lock);
		return -ENOMEM;
	}

	ctx->uvc_dev = DEVICE_DT_GET(DT_NODELABEL(uvc));
	if (!device_is_ready(ctx->uvc_dev)) {
		k_mutex_unlock(&ctx->lock);
		return -ENODEV;
	}

	uvc_device_init(ctx->uvc_dev, video_ctrl_dev);

	ret = uvc_device_add_format(ctx->uvc_dev, &fmt);
	if (ret != 0) {
		uvc_transport_reset_locked(ctx);
		k_mutex_unlock(&ctx->lock);
		return ret;
	}

	ret = uvc_device_enable(ctx->uvc_dev);
	if (ret != 0) {
		uvc_transport_reset_locked(ctx);
		k_mutex_unlock(&ctx->lock);
		return ret;
	}

	ctx->usbd_ctx = sample_usbd_init_device(NULL);
	if (ctx->usbd_ctx == NULL) {
		uvc_transport_reset_locked(ctx);
		k_mutex_unlock(&ctx->lock);
		return -ENODEV;
	}
	ctx->usbd_enabled = false;

	if (!usbd_can_detect_vbus(ctx->usbd_ctx)) {
		ret = usbd_enable(ctx->usbd_ctx);
		if (ret != 0) {
			uvc_transport_reset_locked(ctx);
			k_mutex_unlock(&ctx->lock);
			return ret;
		}
		ctx->usbd_enabled = true;
	}

	ctx->max_frame_size = max_frame_size;
	ctx->tx_idx = 0U;
	for (size_t i = 0; i < UVC_TX_BUF_COUNT; i++) {
		ctx->tx_vbuf[i] = video_buffer_aligned_alloc(max_frame_size, QUAD_UVC_BUF_ALIGN,
							     K_NO_WAIT);
		if (ctx->tx_vbuf[i] == NULL) {
			uvc_transport_reset_locked(ctx);
			k_mutex_unlock(&ctx->lock);
			return -ENOMEM;
		}
		ctx->tx_vbuf[i]->type = VIDEO_BUF_TYPE_INPUT;
		ctx->tx_busy[i] = false;
	}
	ctx->initialized = true;
	k_mutex_unlock(&ctx->lock);
	return 0;
}

int uvc_transport_wait_stream_ready(struct uvc_transport_ctx *ctx, k_timeout_t timeout)
{
	int ret;

	if ((ctx == NULL) || (ctx->uvc_dev == NULL)) {
		return -ENODEV;
	}

	if (timeout.ticks == K_NO_WAIT.ticks) {
		ret = uvc_transport_stream_format_ready(ctx->uvc_dev);
		return (ret == 0) ? 0 : -EAGAIN;
	}

	if (timeout.ticks == K_FOREVER.ticks) {
		while (true) {
			ret = uvc_transport_stream_format_ready(ctx->uvc_dev);
			if (ret == 0) {
				return 0;
			}
			if (ret != -EAGAIN) {
				return ret;
			}
			k_sleep(K_MSEC(UVC_STREAM_POLL_INTERVAL_MS));
		}
	}

	int64_t deadline_ms = (int64_t)k_uptime_get() + uvc_transport_timeout_ms(timeout);

	while ((int64_t)k_uptime_get() < deadline_ms) {
		ret = uvc_transport_stream_format_ready(ctx->uvc_dev);
		if (ret == 0) {
			return 0;
		}
		if (ret != -EAGAIN) {
			return ret;
		}
		k_sleep(K_MSEC(UVC_STREAM_POLL_INTERVAL_MS));
	}

	return -ETIMEDOUT;
}

int uvc_transport_send_frame(struct uvc_transport_ctx *ctx, const uint8_t *jpeg, size_t jpeg_len)
{
	const struct device *uvc_dev;
	struct video_buffer *vbuf;
	uint8_t slot = UINT8_MAX;
	int ret;

	if ((ctx == NULL) || (ctx->uvc_dev == NULL) || (jpeg == NULL) || (jpeg_len == 0U)) {
		return -EINVAL;
	}

	if (jpeg_len > UINT32_MAX) {
		return -EOVERFLOW;
	}

	if ((ctx->max_frame_size == 0U) || (jpeg_len > (size_t)ctx->max_frame_size)) {
		return -ENOMEM;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);

	uvc_dev = ctx->uvc_dev;
	ret = uvc_transport_stream_format_ready(uvc_dev);
	if (ret != 0) {
		k_mutex_unlock(&ctx->lock);
		return (ret == -EAGAIN) ? -ENOTCONN : ret;
	}

	uvc_transport_reclaim_done(uvc_dev, ctx);

	for (size_t i = 0; i < UVC_TX_BUF_COUNT; i++) {
		uint8_t candidate = (ctx->tx_idx + i) % UVC_TX_BUF_COUNT;

		if (!ctx->tx_busy[candidate]) {
			slot = candidate;
			break;
		}
	}

	if (slot == UINT8_MAX) {
		k_mutex_unlock(&ctx->lock);
		return -EAGAIN;
	}

	vbuf = ctx->tx_vbuf[slot];
	if (vbuf == NULL) {
		k_mutex_unlock(&ctx->lock);
		return -ENOMEM;
	}

	memcpy(vbuf->buffer, jpeg, jpeg_len);
	(void)sys_cache_data_flush_range(vbuf->buffer, jpeg_len);

	vbuf->type = VIDEO_BUF_TYPE_INPUT;
	vbuf->bytesused = (uint32_t)jpeg_len;
	vbuf->line_offset = 0U;

	ret = video_enqueue(uvc_dev, vbuf);
	if (ret != 0) {
		k_mutex_unlock(&ctx->lock);
		return ret;
	}

	ctx->tx_busy[slot] = true;
	ctx->tx_idx = (slot + 1U) % UVC_TX_BUF_COUNT;
	k_mutex_unlock(&ctx->lock);
	return 0;
}

int uvc_transport_shutdown(struct uvc_transport_ctx *ctx)
{
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	uvc_transport_ensure_lock(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);

	if (!ctx->initialized || (ctx->usbd_ctx == NULL)) {
		uvc_transport_reset_locked(ctx);
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	if (ctx->uvc_dev != NULL) {
		struct video_buffer *done = NULL;

		(void)video_stream_stop(ctx->uvc_dev, VIDEO_BUF_TYPE_INPUT);

		for (size_t i = 0; i < UVC_TX_BUF_COUNT; i++) {
			if ((video_dequeue(ctx->uvc_dev, &done, K_NO_WAIT) != 0) || (done == NULL)) {
				break;
			}
		}
	}

	ret = ctx->usbd_enabled ? usbd_disable(ctx->usbd_ctx) : 0;
	if (usbd_shutdown(ctx->usbd_ctx) != 0 && ret == 0) {
		ret = -EIO;
	}
	(void)uvc_device_shutdown(ctx->uvc_dev);
	uvc_transport_release_buffers_locked(ctx);
	ctx->usbd_ctx = NULL;
	ctx->uvc_dev = NULL;
	ctx->max_frame_size = 0U;
	ctx->tx_idx = 0U;
	ctx->usbd_enabled = false;
	ctx->initialized = false;
	k_mutex_unlock(&ctx->lock);
	return ret;
}
