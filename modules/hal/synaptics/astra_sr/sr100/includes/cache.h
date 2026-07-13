/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_SHIM_CACHE_H_
#define SYNA_SHIM_CACHE_H_

#include <stdint.h>

#include <zephyr/cache.h>
#include <zephyr/sys/util.h>

static inline void syna_shim_cache_clean(void *addr, uint32_t size)
{
#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
	sys_cache_data_flush_range(addr, size);
#else
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
#endif
}

static inline void syna_shim_cache_invalidate(void *addr, uint32_t size)
{
#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
	sys_cache_data_invd_range(addr, size);
#else
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
#endif
}

#endif /* SYNA_SHIM_CACHE_H_ */

