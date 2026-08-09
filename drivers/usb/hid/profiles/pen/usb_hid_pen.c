/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID PEN profile implementation.
 *
 * @file usb_hid_pen.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "usb_hid_pen.h"

/**
 * \brief   Build pen HID report payload.
 *
 * \details Fills a 6-byte pen report with tip/in-range and X/Y data fields.
 *
 * \param   counter    Monotonic sample counter used as payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_pen_build_report(uint32_t counter, uint8_t *report, size_t report_len)
{
	if (report == NULL || report_len < USB_HID_PEN_REPORT_SIZE) {
		return -EINVAL;
	}

	memset(report, 0, USB_HID_PEN_REPORT_SIZE);
	report[0] = USB_HID_PEN_REPORT_ID;
	report[1] = (counter & 0x01U) | ((counter & 0x01U) << 1U);
	report[2] = (uint8_t)(counter & 0xFFU);
	report[3] = (uint8_t)((counter >> 4U) & 0x0FU);
	report[4] = (uint8_t)((counter >> 2U) & 0xFFU);
	report[5] = (uint8_t)((counter >> 6U) & 0x0FU);

	return 0;
} /* usb_hid_pen_build_report */
