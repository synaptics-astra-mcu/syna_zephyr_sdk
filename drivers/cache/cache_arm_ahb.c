/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#if defined(CONFIG_EXTERNAL_CACHE)
#include <zephyr/drivers/cache.h>
#endif
#include <zephyr/drivers/cache/arm_ahb_cache.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/util.h>

#define DT_DRV_COMPAT arm_ahb_cache

#define ARM_CACHE_CTRL			0x10

#define ARM_CACHE_CTRL_ENABLE		BIT(0)

#define ARM_CACHE_MAINT_CTRL_ALL	0x20
#define ARM_CACHE_MAINT_CTRL_LINES	0x24

#define ARM_CACHE_TRIG_CLEAN		BIT(0)
#define ARM_CACHE_TRIG_INVALIDATE	BIT(1)

#define ARM_CACHE_MAINT_STATUS		0x28

#define ARM_CACHE_ONGOING_EN_DIS	BIT(1)
#define ARM_CACHE_ONGOING_MAINT		BIT(2)

#define ARM_CACHE_LINE_SIZE		32

#define ARM_CACHE_HWPARAMS			0x00
#define ARM_CACHE_HWPARAMS_SIZE		GENMASK(15, 8)

#define ARM_CACHE_SEC_HIT			0x300
#define ARM_CACHE_SEC_MISS			0x304
#define ARM_CACHE_SEC_STAT_CTRL		0x308
#define ARM_CACHE_SEC_STAT_ENABLE	BIT(0)
#define ARM_CACHE_SEC_STAT_DISABLE	0U
#define ARM_CACHE_SEC_STAT_RESET	BIT(1)

struct arm_cache_config {
	uintptr_t base;
};

static const struct arm_cache_config cache_cfg = {
	.base = DT_INST_REG_ADDR(0),
};

static void arm_cache_wait_maint(void)
{
	uint32_t val;

	do {
		val = sys_read32(cache_cfg.base + ARM_CACHE_MAINT_STATUS);
	} while (val & ARM_CACHE_ONGOING_MAINT);
}

static void arm_cache_wait_en_dis(void)
{
	uint32_t val;

	do {
		val = sys_read32(cache_cfg.base + ARM_CACHE_MAINT_STATUS);
	} while (val & ARM_CACHE_ONGOING_EN_DIS);
}

static void arm_cache_maint_all(uint32_t operation)
{
	barrier_dsync_fence_full();
	sys_write32(operation, cache_cfg.base + ARM_CACHE_MAINT_CTRL_ALL);
	arm_cache_wait_maint();
	barrier_dsync_fence_full();
}

int arm_ahb_cache_flush_all(void)
{
	arm_cache_maint_all(ARM_CACHE_TRIG_CLEAN);

	return 0;
}

int arm_ahb_cache_invd_all(void)
{
	arm_cache_maint_all(ARM_CACHE_TRIG_INVALIDATE);

	return 0;
}

int arm_ahb_cache_flush_and_invd_all(void)
{
	arm_cache_maint_all(ARM_CACHE_TRIG_CLEAN | ARM_CACHE_TRIG_INVALIDATE);

	return 0;
}

static void arm_cache_maint_line(uintptr_t addr, uint32_t operation)
{
	sys_write32((uint32_t)addr | operation, cache_cfg.base + ARM_CACHE_MAINT_CTRL_LINES);

	arm_cache_wait_maint();
}

static int arm_cache_maint_range(void *addr, size_t size, uint32_t operation)
{
	uintptr_t start;
	uintptr_t last;

	if (size == 0U) {
		return 0;
	}

	last = (uintptr_t)addr + size - 1U;
	if (last < (uintptr_t)addr) {
		return -EINVAL;
	}

	start = ROUND_DOWN((uintptr_t)addr, ARM_CACHE_LINE_SIZE);
	last = ROUND_DOWN(last, ARM_CACHE_LINE_SIZE);

	barrier_dsync_fence_full();
	for (;;) {
		arm_cache_maint_line(start, operation);
		if (start == last) {
			break;
		}
		start += ARM_CACHE_LINE_SIZE;
	}
	barrier_dsync_fence_full();

	return 0;
}

int arm_ahb_cache_flush_range(void *addr, size_t size)
{
	return arm_cache_maint_range(addr, size, ARM_CACHE_TRIG_CLEAN);
}

int arm_ahb_cache_invd_range(void *addr, size_t size)
{
	return arm_cache_maint_range(addr, size, ARM_CACHE_TRIG_INVALIDATE);
}

int arm_ahb_cache_flush_and_invd_range(void *addr, size_t size)
{
	return arm_cache_maint_range(addr, size,
				     ARM_CACHE_TRIG_CLEAN | ARM_CACHE_TRIG_INVALIDATE);
}

void arm_ahb_cache_enable(void)
{
	uint32_t val;

	arm_ahb_cache_invd_all();

	val = sys_read32(cache_cfg.base + ARM_CACHE_CTRL);
	val |= ARM_CACHE_CTRL_ENABLE;
	sys_write32(val, cache_cfg.base + ARM_CACHE_CTRL);

	arm_cache_wait_en_dis();
	barrier_dsync_fence_full();
	barrier_isync_fence_full();
}

void arm_ahb_cache_disable(void)
{
	uint32_t val;

	arm_ahb_cache_flush_all();

	val = sys_read32(cache_cfg.base + ARM_CACHE_CTRL);
	val &= ~ARM_CACHE_CTRL_ENABLE;
	sys_write32(val, cache_cfg.base + ARM_CACHE_CTRL);

	arm_cache_wait_en_dis();
	barrier_dsync_fence_full();
	barrier_isync_fence_full();
}

uint32_t arm_ahb_cache_size_get(void)
{
	uint32_t params = sys_read32(cache_cfg.base + ARM_CACHE_HWPARAMS);
	uint32_t size_log2 = FIELD_GET(ARM_CACHE_HWPARAMS_SIZE, params);

	return (size_log2 < 32U) ? BIT(size_log2) : 0U;
}

uint32_t arm_ahb_cache_hit_count_get(void)
{
	return sys_read32(cache_cfg.base + ARM_CACHE_SEC_HIT);
}

uint32_t arm_ahb_cache_miss_count_get(void)
{
	return sys_read32(cache_cfg.base + ARM_CACHE_SEC_MISS);
}

void arm_ahb_cache_stats_enable(void)
{
	sys_write32(ARM_CACHE_SEC_STAT_ENABLE, cache_cfg.base + ARM_CACHE_SEC_STAT_CTRL);
}

void arm_ahb_cache_stats_disable(void)
{
	sys_write32(ARM_CACHE_SEC_STAT_RESET, cache_cfg.base + ARM_CACHE_SEC_STAT_CTRL);
	sys_write32(ARM_CACHE_SEC_STAT_DISABLE, cache_cfg.base + ARM_CACHE_SEC_STAT_CTRL);
}

void arm_ahb_cache_stats_reset(void)
{
	sys_write32(ARM_CACHE_SEC_STAT_RESET, cache_cfg.base + ARM_CACHE_SEC_STAT_CTRL);
	sys_write32(ARM_CACHE_SEC_STAT_ENABLE, cache_cfg.base + ARM_CACHE_SEC_STAT_CTRL);
}

/* Preserve Zephyr's external-cache backend for boards that select it. */
#if defined(CONFIG_EXTERNAL_CACHE)
void cache_instr_enable(void)
{
	arm_ahb_cache_enable();
}

void cache_instr_disable(void)
{
	arm_ahb_cache_disable();
}

int cache_instr_flush_all(void)
{
	return arm_ahb_cache_flush_all();
}

int cache_instr_invd_all(void)
{
	return arm_ahb_cache_invd_all();
}

int cache_instr_flush_and_invd_all(void)
{
	return arm_ahb_cache_flush_and_invd_all();
}

int cache_instr_flush_range(void *addr, size_t size)
{
	return arm_ahb_cache_flush_range(addr, size);
}

int cache_instr_invd_range(void *addr, size_t size)
{
	return arm_ahb_cache_invd_range(addr, size);
}

int cache_instr_flush_and_invd_range(void *addr, size_t size)
{
	return arm_ahb_cache_flush_and_invd_range(addr, size);
}

#endif /* CONFIG_EXTERNAL_CACHE */
