/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID CUSTOM profile implementation.
 *
 * @file usb_hid_custom.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "usb_hid_custom.h"

/**
 * \brief   Build custom HID report payload.
 *
 * \details Fills a 64-byte vendor report and encodes the sample counter.
 *
 * \param   counter    Monotonic sample counter used as payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_custom_build_report(uint32_t counter, uint8_t *report, size_t report_len)
{
	if (report == NULL || report_len < USB_HID_CUSTOM_REPORT_SIZE) {
		return -EINVAL;
	}

	memset(report, 0, USB_HID_CUSTOM_REPORT_SIZE);
	report[0] = USB_HID_CUSTOM_REPORT_ID;
	report[1] = (uint8_t)(counter & 0xFFU);
	report[2] = (uint8_t)((counter >> 8U) & 0xFFU);

	return 0;
} /* usb_hid_custom_build_report */
