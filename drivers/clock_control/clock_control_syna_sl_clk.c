/*
 * Copyright (C) 2026 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sl_clk

#include <string.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>

struct syna_sl_clk_cfg {
	DEVICE_MMIO_ROM;
	const struct device *clk_dev[6];
	const clock_control_subsys_t clk_id[6];
};

struct syna_sl_clk_data {
	DEVICE_MMIO_RAM;
};

#define CLKEN		(1 << 0)
#define CLKPLLSEL_MASK	7
#define CLKPLLSEL_SHIFT	1
#define CLKPLLSWITCH	(1 << 4)
#define CLKSWITCH	(1 << 5)
#define CLKD3SWITCH	(1 << 6)
#define CLKSEL_MASK	7
#define CLKSEL_SHIFT	7

static uint8_t clk_div[] = {1, 2, 4, 6, 8, 12, 1, 1};

static int syna_sl_clk_on(const struct device *dev, clock_control_subsys_t sys)
{
	uint32_t id = (uintptr_t)sys;
	mm_reg_t reg = DEVICE_MMIO_GET(dev) + id * 4;
	uint32_t val;

	val = sys_read32(reg);
	if (!(val & CLKEN)) {
		val |= CLKEN;
		sys_write32(val, reg);
	}

	return 0;
}

static int syna_sl_clk_off(const struct device *dev, clock_control_subsys_t sys)
{
	uint32_t id = (uintptr_t)sys;
	mm_reg_t reg = DEVICE_MMIO_GET(dev) + id * 4;
	uint32_t val;

	val = sys_read32(reg);
	if (val & CLKEN) {
		val &= ~CLKEN;
		sys_write32(val, reg);
	}

	return 0;
}

static enum clock_control_status syna_sl_clk_get_status(const struct device *dev,
							clock_control_subsys_t sys)
{
	uint32_t id = (uintptr_t)sys;
	mm_reg_t reg = DEVICE_MMIO_GET(dev) + id * 4;
	uint32_t val;

	val = sys_read32(reg);
	val &= CLKEN;
	return val ? CLOCK_CONTROL_STATUS_ON : CLOCK_CONTROL_STATUS_OFF;
}

static int syna_sl_clk_get_rate(const struct device *dev,
				clock_control_subsys_t sys, uint32_t *rate)
{
	uint32_t id = (uintptr_t)sys;
	mm_reg_t reg = DEVICE_MMIO_GET(dev) + id * 4;
	const struct syna_sl_clk_cfg *config = dev->config;
	uint32_t val, div, parent, parent_rate;
	int ret;

	val = sys_read32(reg);
	if (val & CLKPLLSWITCH) {
		parent = (val >> CLKPLLSEL_SHIFT) & CLKPLLSEL_MASK;
		parent++;
	} else {
		parent = 0;
	}

	ret = clock_control_get_rate(config->clk_dev[parent], config->clk_id[parent], &parent_rate);
	if (ret) {
		return ret;
	}

	if (val & CLKD3SWITCH) {
		div = 3;
	} else {
		if (val & CLKSWITCH) {
			val >>= CLKSEL_SHIFT;
			val &= CLKSEL_MASK;
			div = clk_div[val];
		} else {
			div = 1;
		}
	}

	*rate = parent_rate / div;

	return 0;
}

int syna_sl_clk_configure(const struct device *dev, uint32_t id, uint32_t pllsel, uint32_t div)
{
	mm_reg_t reg = DEVICE_MMIO_GET(dev) + id * 4;
	uint32_t val;

	val = sys_read32(reg);

	if (pllsel == 0) {
		val &= ~CLKPLLSWITCH;
	} else {
		val |= CLKPLLSWITCH;
		val &= ~(CLKPLLSEL_MASK << CLKPLLSEL_SHIFT);
		val |= (pllsel - 1) << CLKPLLSEL_SHIFT;
	}

	val &= ~CLKD3SWITCH;
	if (div == 1) {
		val &= ~CLKSWITCH;
	} else if (div == 3) {
		val |= CLKD3SWITCH;
	} else {
		int i;

		for (i = 1; i < ARRAY_SIZE(clk_div); i++) {
			if (div == clk_div[i]) {
				val &= ~(CLKSEL_MASK << CLKSEL_SHIFT);
				val |= (i << CLKSEL_MASK) | CLKSWITCH;
				break;
			}
		}
		if (i >= ARRAY_SIZE(clk_div))
			return -EINVAL;
	}

	sys_write32(val, reg);

	return 0;
}

static DEVICE_API(clock_control, clock_control_syna_sl_clk_api) = {
	.on = syna_sl_clk_on,
	.off = syna_sl_clk_off,
	.get_status = syna_sl_clk_get_status,
	.get_rate = syna_sl_clk_get_rate,
};

void __weak soc_clk_init(const struct device *dev)
{
}

static int syna_sl_clk_init(const struct device *dev)
{
	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	soc_clk_init(dev);

	return 0;
}

#define SYNA_SL_PLL_CLK_INIT(idx)								\
	static struct syna_sl_clk_data clk_data##idx;						\
	static const struct syna_sl_clk_cfg clk_cfg##idx = {					\
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(idx)),						\
		.clk_dev[0] = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(idx, 0)),		\
		.clk_dev[1] = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(idx, 1)),		\
		.clk_dev[2] = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(idx, 2)),		\
		.clk_dev[3] = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(idx, 3)),		\
		.clk_dev[4] = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(idx, 4)),		\
		.clk_dev[5] = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(idx, 5)),		\
		.clk_id[0] = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(idx, 0, clkid),	\
		.clk_id[1] = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(idx, 1, clkid),	\
		.clk_id[2] = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(idx, 2, clkid),	\
		.clk_id[3] = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(idx, 3, clkid),	\
		.clk_id[4] = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(idx, 4, clkid),	\
		.clk_id[5] = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(idx, 5, clkid),	\
	};											\
	DEVICE_DT_INST_DEFINE(idx, syna_sl_clk_init, NULL, &clk_data##idx, &clk_cfg##idx,	\
			      PRE_KERNEL_1, CONFIG_CLOCK_CONTROL_SYNA_SL_CLK_INIT_PRIORITY,	\
			      &clock_control_syna_sl_clk_api);

DT_INST_FOREACH_STATUS_OKAY(SYNA_SL_PLL_CLK_INIT);
