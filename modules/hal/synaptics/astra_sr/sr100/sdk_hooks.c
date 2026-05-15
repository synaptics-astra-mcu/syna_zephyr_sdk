/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Zephyr hook implementations required by the prebuilt SRSDK MIPI library.
 *
 */

#include <stdarg.h>
#include <stdint.h>

#include <cache.h>
#include <logger.h>

int logger_write(int msg_level, int module_id, const char *format, ...)
{
	va_list ap;
	int len;

	ARG_UNUSED(module_id);

	if (format == NULL) {
		return -EINVAL;
	}

	if (msg_level > 1) {
		return 0;
	}

	va_start(ap, format);
	len = syna_shim_vprint("[imgproc:E] ", format, ap);
	va_end(ap);

	return (len < 0) ? len : 0;
}

int __wrap_printf(const char *format, ...)
{
	ARG_UNUSED(format);
	return 0;
}

void cache_clean_addr(void *addr, uint32_t size)
{
	syna_shim_cache_clean(addr, size);
}

void cache_invalidate_addr(void *addr, uint32_t size)
{
	syna_shim_cache_invalidate(addr, size);
}
