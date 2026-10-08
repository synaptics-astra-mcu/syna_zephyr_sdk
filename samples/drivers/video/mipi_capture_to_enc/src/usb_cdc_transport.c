/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/usb/usbd.h>

#include <sample_usbd.h>

#include "usb_cdc_transport.h"

/* Simple framing for host-side parsing. */
#define CDC_STREAM_MAGIC UINT32_C(0x51425553) /* "QBUS" */
#define CDC_STREAM_VERSION 1U
#define CDC_STREAM_TX_TIMEOUT K_SECONDS(10)

#define CDC_STREAM_HDR_SIZE 16U

BUILD_ASSERT(CDC_STREAM_HDR_SIZE == (4U + 2U + 1U + 1U + 4U + 4U), "cdc header size mismatch");

static void cdc_irq_handler(const struct device *dev, void *user_data)
{
	struct cdc_transport_ctx *ctx = user_data;

	if ((dev == NULL) || (ctx == NULL)) {
		return;
	}

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_tx_ready(dev)) {
			if (ctx->tx_remaining == 0U) {
				uart_irq_tx_disable(dev);
				k_sem_give(&ctx->tx_done_sem);
				continue;
			}

			int wrote = uart_fifo_fill(dev, ctx->tx_ptr, ctx->tx_remaining);

			if (wrote > 0) {
				ctx->tx_ptr += (size_t)wrote;
				ctx->tx_remaining -= (size_t)wrote;
			} else {
				/* Nothing accepted right now; exit ISR loop. */
				break;
			}
		} else {
			/* No TX work; exit ISR loop. */
			break;
		}
	}
}

static int cdc_send_blocking(struct cdc_transport_ctx *ctx, const uint8_t *data, size_t len)
{
	unsigned int key;

	if ((ctx == NULL) || (ctx->cdc_dev == NULL) || (data == NULL) || (len == 0U)) {
		return -EINVAL;
	}

	/* Ensure the ISR cannot observe partially-updated tx_* state. */
	key = irq_lock();
	uart_irq_tx_disable(ctx->cdc_dev);

	ctx->tx_ptr = data;
	ctx->tx_remaining = len;
	irq_unlock(key);

	k_sem_reset(&ctx->tx_done_sem);

	/* Start TX; ISR will drain the buffer and signal completion. */
	uart_irq_tx_enable(ctx->cdc_dev);
	int ret = k_sem_take(&ctx->tx_done_sem, CDC_STREAM_TX_TIMEOUT);
	if (ret != 0) {
		uart_irq_tx_disable(ctx->cdc_dev);
		ctx->tx_ptr = NULL;
		ctx->tx_remaining = 0U;
	}
	return ret;
}

int cdc_transport_init(struct cdc_transport_ctx *ctx)
{
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	if (ctx->usbd_ctx != NULL) {
		return 0;
	}

	k_sem_init(&ctx->tx_done_sem, 0, 1);

	/*
	 * Use a polling-based DTR wait to avoid global state in the USBD message
	 * callback path (sample_usbd_init_device() does not provide a user_data).
	 */
	ctx->usbd_ctx = sample_usbd_init_device(NULL);
	if (ctx->usbd_ctx == NULL) {
		return -ENODEV;
	}

	/* If VBUS detection is not supported, enable right away. */
	if (!usbd_can_detect_vbus(ctx->usbd_ctx)) {
		ret = usbd_enable(ctx->usbd_ctx);
		if (ret != 0) {
			(void)usbd_shutdown(ctx->usbd_ctx);
			ctx->usbd_ctx = NULL;
			return ret;
		}
	}

	ctx->cdc_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
	if (!device_is_ready(ctx->cdc_dev)) {
		return -ENODEV;
	}

	ret = uart_irq_callback_user_data_set(ctx->cdc_dev, cdc_irq_handler, ctx);
	if (ret != 0) {
		return ret;
	}
	uart_irq_tx_disable(ctx->cdc_dev);

	return 0;
}

int cdc_transport_wait_dtr(struct cdc_transport_ctx *ctx, k_timeout_t timeout)
{
	uint32_t dtr = 0U;

	if ((ctx == NULL) || (ctx->cdc_dev == NULL)) {
		return -ENODEV;
	}

	if (uart_line_ctrl_get(ctx->cdc_dev, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr != 0U) {
		return 0;
	}

	if (timeout.ticks == K_NO_WAIT.ticks) {
		return -EAGAIN;
	}

	if (timeout.ticks == K_FOREVER.ticks) {
		while (true) {
			k_sleep(K_MSEC(20));
			if (uart_line_ctrl_get(ctx->cdc_dev, UART_LINE_CTRL_DTR, &dtr) == 0 &&
			    dtr != 0U) {
				return 0;
			}
		}
	}

	int64_t deadline_ms = (int64_t)k_uptime_get() + (int64_t)k_ticks_to_ms_floor64(timeout.ticks);

	while ((int64_t)k_uptime_get() < deadline_ms) {
		k_sleep(K_MSEC(20));
		if (uart_line_ctrl_get(ctx->cdc_dev, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr != 0U) {
			return 0;
		}
	}

	return -EAGAIN;
}

int cdc_transport_send_jpeg(struct cdc_transport_ctx *ctx, uint16_t image_id,
			    const uint8_t *jpeg, size_t jpeg_len)
{
	uint8_t hdr[CDC_STREAM_HDR_SIZE];
	uint32_t dtr = 0U;
	int ret;

	if ((ctx == NULL) || (jpeg == NULL) || (jpeg_len == 0U)) {
		return -EINVAL;
	}

	if (image_id > UINT8_MAX) {
		return -EINVAL;
	}

	if (jpeg_len > UINT32_MAX) {
		return -EOVERFLOW;
	}

	/* Avoid long TX timeouts when the host hasn't asserted DTR. */
	if ((ctx->cdc_dev == NULL) ||
	    (uart_line_ctrl_get(ctx->cdc_dev, UART_LINE_CTRL_DTR, &dtr) != 0) || (dtr == 0U)) {
		return -ENOTCONN;
	}

	memset(hdr, 0, sizeof(hdr));
	sys_put_le32(CDC_STREAM_MAGIC, &hdr[0]);
	sys_put_le16((uint16_t)CDC_STREAM_VERSION, &hdr[4]);
	hdr[6] = (uint8_t)image_id;
	hdr[7] = 0U; /* flags */
	sys_put_le32((uint32_t)jpeg_len, &hdr[8]);
	sys_put_le32(crc32_ieee(jpeg, jpeg_len), &hdr[12]);

	ret = cdc_send_blocking(ctx, hdr, sizeof(hdr));
	if (ret != 0) {
		return ret;
	}

	return cdc_send_blocking(ctx, jpeg, jpeg_len);
}
