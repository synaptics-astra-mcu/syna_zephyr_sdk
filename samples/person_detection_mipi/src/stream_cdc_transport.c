/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "stream_cdc_transport.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/sys/byteorder.h>

#define CDC_STREAM_TX_TIMEOUT K_SECONDS(10)
#define CDC_STREAM_TX_POLL_INTERVAL K_MSEC(50)

static void stream_cdc_irq_handler(const struct device *dev, void *user_data)
{
	struct stream_cdc_transport_ctx *ctx = user_data;

	if ((dev == NULL) || (ctx == NULL)) {
		return;
	}

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (atomic_get(&ctx->stop_requested) != 0) {
			uart_irq_tx_disable(dev);
			ctx->tx_ptr = NULL;
			ctx->tx_remaining = 0U;
			k_sem_give(&ctx->tx_done_sem);
			break;
		}

		if (!uart_irq_tx_ready(dev)) {
			break;
		}

		if (ctx->tx_remaining == 0U) {
			if (uart_irq_tx_complete(dev) != 0) {
				uart_irq_tx_disable(dev);
				k_sem_give(&ctx->tx_done_sem);
			}
			break;
		}

		int wrote = uart_fifo_fill(dev, ctx->tx_ptr, ctx->tx_remaining);

		if (wrote > 0) {
			ctx->tx_ptr += (size_t)wrote;
			ctx->tx_remaining -= (size_t)wrote;
		} else {
			break;
		}
	}
}

static int stream_cdc_send_blocking(struct stream_cdc_transport_ctx *ctx,
				    const uint8_t *data, size_t len)
{
	unsigned int key;
	int ret;

	if ((ctx == NULL) || (ctx->cdc_dev == NULL) || (data == NULL) || (len == 0U)) {
		return -EINVAL;
	}

	key = irq_lock();
	uart_irq_tx_disable(ctx->cdc_dev);
	ctx->tx_ptr = data;
	ctx->tx_remaining = len;
	irq_unlock(key);

	k_sem_reset(&ctx->tx_done_sem);
	uart_irq_tx_enable(ctx->cdc_dev);

	for (int64_t remaining_ms = k_ticks_to_ms_floor64(CDC_STREAM_TX_TIMEOUT.ticks);
	     remaining_ms > 0; remaining_ms -= 50) {
		ret = k_sem_take(&ctx->tx_done_sem, CDC_STREAM_TX_POLL_INTERVAL);
		if (ret == 0) {
			return 0;
		}

		if (atomic_get(&ctx->stop_requested) != 0) {
			uart_irq_tx_disable(ctx->cdc_dev);
			ctx->tx_ptr = NULL;
			ctx->tx_remaining = 0U;
			return -ECANCELED;
		}

		if (!stream_cdc_transport_is_connected(ctx)) {
			uart_irq_tx_disable(ctx->cdc_dev);
			ctx->tx_ptr = NULL;
			ctx->tx_remaining = 0U;
			return -ENOTCONN;
		}
	}

	uart_irq_tx_disable(ctx->cdc_dev);
	ctx->tx_ptr = NULL;
	ctx->tx_remaining = 0U;
	return -EAGAIN;
}

int stream_cdc_transport_init(struct stream_cdc_transport_ctx *ctx)
{
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	if (ctx->initialized) {
		return 0;
	}

	memset(ctx, 0, sizeof(*ctx));
	k_sem_init(&ctx->tx_done_sem, 0, 1);
	atomic_clear(&ctx->stop_requested);

	ctx->cdc_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart1));
	if (!device_is_ready(ctx->cdc_dev)) {
		return -ENODEV;
	}

	ret = uart_irq_callback_user_data_set(ctx->cdc_dev, stream_cdc_irq_handler, ctx);
	if (ret != 0) {
		return ret;
	}

	uart_irq_tx_disable(ctx->cdc_dev);
	(void)uart_line_ctrl_set(ctx->cdc_dev, UART_LINE_CTRL_DCD, 1);
	(void)uart_line_ctrl_set(ctx->cdc_dev, UART_LINE_CTRL_DSR, 1);
	ctx->initialized = true;
	return 0;
}

void stream_cdc_transport_begin_stream(struct stream_cdc_transport_ctx *ctx)
{
	if (ctx == NULL) {
		return;
	}

	atomic_clear(&ctx->stop_requested);
}

void stream_cdc_transport_request_stop(struct stream_cdc_transport_ctx *ctx)
{
	if ((ctx == NULL) || (ctx->cdc_dev == NULL)) {
		return;
	}

	atomic_set(&ctx->stop_requested, 1);
	uart_irq_tx_disable(ctx->cdc_dev);
	ctx->tx_ptr = NULL;
	ctx->tx_remaining = 0U;
	k_sem_give(&ctx->tx_done_sem);
}

bool stream_cdc_transport_is_connected(struct stream_cdc_transport_ctx *ctx)
{
	uint32_t dtr = 0U;

	if ((ctx == NULL) || (ctx->cdc_dev == NULL)) {
		return false;
	}

	return (uart_line_ctrl_get(ctx->cdc_dev, UART_LINE_CTRL_DTR, &dtr) == 0) && (dtr != 0U);
}

int stream_cdc_transport_send_jpeg(struct stream_cdc_transport_ctx *ctx,
				   const uint8_t *jpeg, size_t jpeg_len)
{
	uint8_t hdr[STREAM_CDC_JPEG_TAG_LEN + 4U];
	int ret;

	if ((ctx == NULL) || (jpeg == NULL) || (jpeg_len == 0U)) {
		return -EINVAL;
	}

	if (atomic_get(&ctx->stop_requested) != 0) {
		return -ECANCELED;
	}

	if (!stream_cdc_transport_is_connected(ctx)) {
		return -ENOTCONN;
	}

	if (jpeg_len != STREAM_CDC_JPEG_PAYLOAD_SIZE) {
		return -EINVAL;
	}

	memset(hdr, 0, sizeof(hdr));
	memcpy(hdr, STREAM_CDC_JPEG_TAG, STREAM_CDC_JPEG_TAG_LEN);
	sys_put_be16(STREAM_CDC_JPEG_COL_SIZE, &hdr[STREAM_CDC_JPEG_TAG_LEN]);
	sys_put_be16(STREAM_CDC_JPEG_ROW_SIZE, &hdr[STREAM_CDC_JPEG_TAG_LEN + 2U]);

	ret = stream_cdc_send_blocking(ctx, hdr, sizeof(hdr));
	if (ret != 0) {
		return ret;
	}

	return stream_cdc_send_blocking(ctx, jpeg, jpeg_len);
}
