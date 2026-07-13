/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();

enum kb_leds_idx {
	KB_LED_NUMLOCK = 0,
	KB_LED_CAPSLOCK,
	KB_LED_SCROLLLOCK,
	KB_LED_COUNT,
};

static const struct gpio_dt_spec kb_leds[KB_LED_COUNT] = {
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0}),
};

enum kb_report_idx {
	KB_MOD_KEY = 0,
	KB_RESERVED,
	KB_KEY_CODE1,
	KB_KEY_CODE2,
	KB_KEY_CODE3,
	KB_KEY_CODE4,
	KB_KEY_CODE5,
	KB_KEY_CODE6,
	KB_REPORT_COUNT,
};

struct kb_event {
	uint16_t code;
	int32_t value;
};

K_MSGQ_DEFINE(kb_msgq, sizeof(struct kb_event), 4, 1);

UDC_STATIC_BUF_DEFINE(report, KB_REPORT_COUNT);
static uint32_t kb_duration;
static bool kb_ready;

static void input_cb(struct input_event *evt, void *user_data)
{
	struct kb_event kb_evt;

	ARG_UNUSED(user_data);

	kb_evt.code = evt->code;
	kb_evt.value = evt->value;
	if (k_msgq_put(&kb_msgq, &kb_evt, K_NO_WAIT) != 0) {
		LOG_ERR("Failed to put new input event");
	}
}

INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);

static void kb_iface_ready(const struct device *dev, const bool ready)
{
	LOG_INF("HID device %s interface is %s",
		dev->name, ready ? "ready" : "not ready");
	kb_ready = ready;
}

static int kb_get_report(const struct device *dev,
			 const uint8_t type, const uint8_t id, const uint16_t len,
			 uint8_t *const buf)
{
	LOG_WRN("Get Report not implemented, Type %u ID %u", type, id);

	return 0;
}

static int kb_set_report(const struct device *dev,
			 const uint8_t type, const uint8_t id, const uint16_t len,
			 const uint8_t *const buf)
{
	if (type != HID_REPORT_TYPE_OUTPUT) {
		LOG_WRN("Unsupported report type");
		return -ENOTSUP;
	}

	if (buf == NULL || len == 0) {
		LOG_ERR("Invalid parameters for set_report");
		return -EINVAL;
	}

	for (unsigned int i = 0; i < ARRAY_SIZE(kb_leds); i++) {
		if (kb_leds[i].port == NULL) {
			continue;
		}

		(void)gpio_pin_set_dt(&kb_leds[i], buf[0] & BIT(i));
	}

	return 0;
}

/* Idle duration is stored but not used to calculate idle reports. */
static void kb_set_idle(const struct device *dev,
			const uint8_t id, const uint32_t duration)
{
	LOG_INF("Set Idle %u to %u", id, duration);
	kb_duration = duration;
}

static uint32_t kb_get_idle(const struct device *dev, const uint8_t id)
{
	LOG_INF("Get Idle %u to %u", id, kb_duration);
	return kb_duration;
}

static void kb_set_protocol(const struct device *dev, const uint8_t proto)
{
	LOG_INF("Protocol changed to %s",
		proto == 0U ? "Boot Protocol" : "Report Protocol");
}

static void kb_output_report(const struct device *dev, const uint16_t len,
			     const uint8_t *const buf)
{
	LOG_HEXDUMP_DBG(buf, len, "o.r.");
	kb_set_report(dev, HID_REPORT_TYPE_OUTPUT, 0U, len, buf);
}

struct hid_device_ops kb_ops = {
	.iface_ready = kb_iface_ready,
	.get_report = kb_get_report,
	.set_report = kb_set_report,
	.set_idle = kb_set_idle,
	.get_idle = kb_get_idle,
	.set_protocol = kb_set_protocol,
	.output_report = kb_output_report,
};

#define RING_BUF_SIZE 1024
uint8_t ring_buffer[RING_BUF_SIZE];

struct ring_buf ringbuf;

static bool rx_throttled;

K_SEM_DEFINE(dtr_sem, 0, 1);

static inline void print_baudrate(const struct device *dev)
{
	uint32_t baudrate;
	int ret;

	ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
	if (ret) {
		LOG_WRN("Failed to get baudrate, ret code %d", ret);
	} else {
		LOG_INF("Baudrate %u", baudrate);
	}
}

static void msg_cb(struct usbd_context *const usbd_ctx,
		   const struct usbd_msg *const msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (msg->type == USBD_MSG_CONFIGURATION) {
		LOG_INF("\tConfiguration value %d", msg->status);
	}

	if (usbd_can_detect_vbus(usbd_ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(usbd_ctx)) {
				LOG_ERR("Failed to enable device support");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(usbd_ctx)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;

		uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			k_sem_give(&dtr_sem);
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_LINE_CODING) {
		print_baudrate(msg->dev);
	}
}

static void interrupt_handler(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (!rx_throttled && uart_irq_rx_ready(dev)) {
			int recv_len, rb_len;
			uint8_t buffer[64];
			size_t len = MIN(ring_buf_space_get(&ringbuf),
					 sizeof(buffer));

			if (len == 0) {
				/* Throttle because ring buffer is full */
				uart_irq_rx_disable(dev);
				rx_throttled = true;
				continue;
			}

			recv_len = uart_fifo_read(dev, buffer, len);
			if (recv_len < 0) {
				LOG_ERR("Failed to read UART FIFO");
				recv_len = 0;
			};

			rb_len = ring_buf_put(&ringbuf, buffer, recv_len);
			if (rb_len < recv_len) {
				LOG_ERR("Drop %u bytes", recv_len - rb_len);
			}

			LOG_DBG("tty fifo -> ringbuf %d bytes", rb_len);
			if (rb_len) {
				uart_irq_tx_enable(dev);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buffer[64];
			int rb_len, send_len;

			rb_len = ring_buf_get(&ringbuf, buffer, sizeof(buffer));
			if (!rb_len) {
				LOG_DBG("Ring buffer empty, disable TX IRQ");
				uart_irq_tx_disable(dev);
				continue;
			}

			if (rx_throttled) {
				uart_irq_rx_enable(dev);
				rx_throttled = false;
			}

			send_len = uart_fifo_fill(dev, buffer, rb_len);
			if (send_len < rb_len) {
				LOG_ERR("Drop %d bytes", rb_len - send_len);
			}

			LOG_DBG("ringbuf -> tty fifo %d bytes", send_len);
		}
	}
}

int main(void)
{
	struct usbd_context *sample_usbd;
	const struct device *hid_dev, *cdc0_dev, *cdc1_dev;
	int ret;

	for (unsigned int i = 0; i < ARRAY_SIZE(kb_leds); i++) {
		if (kb_leds[i].port == NULL) {
			continue;
		}

		if (!gpio_is_ready_dt(&kb_leds[i])) {
			LOG_ERR("LED device %s is not ready", kb_leds[i].port->name);
			return -EIO;
		}

		ret = gpio_pin_configure_dt(&kb_leds[i], GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("Failed to configure the LED pin, %d", ret);
			return -EIO;
		}
	}

	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID Device is not ready");
		return -EIO;
	}

	cdc0_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
	if (!device_is_ready(cdc0_dev)) {
		LOG_ERR("CDC Device 0 is not ready");
		return -EIO;
	}

	cdc1_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart1));
	if (!device_is_ready(cdc1_dev)) {
		LOG_ERR("CDC Device 1 is not ready");
		return -EIO;
	}

	ret = hid_device_register(hid_dev,
				  hid_report_desc, sizeof(hid_report_desc),
				  &kb_ops);
	if (ret != 0) {
		LOG_ERR("Failed to register HID Device, %d", ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_USBD_HID_SET_POLLING_PERIOD)) {
		ret = hid_device_set_in_polling(hid_dev, 1000);
		if (ret) {
			LOG_WRN("Failed to set IN report polling period, %d", ret);
		}

		ret = hid_device_set_out_polling(hid_dev, 1000);
		if (ret != 0 && ret != -ENOTSUP) {
			LOG_WRN("Failed to set OUT report polling period, %d", ret);
		}
	}

	sample_usbd = sample_usbd_init_device(msg_cb);
	if (sample_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	if (!usbd_can_detect_vbus(sample_usbd)) {
		ret = usbd_enable(sample_usbd);
		if (ret) {
			LOG_ERR("Failed to enable device support");
			return ret;
		}
	}

	LOG_INF("HID keyboard sample is initialized");

	ring_buf_init(&ringbuf, sizeof(ring_buffer), ring_buffer);

	LOG_INF("Wait for DTR");
	k_sem_take(&dtr_sem, K_FOREVER);
	LOG_INF("DTR set");

	/* Wait 100ms for the host to do all settings */
	k_msleep(100);

	uart_irq_callback_set(cdc0_dev, interrupt_handler);
	/* Enable rx interrupts */
	uart_irq_rx_enable(cdc0_dev);

	uart_irq_callback_set(cdc1_dev, interrupt_handler);
	/* Enable rx interrupts */
	uart_irq_rx_enable(cdc1_dev);

	while (true) {
		struct kb_event kb_evt;

		k_msgq_get(&kb_msgq, &kb_evt, K_FOREVER);

		memset(report, 0, sizeof(report));

		switch (kb_evt.code) {
		case INPUT_KEY_0:
			if (kb_evt.value) {
				report[KB_KEY_CODE1] = HID_KEY_NUMLOCK;
			} else {
				report[KB_KEY_CODE1] = 0;
			}

			break;
		case INPUT_KEY_1:
			if (kb_evt.value) {
				report[KB_KEY_CODE2] = HID_KEY_CAPSLOCK;
			} else {
				report[KB_KEY_CODE2] = 0;
			}

			break;
		case INPUT_KEY_2:
			if (kb_evt.value) {
				report[KB_KEY_CODE3] = HID_KEY_SCROLLLOCK;
			} else {
				report[KB_KEY_CODE3] = 0;
			}

			break;
		case INPUT_KEY_3:
			if (kb_evt.value) {
				report[KB_MOD_KEY] = HID_KBD_MODIFIER_RIGHT_ALT;
				report[KB_KEY_CODE4] = HID_KEY_1;
				report[KB_KEY_CODE5] = HID_KEY_2;
				report[KB_KEY_CODE6] = HID_KEY_3;
			} else {
				report[KB_MOD_KEY] = HID_KBD_MODIFIER_NONE;
				report[KB_KEY_CODE4] = 0;
				report[KB_KEY_CODE5] = 0;
				report[KB_KEY_CODE6] = 0;
			}

			break;
		default:
			LOG_INF("Unrecognized input code %u value %d",
				kb_evt.code, kb_evt.value);
			continue;
		}

		if (!kb_ready) {
			LOG_INF("USB HID device is not ready");
			continue;
		}

		if (IS_ENABLED(CONFIG_SAMPLE_USBD_REMOTE_WAKEUP) &&
		    usbd_is_suspended(sample_usbd)) {
			/* on a press of any button, send wakeup request */
			if (kb_evt.value) {
				ret = usbd_wakeup_request(sample_usbd);
				if (ret) {
					LOG_ERR("Remote wakeup error, %d", ret);
				}
			}
			continue;
		}

		ret = hid_device_submit_report(hid_dev, KB_REPORT_COUNT, report);
		if (ret) {
			LOG_ERR("HID submit report error, %d", ret);
		}
	}

	return 0;
}
