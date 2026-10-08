/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID HPD profile API.
 *
 * @file usb_hid_hpd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_USB_HID_HPD_H_
/* Include guard for HPD profile report builder API. */
#define SYNA_USB_HID_HPD_H_

#include <stddef.h>
#include <stdint.h>

/* HPD report ID used by composite HID descriptor. */
#define USB_HID_HPD_REPORT_ID   0x01U
/* Byte size of encoded HPD input report. */
#define USB_HID_HPD_REPORT_SIZE 7U

/**
 * \brief   Build HPD HID input report bytes.
 *
 * \details Encodes presence/motion and distance fields using sample counter.
 *
 * \param   counter    Monotonic sample counter used as report payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_hpd_build_report(uint32_t counter, uint8_t *report, size_t report_len);

#endif /* SYNA_USB_HID_HPD_H_ */
