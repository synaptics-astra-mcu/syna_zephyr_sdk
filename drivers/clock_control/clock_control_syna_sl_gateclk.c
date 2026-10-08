/*
 * Copyright (C) 2026 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sl_gateclk

#include <string.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>

struct syna_sl_gateclk_cfg {
	DEVICE_MMIO_ROM;
};

struct syna_sl_gateclk_data {
	DEVICE_MMIO_RAM;
};

static int syna_sl_gateclk_on(const struct device *dev, clock_control_subsys_t sys)
{
	uint32_t id = (uintptr_t)sys;
	mm_reg_t reg = DEVICE_MMIO_GET(dev);
	uint32_t val;

	val = sys_read32(reg);
	if (!(val & (1 << id))) {
		val |= (1 << id);
		sys_write32(val, reg);
	}

	return 0;
}

static int syna_sl_gateclk_off(const struct device *dev, clock_control_subsys_t sys)
{
	uint32_t id = (uintptr_t)sys;
	mm_reg_t reg = DEVICE_MMIO_GET(dev);
	uint32_t val;

	val = sys_read32(reg);
	if (val & (1 << id)) {
		val &= ~(1 << id);
		sys_write32(val, reg);
	}

	return 0;
}

static enum clock_control_status syna_sl_gateclk_get_status(const struct device *dev,
							    clock_control_subsys_t sys)
{
	uint32_t id = (uintptr_t)sys;
	mm_reg_t reg = DEVICE_MMIO_GET(dev);
	uint32_t val;

	val = sys_read32(reg);
	val &= (1 << id);
	return val ? CLOCK_CONTROL_STATUS_ON : CLOCK_CONTROL_STATUS_OFF;
}

static DEVICE_API(clock_control, clock_control_syna_sl_gateclk_api) = {
	.on = syna_sl_gateclk_on,
	.off = syna_sl_gateclk_off,
	.get_status = syna_sl_gateclk_get_status,
};

static int syna_sl_gateclk_init(const struct device *dev)
{
	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	return 0;
}

#define SYNA_SL_PLL_CLK_INIT(idx)								\
	static struct syna_sl_gateclk_data clk_data##idx;					\
	static const struct syna_sl_gateclk_cfg clk_cfg##idx = {				\
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(idx)),						\
	};											\
	DEVICE_DT_INST_DEFINE(idx, syna_sl_gateclk_init, NULL, &clk_data##idx, &clk_cfg##idx,	\
			      PRE_KERNEL_1, CONFIG_CLOCK_CONTROL_SYNA_SL_CLK_INIT_PRIORITY,	\
			      &clock_control_syna_sl_gateclk_api);

DT_INST_FOREACH_STATUS_OKAY(SYNA_SL_PLL_CLK_INIT);
