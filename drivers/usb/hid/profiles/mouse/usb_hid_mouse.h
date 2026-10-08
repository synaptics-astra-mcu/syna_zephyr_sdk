/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID MOUSE profile API.
 *
 * @file usb_hid_mouse.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_USB_HID_MOUSE_H_
/* Include guard for mouse profile report builder API. */
#define SYNA_USB_HID_MOUSE_H_

#include <stddef.h>
#include <stdint.h>

/* Mouse report ID used by composite HID descriptor. */
#define USB_HID_MOUSE_REPORT_ID   0x02U
/* Byte size of encoded mouse input report. */
#define USB_HID_MOUSE_REPORT_SIZE 4U

/**
 * \brief   Build mouse HID input report bytes.
 *
 * \details Encodes buttons and relative X/Y movement using the counter.
 *
 * \param   counter    Monotonic sample counter used as report payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_mouse_build_report(uint32_t counter, uint8_t *report, size_t report_len);

#endif /* SYNA_USB_HID_MOUSE_H_ */
