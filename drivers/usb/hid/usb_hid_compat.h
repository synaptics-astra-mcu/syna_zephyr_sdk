/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID compatibility layer API.
 *
 * @file usb_hid_compat.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_USB_hid_H_
/* Include guard for HID compatibility API used by profile-oriented callers. */
#define SYNA_USB_hid_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration for Zephyr device handle type used by config struct. */
struct device;

enum usb_hid_profile {
	USB_HID_PROFILE_ALS = 0,
	USB_HID_PROFILE_PTP,
	USB_HID_PROFILE_TSP,
	USB_HID_PROFILE_CUSTOM,
	USB_HID_PROFILE_HUMAN_PRESENCE,
	USB_HID_PROFILE_GENERIC_ALGO,
	USB_HID_PROFILE_COUNT,
};

struct usb_hid_device_cfg {
	const struct device *dev;
	const uint8_t *report_desc;
	size_t report_desc_size;
};

/**
 * Register one HID profile device and descriptor with Zephyr HID stack.
 */
int usb_hid_register_profile(enum usb_hid_profile profile, const struct usb_hid_device_cfg *cfg);

/**
 * Register all enabled HID profiles on a shared device/descriptor set.
 */
int usb_hid_register_all_profiles(const struct usb_hid_device_cfg *cfg);

int usb_hid_register_als(const struct usb_hid_device_cfg *cfg);
int usb_hid_register_ptp(const struct usb_hid_device_cfg *cfg);
int usb_hid_register_tsp(const struct usb_hid_device_cfg *cfg);
int usb_hid_register_custom(const struct usb_hid_device_cfg *cfg);
int usb_hid_register_human_presence(const struct usb_hid_device_cfg *cfg);
int usb_hid_register_generic_algo(const struct usb_hid_device_cfg *cfg);

/**
 * Submit input report bytes for the given profile.
 */
int usb_hid_submit_report(enum usb_hid_profile profile, const uint8_t *report, size_t report_len);

int usb_hid_send_als(const uint8_t *report, size_t report_len);
int usb_hid_send_ptp(const uint8_t *report, size_t report_len);
int usb_hid_send_tsp(const uint8_t *report, size_t report_len);
int usb_hid_send_custom(const uint8_t *report, size_t report_len);
int usb_hid_send_human_presence(const uint8_t *report, size_t report_len);
int usb_hid_send_generic_algo(const uint8_t *report, size_t report_len);

/**
 * Returns true when profile interface is enumerated and ready.
 */
bool usb_hid_is_ready(enum usb_hid_profile profile);

#endif /* SYNA_USB_hid_H_ */
