/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID stack service implementation.
 *
 * @file usb_hid_stack_service.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usbd_hid.h>

#include <usb_hid_stack_service.h>

#include "descriptors/hid_input_devices_report_desc.h"
#include "descriptors/hid_consolidated_report_desc.h"
#include "descriptors/hid_hpd_customhpd_report_desc.h"
#include "profiles/keyboard/usb_hid_keyboard.h"
#include "profiles/mouse/usb_hid_mouse.h"
#include "profiles/pen/usb_hid_pen.h"
#include "profiles/touchpad/usb_hid_touchpad.h"
#include "profiles/touchscreen/usb_hid_touchscreen.h"
#include "profiles/custom/usb_hid_custom.h"
#include "profiles/als/usb_hid_als.h"
#include "profiles/hpd/usb_hid_hpd.h"
#include "profiles/customhpd/usb_hid_customhpd.h"

/* Compile-time guard: require all three stack-owned HID DT nodes. */
#define SYNA_USB_HID_STACK_HAS_DT \
	(DT_NODE_EXISTS(DT_NODELABEL(hid_input_dev_0)) && \
	 DT_NODE_EXISTS(DT_NODELABEL(hid_custom_als_0)) && \
	 DT_NODE_EXISTS(DT_NODELABEL(hid_hpd_0)))

#if SYNA_USB_HID_STACK_HAS_DT

LOG_MODULE_REGISTER(usb_hid_stack_service, CONFIG_LOG_DEFAULT_LEVEL);

/* DEBUG NOTES:
 * The commented LOG_* lines in this file are intentionally kept for troubleshooting.
 * Uncomment them temporarily when diagnosing HID submit/report behavior.
 */

struct hid_stack_ctx {
	const struct device *input_dev;                 // Interface #1 device (keyboard/mouse/pen/touch)
	const struct device *custom_als_dev;            // Interface #2 device (custom + ALS)
	const struct device *hpd_dev;                   // Interface #3 device (HPD + CUSTOMHPD)
	bool ready[USB_HID_STACK_IF_COUNT];             // Host readiness per interface
	bool initialized;                               // True after one-time stack registration succeeds
};

/* Singleton runtime context for stack-owned HID interfaces. */
static struct hid_stack_ctx g_ctx;

/* Report descriptor for input-devices interface (keyboard/mouse/pen/touch). */
static const uint8_t hid_input_devices_report_desc[] = { HID_INPUT_DEVICES_REPORT_DESC() };
/* Report descriptor for custom + ALS interface. */
static const uint8_t hid_custom_als_report_desc[] = { HID_CONSOLIDATED_MULTI_PROFILE_REPORT_DESC };
/* Report descriptor for HPD + CUSTOMHPD interface. */
static const uint8_t hid_hpd_customhpd_report_desc[] = { HID_HPD_CUSTOMHPD_REPORT_DESC() };

/**
 * \brief   Update readiness for input-devices HID interface.
 */
static void hid_input_iface_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);
	g_ctx.ready[USB_HID_STACK_IF_INPUT_DEVICES] = ready;
} /* hid_input_iface_ready */

/**
 * \brief   Update readiness for custom+ALS HID interface.
 */
static void hid_custom_als_iface_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);
	g_ctx.ready[USB_HID_STACK_IF_CUSTOM_ALS] = ready;
} /* hid_custom_als_iface_ready */

/**
 * \brief   Update readiness for HPD HID interface.
 */
static void hid_hpd_iface_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);
	g_ctx.ready[USB_HID_STACK_IF_HPD] = ready;
} /* hid_hpd_iface_ready */

/**
 * \brief   Handle HID get-report requests for registered interfaces.
 */
static int hid_get_report(const struct device *dev,
			      const uint8_t type, const uint8_t id,
			      const uint16_t len, uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(type);
	ARG_UNUSED(id);

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	memset(buf, 0, len);
	return len;
} /* hid_get_report */

/**
 * \brief   Handle HID set-report requests for registered interfaces.
 */
static int hid_set_report(const struct device *dev,
			      const uint8_t type, const uint8_t id,
			      const uint16_t len, const uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(type);
	ARG_UNUSED(id);

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	return 0;
} /* hid_set_report */

static struct hid_device_ops hid_input_ops = {
	.iface_ready = hid_input_iface_ready,
	.get_report = hid_get_report,
};

static struct hid_device_ops hid_custom_als_ops = {
	.iface_ready = hid_custom_als_iface_ready,
	.get_report = hid_get_report,
	.set_report = hid_set_report,
};

static struct hid_device_ops hid_hpd_ops = {
	.iface_ready = hid_hpd_iface_ready,
	.get_report = hid_get_report,
};

/**
 * \brief   Submit a prepared report through selected stack-owned HID interface.
 */
static int send_to_iface(enum usb_hid_stack_interface iface, const uint8_t *report, uint16_t len)
{
	const struct device *dev = NULL; // Resolved HID device for selected logical interface

	if (report == NULL || len == 0U) {
		return -EINVAL;
	}

	if (iface >= USB_HID_STACK_IF_COUNT) {
		return -EINVAL;
	}

	if (!g_ctx.initialized) {
		return -EACCES;
	}

	if (!g_ctx.ready[iface]) {
		return -EAGAIN;
	}

	switch (iface) {
	case USB_HID_STACK_IF_INPUT_DEVICES:
		dev = g_ctx.input_dev;
		break;
	case USB_HID_STACK_IF_CUSTOM_ALS:
		dev = g_ctx.custom_als_dev;
		break;
	case USB_HID_STACK_IF_HPD:
		dev = g_ctx.hpd_dev;
		break;
	default:
		return -EINVAL;
	}

	if (dev == NULL) {
		return -ENODEV;
	}

	return hid_device_submit_report(dev, len, report);
} /* send_to_iface */

/**
 * \brief   Initialize and register all stack-owned HID interfaces.
 */
int usb_hid_stack_service_init(void)
{
	int ret; // Return code from hid_device_register()

	if (g_ctx.initialized) {
		return 0;
	}

	g_ctx.input_dev = DEVICE_DT_GET(DT_NODELABEL(hid_input_dev_0));
	g_ctx.custom_als_dev = DEVICE_DT_GET(DT_NODELABEL(hid_custom_als_0));
	g_ctx.hpd_dev = DEVICE_DT_GET(DT_NODELABEL(hid_hpd_0));

	if (!device_is_ready(g_ctx.input_dev) || !device_is_ready(g_ctx.custom_als_dev) ||
	    !device_is_ready(g_ctx.hpd_dev)) {
		return -ENODEV;
	}

	ret = hid_device_register(g_ctx.input_dev, hid_input_devices_report_desc,
				  sizeof(hid_input_devices_report_desc), &hid_input_ops);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}

	ret = hid_device_register(g_ctx.custom_als_dev, hid_custom_als_report_desc,
				  sizeof(hid_custom_als_report_desc), &hid_custom_als_ops);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}

	ret = hid_device_register(g_ctx.hpd_dev, hid_hpd_customhpd_report_desc,
				  sizeof(hid_hpd_customhpd_report_desc), &hid_hpd_ops);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}

	g_ctx.initialized = true;
	printk("[USB] HID stack interfaces initialized\n");
	printk("[USB]  - HID Interface #1: Keyboard, Mouse, Pen, Touchpad, TouchScreen (Report IDs: 0x01-0x05)\n");
	printk("[USB]  - HID Interface #2: Custom, ALS (Report IDs: 0x40, 0x41)\n");
	printk("[USB]  - HID Interface #3: HPD, CUSTOMHPD (Report IDs: 0x01, 0x03)\n");
	return 0;
} /* usb_hid_stack_service_init */

/**
 * \brief   Return true if selected HID interface is host-ready.
 */
bool usb_hid_stack_service_is_ready(enum usb_hid_stack_interface iface)
{
	if (iface >= USB_HID_STACK_IF_COUNT) {
		return false;
	}

	return g_ctx.ready[iface];
} /* usb_hid_stack_service_is_ready */

/**
 * \brief   Build and submit keyboard report.
 */
int usb_hid_stack_send_keyboard(uint32_t counter)
{
#if defined(CONFIG_USB_HID_KEYBOARD)
	uint8_t report[USB_HID_KEYBOARD_REPORT_SIZE]; // Encoded keyboard HID report payload
	int ret = usb_hid_keyboard_build_report(counter, report, sizeof(report));

	if (ret != 0) {
		return ret;
	}

	return send_to_iface(USB_HID_STACK_IF_INPUT_DEVICES, report, sizeof(report));
#else
	ARG_UNUSED(counter);
	return -ENOTSUP;
#endif
} /* usb_hid_stack_send_keyboard */

int usb_hid_stack_send_keyboard_key(uint8_t keycode)
{
#if defined(CONFIG_USB_HID_KEYBOARD)
	uint8_t report[USB_HID_KEYBOARD_REPORT_SIZE]; // Encoded keyboard HID report payload
	int ret = usb_hid_keyboard_build_report_key(keycode, report, sizeof(report));

	if (ret != 0) {
		return ret;
	}

	ret = send_to_iface(USB_HID_STACK_IF_INPUT_DEVICES, report, sizeof(report));
	#if DEBUG_RUNTIME_LOGS
		// Debug-only: success debug logs are limited to every 20th report to avoid flooding the log.
		if (ret == 0) {
			static uint32_t g_kbd_submit_count;
			g_kbd_submit_count++;
			if ((g_kbd_submit_count == 1U) || ((g_kbd_submit_count % 20U) == 0U)) {
				LOG_INF("kbd submit ok count=%u key=0x%02x report_id=0x%02x",
						(unsigned int)g_kbd_submit_count,
						(unsigned int)keycode,
						(unsigned int)report[0]);
				LOG_HEXDUMP_INF(report, sizeof(report), "kbd report");
			}
		} 
		else
	#endif
		if (ret < 0 && ret != -EAGAIN) {
			LOG_WRN("kbd submit failed ret=%d key=0x%02x ready=%d",
					ret,
					(unsigned int)keycode,
					(int)g_ctx.ready[USB_HID_STACK_IF_INPUT_DEVICES]);
	}

	return ret;
#else
	ARG_UNUSED(keycode);
	return -ENOTSUP;
#endif
} /* usb_hid_stack_send_keyboard_key */

/**
 * \brief   Build and submit mouse report.
 */
int usb_hid_stack_send_mouse(uint32_t counter)
{
#if defined(CONFIG_USB_HID_MOUSE)
	uint8_t report[USB_HID_MOUSE_REPORT_SIZE]; // Encoded mouse HID report payload
	int ret = usb_hid_mouse_build_report(counter, report, sizeof(report));

	if (ret != 0) {
		return ret;
	}

	return send_to_iface(USB_HID_STACK_IF_INPUT_DEVICES, report, sizeof(report));
#else
	ARG_UNUSED(counter);
	return -ENOTSUP;
#endif
} /* usb_hid_stack_send_mouse */

/**
 * \brief   Build and submit pen report.
 */
int usb_hid_stack_send_pen(uint32_t counter)
{
	uint8_t report[USB_HID_PEN_REPORT_SIZE]; // Encoded pen HID report payload
	int ret = usb_hid_pen_build_report(counter, report, sizeof(report));

	if (ret != 0) {
		return ret;
	}

	return send_to_iface(USB_HID_STACK_IF_INPUT_DEVICES, report, sizeof(report));
} /* usb_hid_stack_send_pen */

/**
 * \brief   Build and submit touchpad report.
 */
int usb_hid_stack_send_touchpad(uint32_t counter)
{
	uint8_t report[USB_HID_TOUCHPAD_REPORT_SIZE]; // Encoded touchpad HID report payload
	int ret = usb_hid_touchpad_build_report(counter, report, sizeof(report));

	if (ret != 0) {
		return ret;
	}

	return send_to_iface(USB_HID_STACK_IF_INPUT_DEVICES, report, sizeof(report));
} /* usb_hid_stack_send_touchpad */

/**
 * \brief   Build and submit touchscreen report.
 */
int usb_hid_stack_send_touchscreen(uint32_t counter)
{
	uint8_t report[USB_HID_TOUCHSCREEN_REPORT_SIZE]; // Encoded touchscreen HID report payload
	int ret = usb_hid_touchscreen_build_report(counter, report, sizeof(report));

	if (ret != 0) {
		return ret;
	}

	return send_to_iface(USB_HID_STACK_IF_INPUT_DEVICES, report, sizeof(report));
} /* usb_hid_stack_send_touchscreen */

/**
 * \brief   Build and submit custom report.
 */
int usb_hid_stack_send_custom(uint32_t counter)
{
	uint8_t report[USB_HID_CUSTOM_REPORT_SIZE]; // Encoded custom HID report payload
	int ret = usb_hid_custom_build_report(counter, report, sizeof(report));

	if (ret != 0) {
		return ret;
	}

	return send_to_iface(USB_HID_STACK_IF_CUSTOM_ALS, report, sizeof(report));
} /* usb_hid_stack_send_custom */

/**
 * \brief   Build and submit ALS report.
 */
int usb_hid_stack_send_als(uint32_t counter)
{
	uint8_t report[USB_HID_ALS_REPORT_SIZE]; // Encoded ALS HID report payload
	int ret = usb_hid_als_build_report(counter, report, sizeof(report));

	if (ret != 0) {
		return ret;
	}

	return send_to_iface(USB_HID_STACK_IF_CUSTOM_ALS, report, sizeof(report));
} /* usb_hid_stack_send_als */

/**
 * \brief   Build and submit HPD report.
 */
int usb_hid_stack_send_hpd(uint32_t counter)
{
	uint8_t report[USB_HID_HPD_REPORT_SIZE]; // Encoded HPD HID report payload
	int ret = usb_hid_hpd_build_report(counter, report, sizeof(report));

	if (ret != 0) {
		return ret;
	}

	return send_to_iface(USB_HID_STACK_IF_HPD, report, sizeof(report));
} /* usb_hid_stack_send_hpd */

/**
 * \brief   Build and submit CUSTOMHPD report.
 */
int usb_hid_stack_send_customhpd(uint32_t counter)
{
	uint8_t report[USB_HID_CUSTOMHPD_REPORT_SIZE]; // Encoded CUSTOMHPD HID report payload
	int ret = usb_hid_customhpd_build_report(counter, report, sizeof(report));

	if (ret != 0) {
		return ret;
	}

	return send_to_iface(USB_HID_STACK_IF_HPD, report, sizeof(report));
} /* usb_hid_stack_send_customhpd */

#else

int usb_hid_stack_service_init(void)
{
	return -ENOTSUP;
} /* usb_hid_stack_service_init */

bool usb_hid_stack_service_is_ready(enum usb_hid_stack_interface iface)
{
	ARG_UNUSED(iface);
	return false;
} /* usb_hid_stack_service_is_ready */

int usb_hid_stack_send_keyboard(uint32_t counter)
{
	ARG_UNUSED(counter);
	return -ENOTSUP;
} /* usb_hid_stack_send_keyboard */

int usb_hid_stack_send_keyboard_key(uint8_t keycode)
{
	ARG_UNUSED(keycode);
	return -ENOTSUP;
} /* usb_hid_stack_send_keyboard_key */

int usb_hid_stack_send_mouse(uint32_t counter)
{
	ARG_UNUSED(counter);
	return -ENOTSUP;
} /* usb_hid_stack_send_mouse */

int usb_hid_stack_send_pen(uint32_t counter)
{
	ARG_UNUSED(counter);
	return -ENOTSUP;
} /* usb_hid_stack_send_pen */

int usb_hid_stack_send_touchpad(uint32_t counter)
{
	ARG_UNUSED(counter);
	return -ENOTSUP;
} /* usb_hid_stack_send_touchpad */

int usb_hid_stack_send_touchscreen(uint32_t counter)
{
	ARG_UNUSED(counter);
	return -ENOTSUP;
} /* usb_hid_stack_send_touchscreen */

int usb_hid_stack_send_custom(uint32_t counter)
{
	ARG_UNUSED(counter);
	return -ENOTSUP;
} /* usb_hid_stack_send_custom */

int usb_hid_stack_send_als(uint32_t counter)
{
	ARG_UNUSED(counter);
	return -ENOTSUP;
} /* usb_hid_stack_send_als */

int usb_hid_stack_send_hpd(uint32_t counter)
{
	ARG_UNUSED(counter);
	return -ENOTSUP;
} /* usb_hid_stack_send_hpd */

int usb_hid_stack_send_customhpd(uint32_t counter)
{
	ARG_UNUSED(counter);
	return -ENOTSUP;
} /* usb_hid_stack_send_customhpd */

#endif
