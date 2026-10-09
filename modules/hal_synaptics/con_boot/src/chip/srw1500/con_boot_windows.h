/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
#ifndef CON_BOOT_WINDOWS_H
#define CON_BOOT_WINDOWS_H

#include <stddef.h>
#include <stdint.h>

typedef struct con_boot_window{
	uint8_t		index;
	uint32_t	aperture;
	uint32_t 	size;
	uint32_t 	target;
} con_boot_window;

typedef struct con_boot_window_config{
	const con_boot_window *mcu2css;
	size_t								mcu2css_count;
	const con_boot_window *css2mcu;
	size_t								css2mcu_count;
} con_boot_window_config;

int32_t con_boot_configure_windows(const con_boot_window_config *config);

#endif /* CON_BOOT_WINDOWS_H */
