/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
/* Public standalone Connsys boot and direct M52 bus API. */
#ifndef CON_BOOT_H
#define CON_BOOT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum con_boot_source_type {
	CON_BOOT_SOURCE_BUFFER = 1,
	CON_BOOT_SOURCE_FLASH,
	CON_BOOT_SOURCE_FILESYSTEM,
} con_boot_source_type;

typedef struct con_boot_file_info {
	const void						*source;
	size_t								source_size;
	con_boot_source_type	source_type;
	uint32_t							dest_addr;
} con_boot_file_info;

int32_t con_boot_wlan_init(void);
int32_t con_boot_wlan_download(const con_boot_file_info *files,
                                size_t file_count, uint32_t timeout_ms);
int32_t con_boot_bt_init(void);
int32_t con_boot_bt_download(const con_boot_file_info *files,
                              size_t file_count, uint32_t timeout_ms);

/* Zephyr only: get a memory node's first reg address/size by node label.
 * Currently supports sram_heap0 only. Returns 0, -EINVAL for invalid
 * arguments, or -ENOTSUP for other node labels.
 */
int32_t con_boot_get_dt_region(const char *node_name, uint32_t *addr, uint32_t *size);

/* Common direct M52-visible bus access; no address translation is performed. */
int32_t con_boot_bus_read(uint32_t address, void *data, size_t size);
int32_t con_boot_bus_write(uint32_t address, const void *data, size_t size);
int32_t con_boot_bus_read32(uint32_t address, uint32_t *value);
int32_t con_boot_bus_write32(uint32_t address, uint32_t value);

/* Read bytes from the platform's firmware flash by offset. */
int32_t con_boot_flash_read(uint32_t offset, void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* CON_BOOT_H */
