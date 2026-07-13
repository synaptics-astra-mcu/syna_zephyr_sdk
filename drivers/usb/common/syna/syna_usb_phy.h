/*
 * Copyright (c) 2026 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 * Inspiration from stm32_usb_common.h, which is:
 * Copyright (c) 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_USB_COMMON_SYNA_SYNA_USB_PHY_H_
#define ZEPHYR_DRIVERS_USB_COMMON_SYNA_SYNA_USB_PHY_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>

/* Compatible of all SYNA USB controllers */
#define SYNA_USB_COMPATIBLES								\
	syna_sl_usb2

/* Shorthand to obtain PHY node for an instance */
#define USB_SYNA_PHY(usb_node)			DT_PROP_BY_IDX(usb_node, phys, 0)

/* SYNA USB PHY pseudo-device */
struct syna_usb_phy {
	/**
	 * Enable PHY and apply PHY configuration.
	 *
	 * @pre The USB controller clock is enabled.
	 *
	 * @param phy PHY pseudo-device
	 * @returns 0 on success, negative error code otherwise.
	 */
	int (*enable)(const struct syna_usb_phy *phy);

	/**
	 * Disable PHY.
	 *
	 * @pre The USB controller clock is enabled.
	 *
	 * @param phy PHY pseudo-device
	 * @returns 0 on success, negative error code otherwise.
	 */
	int (*disable)(const struct syna_usb_phy *phy);

	/**
	 * Set PHY mode.
	 *
	 * @pre The USB phy is enabled.
	 *
	 * @param phy PHY pseudo-device
	 * @param host set host mode or device mode
	 * @returns 0 on success, negative error code otherwise.
	 */
	int (*set_mode)(const struct syna_usb_phy *phy, bool host);

	/**
	 * PHY-specific configuration
	 *
	 * This field is reserved for PHY pseudo-device drivers;
	 * USB controller drivers must not examine this field.
	 *
	 * a pointer to an out-of-band configuration block is saved in `pcfg`.
	 */
	const void *pcfg;
};

/*
 * Returns the name of the PHY pseudo-device for `usb_node`.
 *
 * Implementation notes:
 * Use unique DT device name suffixed with "__syna_phy".
 */
#define USB_SYNA_PHY_PSEUDODEV_NAME(usb_node)					\
	CONCAT(DEVICE_DT_NAME_GET(usb_node), __syna_phy)

/* Forward declare all PHY pseudo-devices */
#define _SYNA_USB_PHY_PSEUDODEV_DECLARE(usb_node)				\
	extern const struct syna_usb_phy USB_SYNA_PHY_PSEUDODEV_NAME(usb_node);
#define _SYNA_USB_DECLARE_ALL_PHYS_OF_COMPAT(compat)				\
	DT_FOREACH_STATUS_OKAY(compat, _SYNA_USB_PHY_PSEUDODEV_DECLARE)
FOR_EACH(_SYNA_USB_DECLARE_ALL_PHYS_OF_COMPAT, (), SYNA_USB_COMPATIBLES)
/* End of PHY pseudo-devices declaration */

#endif /* ZEPHYR_DRIVERS_USB_COMMON_SYNA_SYNA_USB_PHY_H_ */
