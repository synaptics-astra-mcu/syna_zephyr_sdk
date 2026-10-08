/*
 * Copyright (C) 2026 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sl261x_usb2_phy /* Unused; defined for grep-ability */

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include "syna_usb_phy.h"

#define SL261X_USB_PHY_CTRL0		0x0
#define SL261X_USB_PHY_CTRL1		0x4
#define  PHY_USB_CTRL1_DPPULLDOWN	BIT(27)
#define  PHY_USB_CTRL1_DMPULLDOWN	BIT(28)
#define  PHY_USB_CTRL1_IDDIG		BIT(29)
#define SL261X_USB_PHY_CTRL2		0x8

struct syna_usb2_phy_config {
	uintptr_t reg_base;
	size_t reg_size;
	const struct device *clk_dev;
	const clock_control_subsys_t clk_id;
	const struct reset_dt_spec reset;
};

static int sl261x_usb2_phy_enable(const struct syna_usb_phy *phy)
{
	const struct syna_usb2_phy_config *cfg = phy->pcfg;

	clock_control_on(cfg->clk_dev, cfg->clk_id);
	reset_line_deassert_dt(&cfg->reset);
	k_busy_wait(100);

	return 0;
}

static int sl261x_usb2_phy_disable(const struct syna_usb_phy *phy)
{
	const struct syna_usb2_phy_config *cfg = phy->pcfg;

	clock_control_off(cfg->clk_dev, cfg->clk_id);

	return 0;
}

static int sl261x_usb2_phy_set_mode(const struct syna_usb_phy *phy, bool host)
{
	const struct syna_usb2_phy_config *cfg = phy->pcfg;
	mm_reg_t addr;
	uint32_t val;

	device_map(&addr, cfg->reg_base, cfg->reg_size, K_MEM_CACHE_NONE);

	val = sys_read32(addr + SL261X_USB_PHY_CTRL1);
	if (host) {
		val |= (PHY_USB_CTRL1_DPPULLDOWN | PHY_USB_CTRL1_DMPULLDOWN);
		val &= ~PHY_USB_CTRL1_IDDIG;
	} else {
		val &= ~(PHY_USB_CTRL1_DPPULLDOWN | PHY_USB_CTRL1_DMPULLDOWN);
		val |= PHY_USB_CTRL1_IDDIG;
	}
	sys_write32(val, addr + SL261X_USB_PHY_CTRL1);

	device_unmap(addr, cfg->reg_size);

	return 0;
}

#define DEFINE_PHY_SYNA_SL261X_USB2(usb_node, phy_node)						\
	static const struct syna_usb2_phy_config CONCAT(phy, DT_DEP_ORD(phy_node), _cfg) = {	\
		.reg_base = DT_REG_ADDR(phy_node),						\
		.reg_size = DT_REG_SIZE(phy_node),						\
		.clk_dev = DEVICE_DT_GET(DT_CLOCKS_CTLR(phy_node)),				\
		.clk_id = (clock_control_subsys_t)DT_CLOCKS_CELL(phy_node, clkid),		\
		.reset = RESET_DT_SPEC_GET(phy_node),						\
	};											\
	const struct syna_usb_phy USB_SYNA_PHY_PSEUDODEV_NAME(usb_node) = {			\
		.enable = sl261x_usb2_phy_enable,						\
		.disable = sl261x_usb2_phy_disable,						\
		.set_mode = sl261x_usb2_phy_set_mode,						\
		.pcfg = &CONCAT(phy, DT_DEP_ORD(phy_node), _cfg),				\
	};

/*
 * Iterate all USB nodes and instantiate PHY when appropriate.
 */
#define _FOREACH_NODE(usb_node)									\
	IF_ENABLED(1,					\
		(DEFINE_PHY_SYNA_SL261X_USB2(usb_node, USB_SYNA_PHY(usb_node))))
#define _FOREACH_COMPAT(compat) DT_FOREACH_STATUS_OKAY(compat, _FOREACH_NODE)
FOR_EACH(_FOREACH_COMPAT, (), SYNA_USB_COMPATIBLES)
