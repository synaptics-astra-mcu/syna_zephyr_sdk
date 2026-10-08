/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID MOUSE profile implementation.
 *
 * @file usb_hid_mouse.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "usb_hid_mouse.h"

/**
 * \brief   Build mouse HID report payload.
 *
 * \details Fills a 4-byte mouse report with button and relative motion fields.
 *
 * \param   counter    Monotonic sample counter used as payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_mouse_build_report(uint32_t counter, uint8_t *report, size_t report_len)
{
	if (report == NULL || report_len < USB_HID_MOUSE_REPORT_SIZE) {
		return -EINVAL;
	}

	memset(report, 0, USB_HID_MOUSE_REPORT_SIZE);
	report[0] = USB_HID_MOUSE_REPORT_ID;
	report[1] = (counter & 0x01U) ? 0x01U : 0x00U;
	report[2] = (counter & 0x01U) ? 24U : (uint8_t)-24;
	report[3] = ((counter >> 1U) & 0x01U) ? 12U : (uint8_t)-12;

	return 0;
} /* usb_hid_mouse_build_report */
