/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SAMPLES_DRIVERS_VIDEO_MIPI_CAPTURE_TO_ENC_USB_UVC_TRANSPORT_H_
#define ZEPHYR_SAMPLES_DRIVERS_VIDEO_MIPI_CAPTURE_TO_ENC_USB_UVC_TRANSPORT_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>

struct usbd_context;

struct uvc_transport_ctx {
	struct usbd_context *usbd_ctx;
	const struct device *uvc_dev;
	uint32_t max_frame_size;
	struct video_buffer *tx_vbuf[2];
	bool tx_busy[2];
	uint8_t tx_idx;
	bool usbd_enabled;
	bool initialized;
	bool lock_initialized;
	struct k_mutex lock;
};

int uvc_transport_init(struct uvc_transport_ctx *ctx, const struct device *video_ctrl_dev,
		       uint32_t width, uint32_t height, uint32_t max_frame_size);
int uvc_transport_wait_stream_ready(struct uvc_transport_ctx *ctx, k_timeout_t timeout);
int uvc_transport_send_frame(struct uvc_transport_ctx *ctx, const uint8_t *jpeg, size_t jpeg_len);
int uvc_transport_shutdown(struct uvc_transport_ctx *ctx);

#endif /* ZEPHYR_SAMPLES_DRIVERS_VIDEO_MIPI_CAPTURE_TO_ENC_USB_UVC_TRANSPORT_H_ */
