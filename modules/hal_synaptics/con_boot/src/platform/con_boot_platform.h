/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
#ifndef CON_BOOT_PLATFORM_H
#define CON_BOOT_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <cmsis_gcc.h>

#define CON_BOOT_ATOMIC_BLOCK(body) do { \
	uint32_t con_boot_primask = __get_PRIMASK(); \
	__disable_irq(); \
	body \
	__DSB(); \
	__set_PRIMASK(con_boot_primask); \
} while (0)

uint32_t con_boot_platform_read32(uint32_t address);
void con_boot_platform_write32(uint32_t address, uint32_t value);
int32_t con_boot_platform_flash_read(uint32_t offset, void *data, size_t size);

int32_t con_boot_platform_file_read(const char *path, size_t offset,
                                     void *data, size_t size);

#endif /* CON_BOOT_PLATFORM_H */
