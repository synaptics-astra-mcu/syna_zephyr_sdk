/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Compatibility shim for Synaptics SDK logging.
 *
 * - Maps vendor LOG_* macros to Zephyr printk().
 * - Defines LOG_MOD_* constants used by SDK drivers.
 * - Provides helper(s) used by the Zephyr SRSDK compat layer to implement the
 *   SRSDK function-based logger API (`logger_write()`).
 */
#ifndef SYNA_SHIM_LOGGER_H_
#define SYNA_SHIM_LOGGER_H_

#include <errno.h>
#include <stdarg.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define LOG_MOD_CSI       "CSI"

#define SYNA_LOG_PRINT(level, mod, fmt, ...)                                         \
	do {                                                                         \
		if (!k_is_in_isr()) {                                                \
			printk(level "/" mod ": " fmt, ##__VA_ARGS__);                \
		}                                                                    \
	} while (0)

#define LOG_ERROR(mod, fmt, ...)   SYNA_LOG_PRINT("E", mod, fmt, ##__VA_ARGS__)
#define LOG_WARN(mod, fmt, ...)    SYNA_LOG_PRINT("W", mod, fmt, ##__VA_ARGS__)
#define LOG_WARNING(mod, fmt, ...) SYNA_LOG_PRINT("W", mod, fmt, ##__VA_ARGS__)
#define LOG_INFO(mod, fmt, ...)    SYNA_LOG_PRINT("I", mod, fmt, ##__VA_ARGS__)

#if defined(CONFIG_SYNA_LOG_DEBUG)
#define LOG_DEBUG(mod, fmt, ...)   SYNA_LOG_PRINT("D", mod, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(mod, fmt, ...)   ((void)0)
#endif

#if defined(CONFIG_SYNA_LOG_VERBOSE)
#define LOG_VERBOSE(mod, fmt, ...) SYNA_LOG_PRINT("V", mod, fmt, ##__VA_ARGS__)
#else
#define LOG_VERBOSE(mod, fmt, ...) ((void)0)
#endif

static inline int syna_shim_vprint(const char *prefix, const char *format, va_list ap)
{
	char buffer[224];
	int len;

	if (format == NULL) {
		return -EINVAL;
	}

	if (k_is_in_isr()) {
		return 0;
	}

	len = vsnprintk(buffer, sizeof(buffer), format, ap);
	if (len < 0) {
		return len;
	}

	if (prefix != NULL) {
		printk("%s%s", prefix, buffer);
	} else {
		printk("%s", buffer);
	}

	return len;
}

int logger_write(int msg_level, int module_id, const char *format, ...);

#endif /* SYNA_SHIM_LOGGER_H_ */
