/*
 * Copyright (c) 2026 Synaptics, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/syna_sl261x_clock.h>
#include <zephyr/arch/common/sys_io.h>

#define DT_DRV_COMPAT syna_sl261x_clock

#define PLL0_RATE CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC

#define GET_CLK_ID(id)		((id) & 0xFF)
#define GET_SER_ID(id)		(((id) >> SER_ID) & 0xFF)
#define GET_CTRL_REG(id)	(((id) >> CTRL_REG) & 0xFF)

struct clock_control_syna_config {
	mem_addr_t regs;
};

static inline uint32_t syna_clk_read(const struct clock_control_syna_config *config, mm_reg_t addr)
{
	return sys_read32(config->regs + addr);
}

static inline void syna_clk_write(const struct clock_control_syna_config *config, uint32_t data,
				  mm_reg_t addr)
{
	sys_write32(data, config->regs + addr);
}

static inline int syna_clk_enable(const struct device *dev, uint32_t id, int enable)
{
	const struct clock_control_syna_config *config = dev->config;
	uint32_t offset = CLK_ENABLE1;
	uint32_t clk_id = GET_CLK_ID(id);
	uint32_t ctrl = GET_CTRL_REG(id);
	uint32_t value;
	uint32_t bit_mask;

	if (ctrl) {
		value = syna_clk_read(config, ctrl);
		value |= 1;
		syna_clk_write(config, value, ctrl);
	}

	if (clk_id != NO_CLK_ID) {
		while (clk_id >= 32U) {
			offset += 4;
			clk_id -= 32U;
		}

		value = syna_clk_read(config, offset);
		bit_mask = 1U << clk_id;
		if (enable != 0) {
			value |= bit_mask;
		} else {
			value &= ~(bit_mask);
		}
		syna_clk_write(config, value, offset);
	}

	clk_id = GET_SER_ID(id);
	if (clk_id != NO_CLK_ID) {
		offset = CLK_ENABLE1;
		while (clk_id >= 32U) {
			offset += 4;
			clk_id -= 32U;
		}

		value = syna_clk_read(config, offset);
		bit_mask = 1U << clk_id;
		if (enable != 0) {
			value |= bit_mask;
		} else {
			value &= ~(bit_mask);
		}
		syna_clk_write(config, value, offset);
	}

	return 0;
}

static inline int api_on(const struct device *dev, clock_control_subsys_t clkcfg)
{
	uint32_t clock_id = (uint32_t)(uintptr_t)clkcfg;

	return syna_clk_enable(dev, clock_id, 1);
}

static inline int api_off(const struct device *dev, clock_control_subsys_t clkcfg)
{
	uint32_t clock_id = (uint32_t)(uintptr_t)clkcfg;

	return syna_clk_enable(dev, clock_id, 0);
}

static const struct clock_control_driver_api syna_clkctrl_api = {
	.on = api_on,
	.off = api_off,
};

static const struct clock_control_syna_config syna_config = {
	.regs = DT_INST_REG_ADDR(0),
};

static int syna_clkctrl_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

DEVICE_DT_INST_DEFINE(0, syna_clkctrl_init, NULL, NULL, &syna_config, PRE_KERNEL_1,
		      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &syna_clkctrl_api);
