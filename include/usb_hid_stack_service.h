/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID stack service API.
 *
 * @file usb_hid_stack_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_USB_HID_STACK_SERVICE_H_
/* Include guard for stack-owned HID service API. */
#define SYNA_USB_HID_STACK_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>

enum usb_hid_stack_interface {
	USB_HID_STACK_IF_INPUT_DEVICES = 0,
	USB_HID_STACK_IF_CUSTOM_ALS,
	USB_HID_STACK_IF_HPD,
	USB_HID_STACK_IF_COUNT,
};

/**
 * \brief   Initialize stack-owned HID interfaces for the active core.
 *
 * \details Registers HID interfaces, callbacks, and report descriptors.
 *          On non-USB targets this API returns -ENOTSUP.
 *
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_service_init(void);

/**
 * \brief   Query whether a stack-owned HID interface is enumerated and ready.
 *
 * \param   iface HID interface identifier.
 * \return  true if ready; otherwise false.
 */
bool usb_hid_stack_service_is_ready(enum usb_hid_stack_interface iface);

/**
 * \brief   Build and submit keyboard report through stack HID service.
 * \param   counter Monotonic sample counter used as payload source.
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_send_keyboard(uint32_t counter);

/**
 * \brief   Build and submit keyboard report for a specific HID key.
 * \param   keycode HID key usage to place in the report.
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_send_keyboard_key(uint8_t keycode);

/**
 * \brief   Build and submit mouse report through stack HID service.
 * \param   counter Monotonic sample counter used as payload source.
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_send_mouse(uint32_t counter);

/**
 * \brief   Build and submit pen report through stack HID service.
 * \param   counter Monotonic sample counter used as payload source.
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_send_pen(uint32_t counter);

/**
 * \brief   Build and submit touchpad report through stack HID service.
 * \param   counter Monotonic sample counter used as payload source.
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_send_touchpad(uint32_t counter);

/**
 * \brief   Build and submit touchscreen report through stack HID service.
 * \param   counter Monotonic sample counter used as payload source.
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_send_touchscreen(uint32_t counter);

/**
 * \brief   Build and submit custom report through stack HID service.
 * \param   counter Monotonic sample counter used as payload source.
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_send_custom(uint32_t counter);

/**
 * \brief   Build and submit ALS report through stack HID service.
 * \param   counter Monotonic sample counter used as payload source.
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_send_als(uint32_t counter);

/**
 * \brief   Build and submit HPD report through stack HID service.
 * \param   counter Monotonic sample counter used as payload source.
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_send_hpd(uint32_t counter);

/**
 * \brief   Build and submit CUSTOMHPD report through stack HID service.
 * \param   counter Monotonic sample counter used as payload source.
 * \return  0 on success, negative errno on failure.
 */
int usb_hid_stack_send_customhpd(uint32_t counter);

#endif /* SYNA_USB_HID_STACK_SERVICE_H_ */
