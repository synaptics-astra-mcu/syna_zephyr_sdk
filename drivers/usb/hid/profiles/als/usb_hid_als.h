/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID ALS profile API.
 *
 * @file usb_hid_als.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_USB_HID_ALS_H_
/* Include guard for ALS profile report builder API. */
#define SYNA_USB_HID_ALS_H_

#include <stddef.h>
#include <stdint.h>

/* ALS report ID used by composite HID descriptor. */
#define USB_HID_ALS_REPORT_ID   0x41U
/* Byte size of encoded ALS input report. */
#define USB_HID_ALS_REPORT_SIZE 3U

/**
 * \brief   Build ALS HID input report bytes.
 *
 * \details Encodes report ID and 16-bit illuminance value from sample counter.
 *
 * \param   counter    Monotonic sample counter used as report payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_als_build_report(uint32_t counter, uint8_t *report, size_t report_len);

#endif /* SYNA_USB_HID_ALS_H_ */
