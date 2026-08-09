/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SECURITY_ARM_SECURITY_COMMON_H_
#define ZEPHYR_DRIVERS_SECURITY_ARM_SECURITY_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>
#include <cmsis_core.h>
#include <stdint.h>

#ifndef CONFIG_ARM_SECURITY_INIT_PRIORITY
#define CONFIG_ARM_SECURITY_INIT_PRIORITY 30
#endif

struct arm_addr_range {
	uint32_t base;
	uint32_t limit;
	uint32_t offset;
	uint32_t attr;
};

static inline void arm_barrier(void)
{
	__DSB();
	__ISB();
}

static inline void arm_set_bit(uint32_t addr, uint8_t bit, bool set)
{
	uint32_t v = sys_read32(addr);

	if (set) {
		v |= BIT(bit);
	} else {
		v &= ~BIT(bit);
	}
	sys_write32(v, addr);
}

#endif
