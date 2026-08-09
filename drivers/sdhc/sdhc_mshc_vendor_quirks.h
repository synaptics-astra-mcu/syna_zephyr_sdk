/*
 * Copyright (c) 2026 Synaptics, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SDHC_MSHC_VENDOR_QUIRKS_H
#define ZEPHYR_DRIVERS_SDHC_MSHC_VENDOR_QUIRKS_H

#include "sdhc_mshc.h"

#include <stdint.h>
#include <zephyr/device.h>

#if DT_HAS_COMPAT_STATUS_OKAY(syna_sl261x_emmc)

#include <zephyr/sys/sys_io.h>
#include <zephyr/kernel.h>

#define PHY_RESET	0x300
#define PHY_RXSEL	0x304
#define PHY_WEAKPULL	0x308
#define PHY_TXSLEW	0x30C
#define PHY_COMMDL_CNFG	0x31C
#define PHY_SMPLDL_CNFG	0x320
#define PHY_AT_CTRL	0x540

static int syna_sl261x_enable_clk(const struct device *dev)
{
	return 0;
}

static int syna_sl261x_phy_init(const struct device *dev)
{
	uintptr_t base = DT_REG_ADDR(DT_NODELABEL(emmc0));
	uint32_t value;
	int tmout = 1000;

	/* config PHY_CNFG, general configuration */
	value = sys_read32(base + PHY_RESET);
	value &= ~(0xff << 16);
	value |= (8 << 20) | (8 << 16);
	sys_write32(value, base + PHY_RESET);

	/* config PHY RXSEL */
	/* config PHY WEAKPULL_EN */
	/* config PHY TXSLEW_CTRL_P: TX_SLEW_P_0 (0) */
	/* config PHY TXSLEW_CTRL_N: TX_SLEW_N_3 (3) */
	value  = (1 << 16) | (1 << 0); /* SCHMITT1P8 */
	value |= (1 << 19) | (1 << 3); /* WPE_PULLUP */
	value |= (3 << 25) | (3 << 9); /* TX_SLEW_N_3 */
	sys_write32(value, base + 0x304);

	value  = (1 << 16); /* SCHMITT1P8 */
	value |= (2 << 19); /* WPE_PULLDOWN */
	value |= (3 << 25) | (3 << 9); /* TX_SLEW_N_3 */
	sys_write32(value, base + 0x308);

	value  = (1 << 0); /* SCHMITT1P8 */
	value |= (1 << 3); /* WPE_PULLUP */
	value |= (3 << 9); /* TX_SLEW_N_3 */
	sys_write32(value, base + 0x30c);

	/* wait for PHY powergood */
	value = sys_read32(base + PHY_RESET);
	while ((((value >> 1) & 0x1) == 0x0) && tmout--) {
		value = sys_read32(base + PHY_RESET);
		k_busy_wait(10);
	}

	/* De-assert PHY reset */
	value = sys_read32(base + PHY_RESET);
	value |= 1;
	sys_write32(value, base + PHY_RESET);

	/* PHY delay line settings */
	value = sys_read32(base + PHY_COMMDL_CNFG);
	value &= ~0xffff;
	sys_write32(value, base + PHY_COMMDL_CNFG);

	value = (3 << 10) | (3 << 2); /* a_inpsel_cnfg, s_inpsel_cnfg */
	sys_write32(value, base + PHY_SMPLDL_CNFG);

	value = sys_read32(base + PHY_COMMDL_CNFG);
	value |= BIT(12); /* update_dc */
	sys_write32(value, base + PHY_COMMDL_CNFG);
	value |= 127 << 16; /* cckdl_dc */
	sys_write32(value, base + PHY_COMMDL_CNFG);
	value &= ~BIT(12); /* update_dc */
	sys_write32(value, base + PHY_COMMDL_CNFG);

	/* PHY tuning */
	value = sys_read32(base + PHY_AT_CTRL);
	value |= BIT(16); /* tune_clk_stop_en */
	value |= 3 << 17; /* pre_change_dly */
	value |= 3 << 19; /* post_change_dly */
	sys_write32(value, base + PHY_AT_CTRL);

	return 0;
}

#define QUIRK_SYNA_SL261X_EMMC_DEFINE(n)					\
	const struct dwc_mshc_vendor_quirks dwc_mshc_vendor_quirks_##n = {	\
		.pre_enable = syna_sl261x_enable_clk,				\
		.post_enable = syna_sl261x_phy_init,				\
	};

DT_INST_FOREACH_STATUS_OKAY(QUIRK_SYNA_SL261X_EMMC_DEFINE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(syna_sl261x_emmc) */

/* Add next vendor quirks definition above this line */

#endif /* ZEPHYR_DRIVERS_SDHC_MSHC_VENDOR_QUIRKS_H */
