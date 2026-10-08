/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SAMPLES_DRIVERS_VIDEO_MIPI_CAPTURE_TO_ENC_USB_CDC_TRANSPORT_H_
#define ZEPHYR_SAMPLES_DRIVERS_VIDEO_MIPI_CAPTURE_TO_ENC_USB_CDC_TRANSPORT_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

struct usbd_context;

struct cdc_transport_ctx {
	struct usbd_context *usbd_ctx;
	const struct device *cdc_dev;
	struct k_sem tx_done_sem;
	const uint8_t *tx_ptr;
	size_t tx_remaining;
};

int cdc_transport_init(struct cdc_transport_ctx *ctx);
int cdc_transport_wait_dtr(struct cdc_transport_ctx *ctx, k_timeout_t timeout);
int cdc_transport_send_jpeg(struct cdc_transport_ctx *ctx, uint16_t image_id,
			    const uint8_t *jpeg, size_t jpeg_len);

#endif /* ZEPHYR_SAMPLES_DRIVERS_VIDEO_MIPI_CAPTURE_TO_ENC_USB_CDC_TRANSPORT_H_ */
