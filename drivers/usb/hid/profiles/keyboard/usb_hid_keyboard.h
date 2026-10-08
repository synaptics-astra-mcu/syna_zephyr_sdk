/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID KEYBOARD profile API.
 *
 * @file usb_hid_keyboard.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_USB_HID_KEYBOARD_H_
/* Include guard for keyboard profile report builder API. */
#define SYNA_USB_HID_KEYBOARD_H_

#include <stddef.h>
#include <stdint.h>

/* Keyboard report ID used by composite HID descriptor. */
#define USB_HID_KEYBOARD_REPORT_ID   0x01U
/* Byte size of encoded keyboard input report. */
#define USB_HID_KEYBOARD_REPORT_SIZE 9U

/**
 * \brief   Build keyboard HID input report bytes.
 *
 * \details Encodes report ID and key payload using the provided sample counter.
 *
 * \param   counter    Monotonic sample counter used as report payload source.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_keyboard_build_report(uint32_t counter, uint8_t *report, size_t report_len);

/**
 * \brief   Build keyboard HID input report bytes for a specific keycode.
 *
 * \param   keycode    HID key usage to place in the primary key slot.
 * \param   report     Output report buffer.
 * \param   report_len Size of output report buffer in bytes.
 * \return  0 on success, negative errno on invalid arguments.
 */
int usb_hid_keyboard_build_report_key(uint8_t keycode, uint8_t *report, size_t report_len);

#endif /* SYNA_USB_HID_KEYBOARD_H_ */
