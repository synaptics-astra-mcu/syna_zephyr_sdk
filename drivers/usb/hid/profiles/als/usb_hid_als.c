/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID ALS profile implementation.
 *
 * @file usb_hid_als.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "usb_hid_als.h"

/**
 * \brief   Build ALS HID report payload.
 *
 * \details Fills a 3-byte ALS report with a 16-bit illuminance payload.
 *
 * \param   counter    Monotonic sample counter used as payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_als_build_report(uint32_t counter, uint8_t *report, size_t report_len)
{
	if (report == NULL || report_len < USB_HID_ALS_REPORT_SIZE) {
		return -EINVAL;
	}

	memset(report, 0, USB_HID_ALS_REPORT_SIZE);
	report[0] = USB_HID_ALS_REPORT_ID;
	report[1] = (uint8_t)(counter & 0xFFU);
	report[2] = (uint8_t)((counter >> 8U) & 0x7FU);

	return 0;
} /* usb_hid_als_build_report */
