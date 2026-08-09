/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB CDC stack service implementation.
 *
 * @file usb_cdc_stack_service.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/bos.h>
#include <zephyr/usb/usbd.h>

#include <usb_cdc_stack_service.h>

/* Per-interface CDC software ring-buffer size in bytes. */
#define USB_CDC_RING_BUF_SIZE 1024

/* Compile-time guard: enable stack service only when both CDC UART DT nodes exist. */
#define SYNA_USB_CDC_STACK_HAS_DT \
	(DT_NODE_EXISTS(DT_NODELABEL(cdc_acm_uart0)) && \
	 DT_NODE_EXISTS(DT_NODELABEL(cdc_acm_uart1)))

#if SYNA_USB_CDC_STACK_HAS_DT

/* Keep DFU class out of this composite sample. */
static const char *const cdc_blocklist[] = {
	"dfu_dfu",
	NULL,
};

USBD_DEVICE_DEFINE(syna_cdc_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   CONFIG_SAMPLE_USBD_VID, CONFIG_SAMPLE_USBD_PID);

USBD_DESC_LANG_DEFINE(syna_cdc_lang);
USBD_DESC_MANUFACTURER_DEFINE(syna_cdc_mfr, CONFIG_SAMPLE_USBD_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(syna_cdc_product, CONFIG_SAMPLE_USBD_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(syna_cdc_sn)));

USBD_DESC_CONFIG_DEFINE(syna_cdc_fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(syna_cdc_hs_cfg_desc, "HS Configuration");

static const uint8_t cdc_usb_attributes =
	(IS_ENABLED(CONFIG_SAMPLE_USBD_SELF_POWERED) ? USB_SCD_SELF_POWERED : 0) |
	(IS_ENABLED(CONFIG_SAMPLE_USBD_REMOTE_WAKEUP) ? USB_SCD_REMOTE_WAKEUP : 0);

USBD_CONFIGURATION_DEFINE(syna_cdc_fs_config,
			      cdc_usb_attributes,
			      CONFIG_SAMPLE_USBD_MAX_POWER,
			      &syna_cdc_fs_cfg_desc);

USBD_CONFIGURATION_DEFINE(syna_cdc_hs_config,
			      cdc_usb_attributes,
			      CONFIG_SAMPLE_USBD_MAX_POWER,
			      &syna_cdc_hs_cfg_desc);

struct usb_cdc_stack_ctx {
	uint8_t cdc0_ring_buf_data[USB_CDC_RING_BUF_SIZE]; // Backing storage for CDC ACM #0 ring buffer
	uint8_t cdc1_ring_buf_data[USB_CDC_RING_BUF_SIZE]; // Backing storage for CDC ACM #1 ring buffer
	struct ring_buf cdc0_ringbuf;                      // Software RX/TX ring for CDC ACM #0
	struct ring_buf cdc1_ringbuf;                      // Software RX/TX ring for CDC ACM #1
	const struct device *cdc0_dev;                     // Device handle for CDC ACM #0 UART endpoint
	const struct device *cdc1_dev;                     // Device handle for CDC ACM #1 UART endpoint
	struct usbd_context *usbd_ctx;                     // Stack-owned USB device context
	bool initialized;                                  // True after one-time service initialization succeeds
};

/* Singleton runtime state for the stack-owned CDC service. */
static struct usb_cdc_stack_ctx g_ctx;

/* USBD event callback registered during CDC service initialization. */
static void usbd_msg_cb(struct usbd_context *const usbd_ctx,
			const struct usbd_msg *const msg); // USBD event payload from controller stack

/**
 * \brief   Force interface-class code triple for composite USB classes.
 */
static void cdc_fix_code_triple(struct usbd_context *uds_ctx, enum usbd_speed speed)
{
	if (IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_CDC_ECM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_CDC_NCM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_MIDI2_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_AUDIO2_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_VIDEO_CLASS)) {
		usbd_device_set_code_triple(uds_ctx, speed,
					    USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	} else {
		usbd_device_set_code_triple(uds_ctx, speed, 0, 0, 0);
	}
} /* cdc_fix_code_triple */

/**
 * \brief   Setup and initialize stack-owned USBD device context.
 */
static int cdc_setup_usbd_device(struct usbd_context **ctx)
{
	int ret; // Return code from descriptor/config/class registration calls

	ret = usbd_add_descriptor(&syna_cdc_usbd, &syna_cdc_lang);
	if (ret != 0) {
		return ret;
	}

	ret = usbd_add_descriptor(&syna_cdc_usbd, &syna_cdc_mfr);
	if (ret != 0) {
		return ret;
	}

	ret = usbd_add_descriptor(&syna_cdc_usbd, &syna_cdc_product);
	if (ret != 0) {
		return ret;
	}

	IF_ENABLED(CONFIG_HWINFO, (
		ret = usbd_add_descriptor(&syna_cdc_usbd, &syna_cdc_sn);
	));
	if (ret != 0) {
		return ret;
	}

	if (USBD_SUPPORTS_HIGH_SPEED && usbd_caps_speed(&syna_cdc_usbd) == USBD_SPEED_HS) {
		ret = usbd_add_configuration(&syna_cdc_usbd, USBD_SPEED_HS,
					     &syna_cdc_hs_config);
		if (ret != 0) {
			return ret;
		}

		ret = usbd_register_all_classes(&syna_cdc_usbd, USBD_SPEED_HS, 1, cdc_blocklist);
		if (ret != 0) {
			return ret;
		}

		cdc_fix_code_triple(&syna_cdc_usbd, USBD_SPEED_HS);
	}

	ret = usbd_add_configuration(&syna_cdc_usbd, USBD_SPEED_FS, &syna_cdc_fs_config);
	if (ret != 0) {
		return ret;
	}

	ret = usbd_register_all_classes(&syna_cdc_usbd, USBD_SPEED_FS, 1, cdc_blocklist);
	if (ret != 0) {
		return ret;
	}

	cdc_fix_code_triple(&syna_cdc_usbd, USBD_SPEED_FS);
	usbd_self_powered(&syna_cdc_usbd, cdc_usb_attributes & USB_SCD_SELF_POWERED);

	ret = usbd_msg_register_cb(&syna_cdc_usbd, usbd_msg_cb);
	if (ret != 0) {
		return ret;
	}

	ret = usbd_init(&syna_cdc_usbd);
	if (ret != 0) {
		return ret;
	}

	*ctx = &syna_cdc_usbd;
	return 0;
} /* cdc_setup_usbd_device */

/**
 * \brief   Handle USBD events for stack-owned CDC service.
 */
static void usbd_msg_cb(struct usbd_context *const usbd_ctx,
			const struct usbd_msg *const msg)
{
	printk("[USB] USBD message: %s\n", usbd_msg_type_string(msg->type));

	if (usbd_can_detect_vbus(usbd_ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(usbd_ctx) != 0) {
				printk("[USB] Failed to enable device support\n");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(usbd_ctx) != 0) {
				printk("[USB] Failed to disable device support\n");
			}
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U; // Host Data Terminal Ready line state

		uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr != 0U) {
			printk("[USB] CDC ACM DTR set\n");
		}
	}
} /* usbd_msg_cb */

/**
 * \brief   CDC ACM UART interrupt handler for RX/TX processing.
 */
static void cdc_interrupt_handler(const struct device *dev, void *user_data)
{
	struct ring_buf *rb = (struct ring_buf *)user_data;

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[64]; // Temporary RX FIFO drain buffer
			int recv_len = uart_fifo_read(dev, buf, sizeof(buf));

			if (recv_len > 0) {
				ring_buf_put(rb, buf, recv_len);
				uart_irq_tx_enable(dev);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buf[64]; // Temporary TX staging buffer from software ring
			int rb_len = ring_buf_get(rb, buf, sizeof(buf));

			if (rb_len == 0) {
				uart_irq_tx_disable(dev);
				continue;
			}

			uart_fifo_fill(dev, buf, rb_len);
		}
	}
} /* cdc_interrupt_handler */

/**
 * \brief   Initialize stack-owned USB CDC ACM service.
 */
int usb_cdc_stack_service_init(void)
{
	int ret;         // Return code from USB/UART setup calls
	uint32_t dtr = 0U; // Host Data Terminal Ready line state

	if (g_ctx.initialized) {
		return 0;
	}

	g_ctx.cdc0_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
	if (!device_is_ready(g_ctx.cdc0_dev)) {
		printk("[USB] CDC ACM 0 device not ready\n");
		return -ENODEV;
	}

	g_ctx.cdc1_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart1));
	if (!device_is_ready(g_ctx.cdc1_dev)) {
		printk("[USB] CDC ACM 1 device not ready\n");
		return -ENODEV;
	}

	ring_buf_init(&g_ctx.cdc0_ringbuf, sizeof(g_ctx.cdc0_ring_buf_data), g_ctx.cdc0_ring_buf_data);
	ring_buf_init(&g_ctx.cdc1_ringbuf, sizeof(g_ctx.cdc1_ring_buf_data), g_ctx.cdc1_ring_buf_data);

	ret = cdc_setup_usbd_device(&g_ctx.usbd_ctx);
	if (ret != 0) {
		printk("[USB] Failed to initialize USB device, %d\n", ret);
		return ret;
	}

	ret = usbd_enable(g_ctx.usbd_ctx);
	if (ret != 0 && ret != -EALREADY) {
		printk("[USB] Failed to enable device support, %d\n", ret);
		return ret;
	}

	uart_line_ctrl_set(g_ctx.cdc0_dev, UART_LINE_CTRL_DCD, 1);
	uart_line_ctrl_set(g_ctx.cdc0_dev, UART_LINE_CTRL_DSR, 1);
	uart_line_ctrl_set(g_ctx.cdc1_dev, UART_LINE_CTRL_DCD, 1);
	uart_line_ctrl_set(g_ctx.cdc1_dev, UART_LINE_CTRL_DSR, 1);

	k_msleep(100);

	uart_irq_callback_user_data_set(g_ctx.cdc0_dev, cdc_interrupt_handler, &g_ctx.cdc0_ringbuf);
	uart_irq_rx_enable(g_ctx.cdc0_dev);

	uart_irq_callback_user_data_set(g_ctx.cdc1_dev, cdc_interrupt_handler, &g_ctx.cdc1_ringbuf);
	uart_irq_rx_enable(g_ctx.cdc1_dev);

	if (uart_line_ctrl_get(g_ctx.cdc0_dev, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr != 0U) {
		printk("[USB] CDC ACM 0 DTR set\n");
	} else {
		printk("[USB] CDC ACM initialized; waiting for host DTR on CDC ACM 0\n");
	}

	g_ctx.initialized = true;
	printk("[USB] CDC stack interfaces initialized (CDC ACM 0/1)\n");
	return 0;
} /* usb_cdc_stack_service_init */

#else

int usb_cdc_stack_service_init(void)
{
	return -ENOTSUP;
} /* usb_cdc_stack_service_init */

#endif
