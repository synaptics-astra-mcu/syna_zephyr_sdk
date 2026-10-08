/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID HPD profile implementation.
 *
 * @file usb_hid_hpd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "usb_hid_hpd.h"

/**
 * \brief   Build HPD HID report payload.
 *
 * \details Fills a 7-byte HPD report compatible with the current sample mapping.
 *
 * \param   counter    Monotonic sample counter used as payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_hpd_build_report(uint32_t counter, uint8_t *report, size_t report_len)
{
	if (report == NULL || report_len < USB_HID_HPD_REPORT_SIZE) {
		return -EINVAL;
	}

	memset(report, 0, USB_HID_HPD_REPORT_SIZE);
	report[0] = USB_HID_HPD_REPORT_ID;
	report[1] = (counter & 0x01U) ? 2U : 1U;
	report[2] = (counter & 0x01U) ? 1U : 0U;
	report[3] = (counter & 0x01U) ? 1U : 0U;
	report[4] = (counter & 0x01U) ? 1U : 0U;
	report[5] = (uint8_t)(counter & 0xFFU);
	report[6] = (uint8_t)((counter >> 8U) & 0xFFU);

	return 0;
} /* usb_hid_hpd_build_report */
