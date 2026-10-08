/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID CUSTOMHPD profile API.
 *
 * @file usb_hid_customhpd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_USB_HID_CUSTOMHPD_H_
/* Include guard for CUSTOMHPD profile report builder API. */
#define SYNA_USB_HID_CUSTOMHPD_H_

#include <stddef.h>
#include <stdint.h>

/* CUSTOMHPD report ID used by composite HID descriptor. */
#define USB_HID_CUSTOMHPD_REPORT_ID   0x03U
/* Byte size of encoded CUSTOMHPD input report. */
#define USB_HID_CUSTOMHPD_REPORT_SIZE 7U

/**
 * \brief   Build CUSTOMHPD HID input report bytes.
 *
 * \details Encodes vendor-specific HPD payload fields using sample counter.
 *
 * \param   counter    Monotonic sample counter used as report payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_customhpd_build_report(uint32_t counter, uint8_t *report, size_t report_len);

#endif /* SYNA_USB_HID_CUSTOMHPD_H_ */
