/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID CUSTOM profile API.
 *
 * @file usb_hid_custom.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_USB_HID_CUSTOM_H_
/* Include guard for custom profile report builder API. */
#define SYNA_USB_HID_CUSTOM_H_

#include <stddef.h>
#include <stdint.h>

/* Custom report ID used by composite HID descriptor. */
#define USB_HID_CUSTOM_REPORT_ID   0x40U
/* Byte size of encoded custom input report. */
#define USB_HID_CUSTOM_REPORT_SIZE 64U

/**
 * \brief   Build vendor custom HID input report bytes.
 *
 * \details Encodes report ID and sample counter bytes in the custom payload.
 *
 * \param   counter    Monotonic sample counter used as report payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_custom_build_report(uint32_t counter, uint8_t *report, size_t report_len);

#endif /* SYNA_USB_HID_CUSTOM_H_ */
