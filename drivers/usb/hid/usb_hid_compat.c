/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID compatibility layer implementation.
 *
 * @file usb_hid_compat.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/class/usbd_hid.h>

#include "usb_hid_compat.h"

struct usb_hid_profile_ctx {
	const struct device *dev; // Registered HID device backing this profile
	bool ready;               // Host interface readiness signaled by HID core
	uint32_t idle_rate_ms;    // Last idle rate requested by host for this profile
};

/* Runtime state for each logical HID profile routed through compatibility layer. */
static struct usb_hid_profile_ctx g_profile_ctx[USB_HID_PROFILE_COUNT];
/* Shared physical HID device pointer used by the currently active registration set. */
static const struct device *g_shared_dev;

static bool profile_enabled(const enum usb_hid_profile profile)
{
	switch (profile) {
	case USB_HID_PROFILE_ALS:
		return IS_ENABLED(CONFIG_USB_HID_ALS);
	case USB_HID_PROFILE_PTP:
		return IS_ENABLED(CONFIG_USB_HID_PTP);
	case USB_HID_PROFILE_TSP:
		return IS_ENABLED(CONFIG_USB_HID_TSP);
	case USB_HID_PROFILE_CUSTOM:
		return IS_ENABLED(CONFIG_USB_HID_CUSTOM);
	case USB_HID_PROFILE_HUMAN_PRESENCE:
		return IS_ENABLED(CONFIG_USB_HID_HUMAN_PRESENCE);
	case USB_HID_PROFILE_GENERIC_ALGO:
		return IS_ENABLED(CONFIG_USB_HID_GENERIC_ALGO);
	default:
		return false;
	}
}

static enum usb_hid_profile profile_from_dev(const struct device *dev)
{
	for (int i = 0; i < USB_HID_PROFILE_COUNT; i++) {
		if (g_profile_ctx[i].dev == dev) {
			return (enum usb_hid_profile)i;
		}
	}

	return USB_HID_PROFILE_COUNT;
}

static void hid_compat_iface_ready(const struct device *dev, const bool ready)
{
	for (int i = 0; i < USB_HID_PROFILE_COUNT; i++) {
		if (g_profile_ctx[i].dev == dev) {
			g_profile_ctx[i].ready = ready;
		}
	}
}

static int hid_compat_get_report(const struct device *dev,
				 const uint8_t type, const uint8_t id,
				 const uint16_t len, uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(type);
	ARG_UNUSED(id);

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	memset(buf, 0, len);
	return len;
}

static int hid_compat_set_report(const struct device *dev,
				 const uint8_t type, const uint8_t id,
				 const uint16_t len,
				 const uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(type);
	ARG_UNUSED(id);

	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}

	return 0;
}

static void hid_compat_set_idle(const struct device *dev,
				const uint8_t id, const uint32_t duration)
{
	ARG_UNUSED(id);

	enum usb_hid_profile profile = profile_from_dev(dev);

	if (profile >= USB_HID_PROFILE_COUNT) {
		return;
	}

	g_profile_ctx[profile].idle_rate_ms = duration;
}

static uint32_t hid_compat_get_idle(const struct device *dev, const uint8_t id)
{
	ARG_UNUSED(id);

	enum usb_hid_profile profile = profile_from_dev(dev);

	if (profile >= USB_HID_PROFILE_COUNT) {
		return 0U;
	}

	return g_profile_ctx[profile].idle_rate_ms;
}

static void hid_compat_set_protocol(const struct device *dev, const uint8_t proto)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(proto);
}

static const struct hid_device_ops hid_compat_ops = {
	.iface_ready = hid_compat_iface_ready,
	.get_report = hid_compat_get_report,
	.set_report = hid_compat_set_report,
	.set_idle = hid_compat_set_idle,
	.get_idle = hid_compat_get_idle,
	.set_protocol = hid_compat_set_protocol,
};

int usb_hid_register_profile(const enum usb_hid_profile profile,
				    const struct usb_hid_device_cfg *cfg)
{
	int ret; // Return code from hid_device_register()

	if (profile >= USB_HID_PROFILE_COUNT || cfg == NULL || cfg->dev == NULL ||
	    cfg->report_desc == NULL || cfg->report_desc_size == 0U) {
		return -EINVAL;
	}

	if (!profile_enabled(profile)) {
		return -ENOTSUP;
	}

	if (!device_is_ready(cfg->dev)) {
		return -ENODEV;
	}

	g_profile_ctx[profile].dev = cfg->dev;
	g_profile_ctx[profile].ready = false;
	g_profile_ctx[profile].idle_rate_ms = 0U;

	if (g_shared_dev == cfg->dev) {
		return 0;
	}

	ret = hid_device_register(cfg->dev, cfg->report_desc,
				  cfg->report_desc_size, &hid_compat_ops);
	if (ret == 0 || ret == -EALREADY) {
		g_shared_dev = cfg->dev;
		return 0;
	}

	return ret;
}

int usb_hid_register_all_profiles(const struct usb_hid_device_cfg *cfg)
{
	int ret; // Return code from hid_device_register()

	if (cfg == NULL || cfg->dev == NULL || cfg->report_desc == NULL || cfg->report_desc_size == 0U) {
		return -EINVAL;
	}

	if (!device_is_ready(cfg->dev)) {
		return -ENODEV;
	}

	for (int i = 0; i < USB_HID_PROFILE_COUNT; i++) {
		if (!profile_enabled((enum usb_hid_profile)i)) {
			continue;
		}

		g_profile_ctx[i].dev = cfg->dev;
		g_profile_ctx[i].ready = false;
		g_profile_ctx[i].idle_rate_ms = 0U;
	}

	if (g_shared_dev == cfg->dev) {
		return 0;
	}

	ret = hid_device_register(cfg->dev, cfg->report_desc,
				 cfg->report_desc_size, &hid_compat_ops);
	if (ret == 0 || ret == -EALREADY) {
		g_shared_dev = cfg->dev;
		return 0;
	}

	return ret;
}

int usb_hid_register_als(const struct usb_hid_device_cfg *cfg)
{
	return usb_hid_register_profile(USB_HID_PROFILE_ALS, cfg);
}

int usb_hid_register_ptp(const struct usb_hid_device_cfg *cfg)
{
	return usb_hid_register_profile(USB_HID_PROFILE_PTP, cfg);
}

int usb_hid_register_tsp(const struct usb_hid_device_cfg *cfg)
{
	return usb_hid_register_profile(USB_HID_PROFILE_TSP, cfg);
}

int usb_hid_register_custom(const struct usb_hid_device_cfg *cfg)
{
	return usb_hid_register_profile(USB_HID_PROFILE_CUSTOM, cfg);
}

int usb_hid_register_human_presence(const struct usb_hid_device_cfg *cfg)
{
	return usb_hid_register_profile(USB_HID_PROFILE_HUMAN_PRESENCE, cfg);
}

int usb_hid_register_generic_algo(const struct usb_hid_device_cfg *cfg)
{
	return usb_hid_register_profile(USB_HID_PROFILE_GENERIC_ALGO, cfg);
}

int usb_hid_submit_report(const enum usb_hid_profile profile,
				 const uint8_t *report, const size_t report_len)
{
	if (profile >= USB_HID_PROFILE_COUNT || report == NULL || report_len == 0U) {
		return -EINVAL;
	}

	if (!profile_enabled(profile)) {
		return -ENOTSUP;
	}

	if (!g_profile_ctx[profile].ready || g_profile_ctx[profile].dev == NULL) {
		return -EAGAIN;
	}

	if (report_len > UINT16_MAX) {
		return -EMSGSIZE;
	}

	return hid_device_submit_report(g_profile_ctx[profile].dev,
					(uint16_t)report_len, report);
}

int usb_hid_send_als(const uint8_t *report, size_t report_len)
{
	return usb_hid_submit_report(USB_HID_PROFILE_ALS, report, report_len);
}

int usb_hid_send_ptp(const uint8_t *report, size_t report_len)
{
	return usb_hid_submit_report(USB_HID_PROFILE_PTP, report, report_len);
}

int usb_hid_send_tsp(const uint8_t *report, size_t report_len)
{
	return usb_hid_submit_report(USB_HID_PROFILE_TSP, report, report_len);
}

int usb_hid_send_custom(const uint8_t *report, size_t report_len)
{
	return usb_hid_submit_report(USB_HID_PROFILE_CUSTOM, report, report_len);
}

int usb_hid_send_human_presence(const uint8_t *report, size_t report_len)
{
	return usb_hid_submit_report(USB_HID_PROFILE_HUMAN_PRESENCE, report, report_len);
}

int usb_hid_send_generic_algo(const uint8_t *report, size_t report_len)
{
	return usb_hid_submit_report(USB_HID_PROFILE_GENERIC_ALGO, report, report_len);
}

bool usb_hid_is_ready(const enum usb_hid_profile profile)
{
	if (profile >= USB_HID_PROFILE_COUNT) {
		return false;
	}

	if (!profile_enabled(profile)) {
		return false;
	}

	return g_profile_ctx[profile].ready;
}
