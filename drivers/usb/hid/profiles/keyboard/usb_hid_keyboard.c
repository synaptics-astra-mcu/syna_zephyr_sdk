/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID KEYBOARD profile implementation.
 *
 * @file usb_hid_keyboard.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/usb/class/hid.h>

#include "usb_hid_keyboard.h"

/**
 * \brief   Build keyboard HID report payload.
 *
 * \details Fills an 8-byte keyboard input report with a rotating key code.
 *
 * \param   counter    Monotonic sample counter used as payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_keyboard_build_report_key(uint8_t keycode, uint8_t *report, size_t report_len)
{
	if (report == NULL || report_len < USB_HID_KEYBOARD_REPORT_SIZE) {
		return -EINVAL;
	}

	memset(report, 0, USB_HID_KEYBOARD_REPORT_SIZE);
	report[0] = USB_HID_KEYBOARD_REPORT_ID;
	report[3] = keycode;

	return 0;
} /* usb_hid_keyboard_build_report_key */

int usb_hid_keyboard_build_report(uint32_t counter, uint8_t *report, size_t report_len)
{
	return usb_hid_keyboard_build_report_key(HID_KEY_A + (counter % 26U), report, report_len);
} /* usb_hid_keyboard_build_report */
