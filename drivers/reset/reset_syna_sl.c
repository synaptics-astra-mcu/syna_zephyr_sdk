/*
 * Copyright (C) 2026 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sl_reset

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

#define SYNA_SL_RESET_OFFSET(id)		(((id) >> 8U) & 0xffffu)
#define SYNA_SL_RESET_STICKY(id)		((id & 0x80u))
#define SYNA_SL_RESET_BIT(id)			((id & 0x3fu))

struct reset_syna_sl_config {
	DEVICE_MMIO_ROM;
};

struct reset_syna_sl_data {
	DEVICE_MMIO_RAM;
};

static struct k_spinlock lock;

static int reset_syna_sl_status(const struct device *dev, uint32_t id, uint8_t *status)
{
	mm_reg_t base = DEVICE_MMIO_GET(dev);
	uint32_t offset = SYNA_SL_RESET_OFFSET(id);

	if (!SYNA_SL_RESET_STICKY(id))
		return -EINVAL;

	k_spinlock_key_t key = k_spin_lock(&lock);
	*status = !sys_test_bit(base + offset, SYNA_SL_RESET_BIT(id));
	k_spin_unlock(&lock, key);

	return 0;
}

static int reset_syna_sl_line_set(const struct device *dev, uint32_t id, bool assert)
{
	uintptr_t base = DEVICE_MMIO_GET(dev);
	uint32_t offset = SYNA_SL_RESET_OFFSET(id);

	if (!SYNA_SL_RESET_STICKY(id))
		return -EINVAL;

	k_spinlock_key_t key = k_spin_lock(&lock);
	if (assert)
		sys_clear_bit(base + offset, SYNA_SL_RESET_BIT(id));
	else
		sys_set_bit(base + offset, SYNA_SL_RESET_BIT(id));
	k_spin_unlock(&lock, key);

	return 0;
}

static int reset_syna_sl_line_assert(const struct device *dev, uint32_t id)
{
	return reset_syna_sl_line_set(dev, id, true);
}

static int reset_syna_sl_line_deassert(const struct device *dev, uint32_t id)
{
	return reset_syna_sl_line_set(dev, id, false);
}

static int reset_syna_sl_line_toggle(const struct device *dev, uint32_t id)
{
	mm_reg_t base = DEVICE_MMIO_GET(dev);

	if (SYNA_SL_RESET_STICKY(id)) {
		uint32_t offset = SYNA_SL_RESET_OFFSET(id);

		sys_write32(BIT(SYNA_SL_RESET_BIT(id)), base + offset);
		return 0;
	}

	reset_syna_sl_line_set(dev, id, true);
	reset_syna_sl_line_set(dev, id, false);

	return 0;
}

static int reset_syna_sl_init(const struct device *dev)
{
	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	return 0;
}

static DEVICE_API(reset, reset_syna_sl_api) = {
	.status = reset_syna_sl_status,
	.line_assert = reset_syna_sl_line_assert,
	.line_deassert = reset_syna_sl_line_deassert,
	.line_toggle = reset_syna_sl_line_toggle
};

#define SYNA_RESET_INIT(n)							\
	static struct reset_syna_sl_data reset_syna_sl_data_##n;		\
	static const struct reset_syna_sl_config reset_syna_sl_config_##n = {	\
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(n)),				\
	};									\
										\
	DEVICE_DT_INST_DEFINE(n,						\
			reset_syna_sl_init,					\
			NULL,							\
			&reset_syna_sl_data_##n,				\
			&reset_syna_sl_config_##n,				\
			PRE_KERNEL_1,						\
			CONFIG_RESET_INIT_PRIORITY,				\
			&reset_syna_sl_api);

DT_INST_FOREACH_STATUS_OKAY(SYNA_RESET_INIT);
