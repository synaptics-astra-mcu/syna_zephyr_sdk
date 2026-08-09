/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arm_security_common.h"
#include <zephyr/devicetree.h>
#include <zephyr/drivers/security/arm_security.h>
#include <zephyr/dt-bindings/security/srw1500-security.h>

LOG_MODULE_REGISTER(arm_mpc_sie, CONFIG_LOG_DEFAULT_LEVEL);

#define MPC_CTRL_SEC_LOCK_DOWN BIT(31)
#define MPC_BLK_CFG_OFFSET 5U

struct arm_mpc_regs {
	volatile uint32_t ctrl;
	volatile uint32_t reserved0[3];
	volatile uint32_t blk_max;
	volatile uint32_t blk_cfg;
	volatile uint32_t blk_idx;
	volatile uint32_t blk_lutn;
	volatile uint32_t int_stat;
	volatile uint32_t int_clear;
	volatile uint32_t int_en;
	volatile uint32_t int_info1;
	volatile uint32_t int_info2;
	volatile uint32_t int_set;
};

struct arm_mpc_region_cfg {
	uint32_t base;
	uint32_t size;
	uint32_t attr;
};

struct arm_mpc_config {
	mm_reg_t base;
	const struct arm_addr_range *ranges;
	uint8_t range_count;
	const struct arm_mpc_region_cfg *regions;
	uint8_t region_count;
	bool lockdown;
};

static int arm_mpc_find_range(const struct arm_mpc_config *cfg,
			       uint32_t addr, const struct arm_addr_range **range)
{
	for (uint8_t i = 0; i < cfg->range_count; i++) {
		if (addr >= cfg->ranges[i].base && addr <= cfg->ranges[i].limit) {
			*range = &cfg->ranges[i];
			return 0;
		}
	}

	return -EINVAL;
}

int arm_mpc_config_region(const struct device *dev, uint32_t base,
			  uint32_t limit, enum arm_sec_attr attr)
{
	const struct arm_mpc_config *cfg = dev->config;
	struct arm_mpc_regs *regs = (struct arm_mpc_regs *)cfg->base;
	const struct arm_addr_range *br;
	const struct arm_addr_range *lr;
	uint32_t block_size;
	uint32_t base_block;
	uint32_t limit_block;
	uint32_t first_word;
	uint32_t last_word;

	if (base > limit) {
		return -EINVAL;
	}
	if (arm_mpc_find_range(cfg, base, &br) || arm_mpc_find_range(cfg, limit, &lr)) {
		return -ERANGE;
	}
	if (br != lr) {
		return -EINVAL;
	}
	if (br->attr != ARM_MIXED && br->attr != (uint32_t)attr) {
		return -EPERM;
	}

	block_size = BIT(regs->blk_cfg + MPC_BLK_CFG_OFFSET);
	if ((base % block_size) || (((limit + 1U) % block_size) != 0U)) {
		return -EINVAL;
	}

	base_block = ((base - br->base) + br->offset) / block_size;
	limit_block = ((limit - br->base) + br->offset) / block_size;
	first_word = base_block / 32U;
	last_word = limit_block / 32U;

	if (first_word > regs->blk_max || last_word > regs->blk_max) {
		return -ERANGE;
	}

	for (uint32_t word = first_word; word <= last_word; word++) {
		uint32_t lut;
		uint32_t first_bit = (word == first_word) ? (base_block % 32U) : 0U;
		uint32_t last_bit = (word == last_word) ? (limit_block % 32U) : 31U;
		uint32_t mask = GENMASK(last_bit, first_bit);

		regs->blk_idx = word;
		lut = regs->blk_lutn;
		if (attr == ARM_NONSECURE) {
			lut |= mask;
		} else {
			lut &= ~mask;
		}
		regs->blk_lutn = lut;
	}

	return 0;
}

int arm_mpc_lockdown(const struct device *dev)
{
	const struct arm_mpc_config *cfg = dev->config;
	struct arm_mpc_regs *regs = (struct arm_mpc_regs *)cfg->base;

	regs->ctrl |= MPC_CTRL_SEC_LOCK_DOWN;
	return 0;
}

static int arm_mpc_init(const struct device *dev)
{
	const struct arm_mpc_config *cfg = dev->config;

	for (uint8_t i = 0; i < cfg->region_count; i++) {
		const struct arm_mpc_region_cfg *r = &cfg->regions[i];
		int ret;

		if (r->size == 0U) {
			continue;
		}
		ret = arm_mpc_config_region(dev, r->base, r->base + r->size - 1U,
						 (enum arm_sec_attr)r->attr);
		if (ret) {
			LOG_ERR("MPC region 0x%08x size 0x%x failed: %d", r->base, r->size, ret);
			return ret;
		}
	}

	if (cfg->lockdown) {
		arm_mpc_lockdown(dev);
	}
	arm_barrier();
	return 0;
}

#if DT_NODE_HAS_STATUS(DT_NODELABEL(mpc_sram), okay)
static const struct arm_addr_range srw1500_mpc_sram_ranges[] = {
	{ .base = 0x32000000U, .limit = 0x320fffffU, .offset = 0U, .attr = ARM_SECURE },
	{ .base = 0x22000000U, .limit = 0x220fffffU, .offset = 0U, .attr = ARM_NONSECURE },
};

static const struct arm_mpc_region_cfg srw1500_mpc_sram_regions[] = {
	{ .base = 0x220a0000U, .size = 0x00050000U, .attr = ARM_NONSECURE },
};
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(mpc_xspi), okay)
static const struct arm_addr_range srw1500_mpc_xspi_ranges[] = {
	{ .base = 0x38000000U, .limit = 0x3fffffffU, .offset = 0U, .attr = ARM_SECURE },
	{ .base = 0x28000000U, .limit = 0x2fffffffU, .offset = 0U, .attr = ARM_NONSECURE },
};
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(mpc_sram), okay)
static const struct arm_mpc_config srw1500_mpc_sram_cfg = {
	.base = DT_REG_ADDR(DT_NODELABEL(mpc_sram)),
	.ranges = srw1500_mpc_sram_ranges,
	.range_count = ARRAY_SIZE(srw1500_mpc_sram_ranges),
	.regions = srw1500_mpc_sram_regions,
	.region_count = ARRAY_SIZE(srw1500_mpc_sram_regions),
	.lockdown = DT_PROP_OR(DT_NODELABEL(mpc_sram), lockdown, false),
};

DEVICE_DT_DEFINE(DT_NODELABEL(mpc_sram), arm_mpc_init, NULL, NULL,
		 &srw1500_mpc_sram_cfg, PRE_KERNEL_1,
		 CONFIG_ARM_SECURITY_INIT_PRIORITY, NULL);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(mpc_xspi), okay)
static const struct arm_mpc_config srw1500_mpc_xspi_cfg = {
	.base = DT_REG_ADDR(DT_NODELABEL(mpc_xspi)),
	.ranges = srw1500_mpc_xspi_ranges,
	.range_count = ARRAY_SIZE(srw1500_mpc_xspi_ranges),
	.regions = NULL,
	.region_count = 0U,
	.lockdown = DT_PROP_OR(DT_NODELABEL(mpc_xspi), lockdown, false),
};

DEVICE_DT_DEFINE(DT_NODELABEL(mpc_xspi), arm_mpc_init, NULL, NULL,
		 &srw1500_mpc_xspi_cfg, PRE_KERNEL_1,
		 CONFIG_ARM_SECURITY_INIT_PRIORITY, NULL);
#endif
