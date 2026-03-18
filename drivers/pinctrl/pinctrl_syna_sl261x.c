/*
 * Copyright (c) 2026 Synaptics, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/dt-bindings/pinctrl/syna-sl261x-pinctrl.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>
#include <zephyr/arch/common/sys_io.h>

LOG_MODULE_REGISTER(sl261x, CONFIG_PINCTRL_LOG_LEVEL);

struct pinctrl_syna_controller {
	uint32_t mux;
	uint32_t cfg;
};

#define SLXXX_GET_ADDR_OR_NONE(nodelabel, field)                        \
	(DT_NODE_EXISTS(DT_NODELABEL(nodelabel)) ?                      \
	 DT_REG_ADDR_BY_NAME_OR(DT_NODELABEL(nodelabel), field, 0) : 0)

#define SLXXX_GET_PINCTRL(nodelabel) {                                  \
	SLXXX_GET_ADDR_OR_NONE(nodelabel, pinmux),                      \
	SLXXX_GET_ADDR_OR_NONE(nodelabel, pincfg) }

static const struct pinctrl_syna_controller pinctrl_syna_ctrl[] = {
	SLXXX_GET_PINCTRL(pinctrl_gbl),
};

static void pinctrl_cfg(pinctrl_soc_pin_t soc_pin, uint32_t *value,
			uint32_t mask)
{
	if (soc_pin.flags & mask) {
		*value &= ~(mask);
		*value |= soc_pin.pincfg & mask;
	}
}

static void pinctrl_configure_pin(pinctrl_soc_pin_t soc_pin)
{
	uint32_t value, reg, bit, mode, mask;
	const struct pinctrl_syna_controller *ctrl;

	ctrl = &pinctrl_syna_ctrl[SLXXX_PINMUX_CTRL(soc_pin.pinmux)];
	mask = SLXXX_PINMUX_MASK(soc_pin.pinmux);
	if (mask != 0) {
		reg = SLXXX_PINMUX_REG(soc_pin.pinmux);
		bit = SLXXX_PINMUX_BIT(soc_pin.pinmux);
		mode = SLXXX_PINMUX_MODE(soc_pin.pinmux);

		value = sys_read32(ctrl->mux + reg);
		value = (value & ~(mask << bit)) | (mode << bit);
		sys_write32(value, ctrl->mux + reg);
		LOG_DBG("pinctrl 0x%x -> 0x%x\n", value, ctrl->mux + reg);
	}

	if (soc_pin.flags != 0) {
		uint32_t cfg = SLXXX_PINMUX_CFG(soc_pin.pinmux);

		value = sys_read32(ctrl->cfg + cfg);

		pinctrl_cfg(soc_pin, &value, SLXXX_DRV_STRENGTH_MASK);
		pinctrl_cfg(soc_pin, &value, SLXXX_PULL_ENABLE_MASK);
		pinctrl_cfg(soc_pin, &value, SLXXX_INPUT_ENABLE_MASK);
		pinctrl_cfg(soc_pin, &value, SLXXX_SLEW_RATE_MASK);
		pinctrl_cfg(soc_pin, &value, SLXXX_SCHMITT_TRIG_MASK);

		sys_write32(value, ctrl->cfg + cfg);
		LOG_DBG("pincfg 0x%x -> 0x%x\n", value, ctrl->cfg + cfg);
	}
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt,
			   uintptr_t reg)
{
	ARG_UNUSED(reg);

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		pinctrl_configure_pin(*pins++);
	}

	return 0;
}
