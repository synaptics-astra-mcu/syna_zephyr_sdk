/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/syna_srw1500_clock.h>
#include <zephyr/arch/common/sys_io.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#define DT_DRV_COMPAT syna_srw1500_clock

LOG_MODULE_REGISTER(clock_control_syna_srw1500, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

#define PLL0_RATE 						CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC
#define SRW1500_CLK_MCU_PLL_RATE_HZ  	PLL0_RATE
#define SRW1500_XTAL_CLK_RATE_HZ     	48000000U
#define SRW1500_CLK_REF_RATE_HZ      	(SRW1500_XTAL_CLK_RATE_HZ / 2U)
#define SRW1500_DIV_MIN       			1U
#define SRW1500_DIV_MAX       			255U
#define SRW1500_RATE_SLOT_NUM 			256U

#define SRW1500_PERIPH_DIV_SHIFT       2U
#define SRW1500_PERIPH_DIV_MASK        0xFU
#define SRW1500_ADC_DIV_MASK           0xFFU
#define SRW1500_PDM_DIV_MASK           0x1FFU

/*
 * Guarded UART1 divider encoding mode.
 * 0: generic n-1 encoding (register value 0 => divide by 1)
 * 1: direct encoding  (register value 1 => divide by 1)
 *
 * SRW1500 UART1 uses standard n-1 encoding like all other peripheral
 * dividers on this platform family.
 */
#define SRW1500_UART1_DIRECT_DIV_ENCODING 0

#define GET_CLK_ID(id)				((id) & 0xFF)
#define GET_SER_ID(id)				(((id) >> SER_ID) & 0xFF)
#define GET_CTRL_REG(id)			(((id) >> CTRL_REG) & 0xFF)

struct clock_control_syna_config {
	mem_addr_t regs;
	mem_addr_t regs_sec;
};

struct clock_control_syna_data {
	uint32_t ref_rate[SRW1500_RATE_SLOT_NUM];
	uint16_t divider[SRW1500_RATE_SLOT_NUM];
	bool valid[SRW1500_RATE_SLOT_NUM];
};

static bool syna_set_rate_boot_ready;

#ifndef SYNA_SECURE_CLK
#define SYNA_SECURE_CLK 0U
#endif

static inline mem_addr_t syna_clk_base(const struct clock_control_syna_config *config, uint32_t id)
{
	if ((id & SYNA_SECURE_CLK) != 0U) {
		return config->regs_sec;
	}

	return config->regs;
}

static uint32_t syna_rate_slot(uint32_t clock_id)
{
	uint32_t slot = GET_CLK_ID(clock_id);

	if (slot != NO_CLK_ID) {
		return slot;
	}

	slot = GET_SER_ID(clock_id);
	if (slot != NO_CLK_ID) {
		return slot;
	}

	return 0U;
}

static bool syna_calc_divider(uint32_t ref_rate, uint32_t req_rate,
			      uint16_t max_div, uint16_t *divider)
{
	uint32_t div;

	if ((req_rate == 0U) || (ref_rate % req_rate != 0U)) {
		return false;
	}

	div = ref_rate / req_rate;
	if ((div < SRW1500_DIV_MIN) || (div > max_div)) {
		return false;
	}

	*divider = (uint16_t)div;
	return true;
}

static uint32_t syna_div_mask_for_ctrl(uint32_t ctrl)
{
	if (ctrl == ADC_CTRL) {
		return SRW1500_ADC_DIV_MASK;
	}

	if (ctrl == PDM_CTRL) {
		return SRW1500_PDM_DIV_MASK;
	}

	return SRW1500_PERIPH_DIV_MASK;
}

static bool syna_valid_ctrl_reg(uint32_t ctrl)
{
	if ((ctrl < UART1_CTRL) || (ctrl > REF_CALIB_CTRL)) {
		return false;
	}

	return ((ctrl & 0x3U) == 0U);
}

static bool syna_rate_set_supported(uint32_t clock_id)
{
	switch (clock_id) {
	case SYNA_UART1_CLK:
	case SYNA_UART2_CLK:
	case SYNA_UART3_CLK:
	case SYNA_GPIO_CLK:
	case SYNA_I2C0_CLK:
	case SYNA_I2C1_CLK:
	case SYNA_SPI0_CLK:
	case SYNA_SPI1_CLK:
	case SYNA_I3C_CLK:
	case SYNA_XSPI_CLK:
	case SYNA_CAN0_CLK:
	case SYNA_CAN1_CLK:
		return true;
	default:
		return false;
	}
}

static bool syna_direct_div_encoding(uint32_t clock_id)
{
	return (clock_id == SYNA_UART1_CLK) && (SRW1500_UART1_DIRECT_DIV_ENCODING != 0);
}

static uint16_t syna_hw_periph_div(const struct clock_control_syna_config *config,
					  uint32_t clock_id)
{
	uint32_t ctrl = GET_CTRL_REG(clock_id);
	mem_addr_t base = syna_clk_base(config, clock_id);
	uint32_t value;
	uint32_t raw_div;
	uint32_t div_mask;
	uint16_t div;

	if (!syna_valid_ctrl_reg(ctrl)) {
		return 1U;
	}

	div_mask = syna_div_mask_for_ctrl(ctrl);

	value = sys_read32(base + ctrl);
	raw_div = (value >> SRW1500_PERIPH_DIV_SHIFT) & div_mask;

	if (syna_direct_div_encoding(clock_id)) {
		div = (uint16_t)raw_div;
	} else {
		div = (uint16_t)(raw_div + 1U);
	}

	if (div == 0U) {
		return 1U;
	}

	return div;
}

static int syna_hw_set_periph_div(const struct clock_control_syna_config *config,
					 uint32_t clock_id, uint16_t div)
{
	uint32_t ctrl = GET_CTRL_REG(clock_id);
	mem_addr_t base = syna_clk_base(config, clock_id);
	uint32_t value;
	uint32_t readback;
	uint32_t old_value;
	uint32_t div_mask;
	uint32_t encoded_div;
	bool direct_encoding;

	if (!syna_valid_ctrl_reg(ctrl)) {
		return -ENOTSUP;
	}

	div_mask = syna_div_mask_for_ctrl(ctrl);
	direct_encoding = syna_direct_div_encoding(clock_id);

	if (direct_encoding) {
		if ((div == 0U) || ((uint32_t)div > div_mask)) {
			return -EINVAL;
		}
	} else if ((div == 0U) || ((uint32_t)div > (div_mask + 1U))) {
		return -EINVAL;
	}

	if (direct_encoding) {
		encoded_div = (uint32_t)div;
	} else {
		encoded_div = (uint32_t)div - 1U;
	}
	old_value = sys_read32(base + ctrl);
	value = old_value;
	value &= ~(div_mask << SRW1500_PERIPH_DIV_SHIFT);
	value |= (encoded_div & div_mask) << SRW1500_PERIPH_DIV_SHIFT;
	sys_write32(value, base + ctrl);

	readback = sys_read32(base + ctrl);
	if (((readback >> SRW1500_PERIPH_DIV_SHIFT) & div_mask) !=
	    (encoded_div & div_mask)) {
		/* Roll back to previous value if write did not stick. */
		sys_write32(old_value, base + ctrl);
		return -EIO;
	}

	return 0;
}

static uint32_t syna_pll_parent_rate(uint32_t clock_id)
{
	if (clock_id == SYNA_UART0_CLK) {
		return SRW1500_CLK_REF_RATE_HZ;
	}

	return SRW1500_CLK_MCU_PLL_RATE_HZ;
}

static inline uint32_t syna_clk_read(mem_addr_t base, mm_reg_t addr)
{
	return sys_read32(base + addr);
}

static inline void syna_clk_write(mem_addr_t base, uint32_t data, mm_reg_t addr)
{
	sys_write32(data, base + addr);
}

static inline int syna_clk_enable(const struct device *dev, uint32_t id, int enable)
{
	const struct clock_control_syna_config *config = dev->config;
	mem_addr_t base = syna_clk_base(config, id);
	uint32_t offset = CLK_ENABLE1;
	uint32_t clk_id = GET_CLK_ID(id);
	uint32_t ctrl = GET_CTRL_REG(id);
	uint32_t value;
	uint32_t bit_mask;

	if (ctrl) {
		value = syna_clk_read(base, ctrl);
		if (enable != 0) {
			value |= 1;
		} else {
			value &= ~1;
		}
		syna_clk_write(base, value, ctrl);
	}

	if (clk_id != NO_CLK_ID) {
		while (clk_id >= 32U) {
			offset += 4;
			clk_id -= 32U;
		}

		value = syna_clk_read(base, offset);
		bit_mask = 1U << clk_id;
		if (enable != 0) {
			value |= bit_mask;
		} else {
			value &= ~(bit_mask);
		}
		syna_clk_write(base, value, offset);
	}

	clk_id = GET_SER_ID(id);
	if (clk_id != NO_CLK_ID) {
		offset = CLK_ENABLE1;
		while (clk_id >= 32U) {
			offset += 4;
			clk_id -= 32U;
		}

		value = syna_clk_read(base, offset);
		bit_mask = 1U << clk_id;
		if (enable != 0) {
			value |= bit_mask;
		} else {
			value &= ~(bit_mask);
		}
		syna_clk_write(base, value, offset);
	}

	return 0;
}

static int syna_clk_get_rate(const struct device *dev, uint32_t clock_id)
{
	const struct clock_control_syna_config *config = dev->config;
	struct clock_control_syna_data *data = dev->data;
	uint32_t slot = syna_rate_slot(clock_id);
 	uint32_t ref_rate = syna_pll_parent_rate(clock_id);
	uint16_t div = syna_hw_periph_div(config, clock_id);
	uint32_t rate;

	if (div == 0U) {
		div = 1U;
	}

	data->ref_rate[slot] = ref_rate;
	data->divider[slot] = div;
	data->valid[slot] = true;

	rate = data->ref_rate[slot] / data->divider[slot];

	return rate;
}

static int syna_clk_set_rate(const struct device *dev, uint32_t clock_id,
			clock_control_subsys_rate_t rate)
{
	const struct clock_control_syna_config *config = dev->config;
	struct clock_control_syna_data *data = dev->data;
	uint32_t req_rate = (uint32_t)(uintptr_t)rate;
	uint32_t pll_parent_rate = syna_pll_parent_rate(clock_id);
	uint32_t ctrl = GET_CTRL_REG(clock_id);
	uint32_t slot = syna_rate_slot(clock_id);
	uint32_t div_mask;
	uint16_t max_div;
	uint16_t selected_div;
	int ret;

	/* Boot/runtime guard */
	if (!syna_set_rate_boot_ready || k_is_pre_kernel() || k_is_in_isr()) {
		return -EAGAIN;
	}

	/* UART0 is fixed to ref clock path */
	if (clock_id == SYNA_UART0_CLK) {
		if (req_rate != SRW1500_CLK_REF_RATE_HZ) {
			return -ENOTSUP;
		}
		data->ref_rate[slot] = SRW1500_CLK_REF_RATE_HZ;
		data->divider[slot] = 1U;
		data->valid[slot] = true;
		return 0;
	}

	/* Validate clock and control register */
	if (!syna_rate_set_supported(clock_id) || !syna_valid_ctrl_reg(ctrl)) {
		return -ENOTSUP;
	}

	/* Get divider constraints */
	div_mask = syna_div_mask_for_ctrl(ctrl);
	max_div = syna_direct_div_encoding(clock_id) ? 
	          (uint16_t)div_mask : (uint16_t)(div_mask + 1U);

	/* Select divider: direct value or calculated from rate */
	if ((req_rate >= SRW1500_DIV_MIN) && (req_rate <= max_div)) {
		/* Dynamic divider mode: treat value as divider directly */
		selected_div = (uint16_t)req_rate;
	} else {
		/* Calculate divider from requested rate */
		if (!syna_calc_divider(pll_parent_rate, req_rate, max_div, &selected_div)) {
			return -EINVAL;
		}
	}

	/* Write to hardware with verification */
	ret = syna_hw_set_periph_div(config, clock_id, selected_div);
	if (ret != 0) {
		return ret;
	}

	/* Update driver data only after successful hardware write */
	data->ref_rate[slot] = pll_parent_rate;
	data->divider[slot] = selected_div;
	data->valid[slot] = true;

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

static int api_get_rate(const struct device *dev, clock_control_subsys_t clkcfg, uint32_t *rate)
{
	uint32_t clock_id = (uint32_t)(uintptr_t)clkcfg;
	uint32_t clk_rate;

	clk_rate = syna_clk_get_rate(dev, clock_id);
	if (clk_rate == 0) {
		return -EINVAL;
	}

	*rate = clk_rate;

	return 0;
}

static int api_set_rate(const struct device *dev, clock_control_subsys_t clkcfg,
			clock_control_subsys_rate_t rate)
{
	uint32_t clock_id = (uint32_t)(uintptr_t)clkcfg;

	return syna_clk_set_rate(dev, clock_id, rate);
}

static const struct clock_control_driver_api syna_clkctrl_api = {
	.on = api_on,
	.off = api_off,
	.get_rate = api_get_rate,
	.set_rate = api_set_rate,
};

static const struct clock_control_syna_config syna_config = {
	.regs = DT_INST_REG_ADDR_BY_NAME(0, global),
	.regs_sec = DT_INST_REG_ADDR_BY_NAME(0, global_sec),
};

static struct clock_control_syna_data syna_data = {
	.ref_rate = {[0 ... SRW1500_RATE_SLOT_NUM - 1] = SRW1500_CLK_MCU_PLL_RATE_HZ},
	.divider = {[0 ... SRW1500_RATE_SLOT_NUM - 1] = 1U},
	.valid = {[0 ... SRW1500_RATE_SLOT_NUM - 1] = true},
};

static int syna_clkctrl_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static int syna_set_rate_guard_init(void)
{
	syna_set_rate_boot_ready = true;
	return 0;
}

SYS_INIT(syna_set_rate_guard_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

DEVICE_DT_INST_DEFINE(0, syna_clkctrl_init, NULL, &syna_data, &syna_config, PRE_KERNEL_1,
		      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &syna_clkctrl_api);
