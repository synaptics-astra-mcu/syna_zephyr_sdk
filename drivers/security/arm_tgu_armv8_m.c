/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arm_security_common.h"
#include <zephyr/devicetree.h>
#include <zephyr/drivers/security/arm_security.h>
#include <zephyr/dt-bindings/security/srw1500-security.h>

LOG_MODULE_REGISTER(arm_tgu, CONFIG_LOG_DEFAULT_LEVEL);

#define TGU_CFG_PRESENT BIT(31)
#define TGU_BLK_CFG_OFFSET 5U

struct arm_tgu_regs {
	volatile uint32_t ctrl;
	volatile uint32_t cfg;
	volatile uint32_t blk_max;
	volatile uint32_t blk_cfg;
	volatile uint32_t blk_idx;
	volatile uint32_t blk_lutn;
};

struct arm_tgu_region_cfg {
	uint32_t base;
	uint32_t size;
	uint32_t attr;
};

struct arm_tgu_config {
	mm_reg_t base;
	const struct arm_addr_range *ranges;
	uint8_t range_count;
	const struct arm_tgu_region_cfg *regions;
	uint8_t region_count;
};

static int tgu_find_range(const struct arm_tgu_config *cfg, uint32_t addr,
			  const struct arm_addr_range **range)
{
	for (uint8_t i = 0; i < cfg->range_count; i++) {
		if (addr >= cfg->ranges[i].base && addr <= cfg->ranges[i].limit) {
			*range = &cfg->ranges[i];
			return 0;
		}
	}
	return -EINVAL;
}

int arm_tgu_config_region(const struct device *dev, uint32_t base,
			  uint32_t limit, enum arm_sec_attr attr)
{
	const struct arm_tgu_config *cfg = dev->config;
	struct arm_tgu_regs *regs = (struct arm_tgu_regs *)cfg->base;
	const struct arm_addr_range *br;
	const struct arm_addr_range *lr;
	uint32_t block_size;
	uint32_t base_block;
	uint32_t limit_block;

	if (base > limit) {
		return -EINVAL;
	}
	if (!(regs->cfg & TGU_CFG_PRESENT)) {
		return -ENODEV;
	}
	if (tgu_find_range(cfg, base, &br) || tgu_find_range(cfg, limit, &lr) || br != lr) {
		return -ERANGE;
	}
	if (br->attr != ARM_MIXED && br->attr != (uint32_t)attr) {
		return -EPERM;
	}

	block_size = BIT(regs->blk_cfg + TGU_BLK_CFG_OFFSET);
	if ((base % block_size) || (((limit + 1U) % block_size) != 0U)) {
		return -EINVAL;
	}

	base_block = ((base - br->base) + br->offset) / block_size;
	limit_block = ((limit - br->base) + br->offset) / block_size;

	for (uint32_t word = base_block / 32U; word <= limit_block / 32U; word++) {
		uint32_t first_bit = (word == base_block / 32U) ? (base_block % 32U) : 0U;
		uint32_t last_bit = (word == limit_block / 32U) ? (limit_block % 32U) : 31U;
		uint32_t mask = GENMASK(last_bit, first_bit);
		uint32_t lut;

		if (word > regs->blk_max) {
			return -ERANGE;
		}
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

static int arm_tgu_init(const struct device *dev)
{
	const struct arm_tgu_config *cfg = dev->config;
	struct arm_tgu_regs *regs = (struct arm_tgu_regs *)cfg->base;

	if (!(regs->cfg & TGU_CFG_PRESENT)) {
		return -ENODEV;
	}
	for (uint8_t i = 0; i < cfg->region_count; i++) {
		int ret = arm_tgu_config_region(dev, cfg->regions[i].base,
			cfg->regions[i].base + cfg->regions[i].size - 1U,
			(enum arm_sec_attr)cfg->regions[i].attr);
		if (ret) {
			return ret;
		}
	}
	arm_barrier();
	return 0;
}

static const struct arm_addr_range srw1500_itcm_tgu_ranges[] = {
	{ .base = 0x10000000U, .limit = 0x1001ffffU, .offset = 0U, .attr = ARM_MIXED },
};

static const struct arm_addr_range srw1500_dtcm_tgu_ranges[] = {
	{ .base = 0x30000000U, .limit = 0x3001ffffU, .offset = 0U, .attr = ARM_MIXED },
};

#if DT_NODE_HAS_STATUS(DT_NODELABEL(tgu_itcm), okay)
static const struct arm_tgu_config srw1500_itcm_tgu_cfg = {
	.base = DT_REG_ADDR(DT_NODELABEL(tgu_itcm)),
	.ranges = srw1500_itcm_tgu_ranges,
	.range_count = ARRAY_SIZE(srw1500_itcm_tgu_ranges),
	.regions = NULL,
	.region_count = 0U,
};

DEVICE_DT_DEFINE(DT_NODELABEL(tgu_itcm), arm_tgu_init, NULL, NULL,
		 &srw1500_itcm_tgu_cfg, PRE_KERNEL_1,
		 CONFIG_ARM_SECURITY_INIT_PRIORITY, NULL);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(tgu_dtcm), okay)
static const struct arm_tgu_config srw1500_dtcm_tgu_cfg = {
	.base = DT_REG_ADDR(DT_NODELABEL(tgu_dtcm)),
	.ranges = srw1500_dtcm_tgu_ranges,
	.range_count = ARRAY_SIZE(srw1500_dtcm_tgu_ranges),
	.regions = NULL,
	.region_count = 0U,
};

DEVICE_DT_DEFINE(DT_NODELABEL(tgu_dtcm), arm_tgu_init, NULL, NULL,
		 &srw1500_dtcm_tgu_cfg, PRE_KERNEL_1,
		 CONFIG_ARM_SECURITY_INIT_PRIORITY, NULL);
#endif
