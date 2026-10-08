/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CACHE_ARM_AHB_CACHE_H_
#define ZEPHYR_INCLUDE_DRIVERS_CACHE_ARM_AHB_CACHE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Enable the ARM AHB cache used for XSPI/flash accesses. */
void arm_ahb_cache_enable(void);

/** Disable the ARM AHB cache used for XSPI/flash accesses. */
void arm_ahb_cache_disable(void);

/** Clean all lines in the ARM AHB cache. */
int arm_ahb_cache_flush_all(void);

/** Invalidate all lines in the ARM AHB cache. */
int arm_ahb_cache_invd_all(void);

/** Clean and invalidate all lines in the ARM AHB cache. */
int arm_ahb_cache_flush_and_invd_all(void);

/** Clean lines covering an address range in the ARM AHB cache. */
int arm_ahb_cache_flush_range(void *addr, size_t size);

/** Invalidate lines covering an address range in the ARM AHB cache. */
int arm_ahb_cache_invd_range(void *addr, size_t size);

/** Clean and invalidate lines covering an address range in the ARM AHB cache. */
int arm_ahb_cache_flush_and_invd_range(void *addr, size_t size);

/** Return the configured cache capacity in bytes, or zero for an invalid encoding. */
uint32_t arm_ahb_cache_size_get(void);

/** Return the secure cache-hit performance counter. */
uint32_t arm_ahb_cache_hit_count_get(void);

/** Return the secure cache-miss performance counter. */
uint32_t arm_ahb_cache_miss_count_get(void);

/** Enable the secure cache performance counters. */
void arm_ahb_cache_stats_enable(void);

/** Disable the secure cache performance counters. */
void arm_ahb_cache_stats_disable(void);

/** Reset and re-enable the secure cache performance counters. */
void arm_ahb_cache_stats_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_CACHE_ARM_AHB_CACHE_H_ */
