/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PERSON_DETECTION_MIPI_STREAM_CDC_TRANSPORT_H_
#define PERSON_DETECTION_MIPI_STREAM_CDC_TRANSPORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define STREAM_CDC_JPEG_TAG "JPEG"
#define STREAM_CDC_JPEG_TAG_LEN 4U
#define STREAM_CDC_JPEG_COL_SIZE 416U
#define STREAM_CDC_JPEG_ROW_SIZE 82U
#define STREAM_CDC_JPEG_PAYLOAD_SIZE \
	((size_t)STREAM_CDC_JPEG_COL_SIZE * (size_t)STREAM_CDC_JPEG_ROW_SIZE)

struct stream_cdc_transport_ctx {
	const struct device *cdc_dev;
	struct k_sem tx_done_sem;
	const uint8_t *tx_ptr;
	size_t tx_remaining;
	atomic_t stop_requested;
	bool initialized;
};

int stream_cdc_transport_init(struct stream_cdc_transport_ctx *ctx);
void stream_cdc_transport_begin_stream(struct stream_cdc_transport_ctx *ctx);
void stream_cdc_transport_request_stop(struct stream_cdc_transport_ctx *ctx);
bool stream_cdc_transport_is_connected(struct stream_cdc_transport_ctx *ctx);
int stream_cdc_transport_send_jpeg(struct stream_cdc_transport_ctx *ctx,
				   const uint8_t *jpeg, size_t jpeg_len);

#endif /* PERSON_DETECTION_MIPI_STREAM_CDC_TRANSPORT_H_ */
