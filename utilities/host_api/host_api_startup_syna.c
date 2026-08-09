/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "host_api.h"

#include <errno.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_SERVICE_USB_HOST_API_ENABLED)
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/usb/usbd.h>
#endif

#include <logger.h>

#if defined(CONFIG_SERVICE_FW_UPDATE_ENABLED)
#include "service_fw_update.h"
#endif

#ifndef LOG_MOD_HOST_API
#define LOG_MOD_HOST_API "HOST_API"
#endif

#if defined(CONFIG_SERVICE_USB_HOST_API_ENABLED) && defined(CONFIG_USB_DEVICE_STACK_NEXT)
USBD_DEVICE_DEFINE(host_api_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   CONFIG_HOST_API_USB_VID, CONFIG_HOST_API_USB_PID);

USBD_DESC_LANG_DEFINE(host_api_usbd_lang);
USBD_DESC_MANUFACTURER_DEFINE(host_api_usbd_mfr, CONFIG_HOST_API_USB_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(host_api_usbd_product, "Host API");
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(host_api_usbd_sn)));

USBD_DESC_CONFIG_DEFINE(host_api_fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(host_api_hs_cfg_desc, "HS Configuration");

static const uint8_t host_api_usb_attributes =
	(IS_ENABLED(CONFIG_HOST_API_USB_SELF_POWERED) ? USB_SCD_SELF_POWERED : 0U) |
	(IS_ENABLED(CONFIG_HOST_API_USB_REMOTE_WAKEUP) ? USB_SCD_REMOTE_WAKEUP : 0U);

USBD_CONFIGURATION_DEFINE(host_api_fs_config,
			  host_api_usb_attributes,
			  CONFIG_HOST_API_USB_MAX_POWER,
			  &host_api_fs_cfg_desc);

USBD_CONFIGURATION_DEFINE(host_api_hs_config,
			  host_api_usb_attributes,
			  CONFIG_HOST_API_USB_MAX_POWER,
			  &host_api_hs_cfg_desc);

static bool host_api_usb_stack_initialized;

static int host_api_usb_register_speed(struct usbd_context *const uds_ctx,
				       const enum usbd_speed speed)
{
	struct usbd_config_node *cfg_nd;
	int err;

	cfg_nd = (speed == USBD_SPEED_HS) ? &host_api_hs_config : &host_api_fs_config;

	err = usbd_add_configuration(uds_ctx, speed, cfg_nd);
	if (err != 0) {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "Failed to add Host API USB configuration for speed %d: %d\n",
			  speed, err);
		return err;
	}

	err = usbd_register_class(uds_ctx, "cdc_acm_0", speed, 1);
	if (err != 0) {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "Failed to register Host API USB ACM class for speed %d: %d\n",
			  speed, err);
		return err;
	}

#if DT_NODE_EXISTS(DT_NODELABEL(cdc_acm_uart1))
	err = usbd_register_class(uds_ctx, "cdc_acm_1", speed, 1);
	if (err != 0) {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "Failed to register Host API USB stream ACM class for speed %d: %d\n",
			  speed, err);
		return err;
	}
#endif

	err = usbd_device_set_code_triple(uds_ctx, speed, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	if (err != 0) {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "Failed to set Host API USB code triple for speed %d: %d\n",
			  speed, err);
		return err;
	}

	return 0;
}

static int host_api_usb_stack_init(void)
{
	int err;

	if (host_api_usb_stack_initialized) {
		return 0;
	}

	err = usbd_add_descriptor(&host_api_usbd, &host_api_usbd_lang);
	if (err != 0) {
		LOG_ERROR(LOG_MOD_HOST_API, "Failed to add Host API USB language descriptor: %d\n",
			  err);
		return err;
	}

	err = usbd_add_descriptor(&host_api_usbd, &host_api_usbd_mfr);
	if (err != 0) {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "Failed to add Host API USB manufacturer descriptor: %d\n", err);
		return err;
	}

	err = usbd_add_descriptor(&host_api_usbd, &host_api_usbd_product);
	if (err != 0) {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "Failed to add Host API USB product descriptor: %d\n", err);
		return err;
	}

	IF_ENABLED(CONFIG_HWINFO, (
		err = usbd_add_descriptor(&host_api_usbd, &host_api_usbd_sn);
	))
	if (err != 0) {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "Failed to add Host API USB serial descriptor: %d\n", err);
		return err;
	}

	if (USBD_SUPPORTS_HIGH_SPEED &&
	    (usbd_caps_speed(&host_api_usbd) == USBD_SPEED_HS)) {
		err = host_api_usb_register_speed(&host_api_usbd, USBD_SPEED_HS);
		if (err != 0) {
			return err;
		}
	}

	err = host_api_usb_register_speed(&host_api_usbd, USBD_SPEED_FS);
	if (err != 0) {
		return err;
	}

	usbd_self_powered(&host_api_usbd, host_api_usb_attributes & USB_SCD_SELF_POWERED);

	err = usbd_init(&host_api_usbd);
	if (err != 0) {
		LOG_ERROR(LOG_MOD_HOST_API, "Failed to initialize Host API USB device: %d\n", err);
		return err;
	}

	if (!usbd_can_detect_vbus(&host_api_usbd)) {
		err = usbd_enable(&host_api_usbd);
		if (err != 0) {
			LOG_ERROR(LOG_MOD_HOST_API, "Failed to enable Host API USB device: %d\n",
				  err);
			return err;
		}
	}

	host_api_usb_stack_initialized = true;
	LOG_INFO(LOG_MOD_HOST_API, "Host API USB transport initialized\n");

	return 0;
}
#else
static int host_api_usb_stack_init(void)
{
	return -ENOTSUP;
}
#endif

static int host_api_start_enabled_services(void)
{
#if defined(CONFIG_SERVICE_FW_UPDATE_ENABLED)
	service_fw_update_task_create();
#endif

	return 0;
}

static int host_api_bootstrap_init(void)
{
	int rc;

#if defined(CONFIG_SERVICE_USB_HOST_API_ENABLED)
	if (CONFIG_HOST_API_ACTIVE_INTERFACE == ACTIVE_INTERFACE_USB) {
		rc = host_api_usb_stack_init();
		if (rc != 0) {
			LOG_ERROR(LOG_MOD_HOST_API, "Host API USB initialization failed: %d\n", rc);
			return rc;
		}
	}
#endif

	rc = host_api_start_enabled_services();
	if (rc != 0) {
		return rc;
	}

	rc = host_api_start();
	if (rc != 0) {
		LOG_ERROR(LOG_MOD_HOST_API, "Host API auto-start failed: %d\n", rc);
		return rc;
	}

	return 0;
}

SYS_INIT(host_api_bootstrap_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
