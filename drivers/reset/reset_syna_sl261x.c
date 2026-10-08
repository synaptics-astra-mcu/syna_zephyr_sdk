/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sl261x_reset

#include <zephyr/arch/common/sys_io.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/dt-bindings/reset/syna_sl261x_reset.h>

struct reset_syna_config {
	uint32_t base;
};

static int syna_reset_status(const struct device *dev, uint32_t id, uint8_t *status)
{
	const struct reset_syna_config *config = dev->config;
	uint32_t val;
	uint32_t rst_reg;
	uint32_t rst_status = 0;

	rst_reg = (id >> RST_REG) & 0xff;
	if (rst_reg) {
		uint32_t rst_id = (id >> RST_BIT) & 0x1f;
		uint32_t rst_mask = 1 << rst_id;

		val = sys_read32(config->base + rst_reg);
		if (!(val & rst_mask)) {
			rst_status = 1;
		}
	}

	*status = rst_status;

	return 0;
}

static int syna_reset_line_set(const struct device *dev, uint32_t id,
			       uint32_t assert)
{
	const struct reset_syna_config *config = dev->config;
	uint32_t val;
	uint32_t rst_reg;

	rst_reg = (id >> RST_REG) & 0xff;
	if (rst_reg) {
		uint32_t rst_id = (id >> RST_BIT) & 0x1f;
		uint32_t rst_mask = 1 << rst_id;

		val = sys_read32(config->base + rst_reg);
		if (assert) {
			val &= ~rst_mask;
		} else {
			val |= rst_mask;
		}
		sys_write32(val, config->base + rst_reg);

		if ((id & RST_NEXT_BIT) || (id & RST_NEXT_BITS(1))) {
			uint32_t mask = 1;

			rst_id++;
			if (rst_id >= 32) {
				rst_reg += 4;
				rst_id -= 32;
			}

			if (id & RST_NEXT_BITS(RST_NEXT_BITS_MASK)) {
				mask = (id >> RST_NEXT_BITS_SHIFT) & RST_NEXT_BITS_MASK;
			}

			rst_mask = mask << rst_id;
			val = sys_read32(config->base + rst_reg);
			if (assert) {
				val &= ~rst_mask;
			} else {
				val |= rst_mask;
			}
			sys_write32(val, config->base + rst_reg);
		}
	}

	return 0;
}

static int syna_reset_line_assert(const struct device *dev, uint32_t id)
{
	return syna_reset_line_set(dev, id, 1);
}

static int syna_reset_line_deassert(const struct device *dev, uint32_t id)
{
	return syna_reset_line_set(dev, id, 0);
}

static int syna_reset_line_toggle(const struct device *dev, uint32_t id)
{
	int ret;

	ret = syna_reset_line_set(dev, id, 1);
	if (ret == 0) {
		ret = syna_reset_line_set(dev, id, 0);
	}

	return ret;
}

static DEVICE_API(reset, syna_reset_api) = {
	.status = syna_reset_status,
	.line_assert = syna_reset_line_assert,
	.line_deassert = syna_reset_line_deassert,
	.line_toggle = syna_reset_line_toggle
};

#define SYNA_RESET_INIT(n)                                                      \
	static const struct reset_syna_config reset_syna_cfg_##n = {            \
		.base = DT_INST_REG_ADDR(n),                                    \
	};                                                                      \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &reset_syna_cfg_##n,         \
			      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, \
			      &syna_reset_api);

DT_INST_FOREACH_STATUS_OKAY(SYNA_RESET_INIT)
