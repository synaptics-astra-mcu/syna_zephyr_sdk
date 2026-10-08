/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID PEN profile API.
 *
 * @file usb_hid_pen.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_USB_HID_PEN_H_
/* Include guard for pen profile report builder API. */
#define SYNA_USB_HID_PEN_H_

#include <stddef.h>
#include <stdint.h>

/* Pen report ID used by composite HID descriptor. */
#define USB_HID_PEN_REPORT_ID   0x03U
/* Byte size of encoded pen input report. */
#define USB_HID_PEN_REPORT_SIZE 6U

/**
 * \brief   Build pen HID input report bytes.
 *
 * \details Encodes tip/in-range flags and 16-bit X/Y coordinates.
 *
 * \param   counter    Monotonic sample counter used as report payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_pen_build_report(uint32_t counter, uint8_t *report, size_t report_len);

#endif /* SYNA_USB_HID_PEN_H_ */
