/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
#include <cmsis_compiler.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>

#include "con_boot.h"
#include "con_boot_platform.h"


#define CON_BOOT_FLASH_DEVICE DEVICE_DT_GET(DT_CHOSEN(zephyr_flash))


int32_t con_boot_get_dt_region(const char *node_name, uint32_t *addr, uint32_t *size)
{
	if (node_name == NULL || addr == NULL || size == NULL || addr == size) {
		return -EINVAL;
	}

	if (strcmp(node_name, "sram_heap0") == 0) {
		*addr = DT_REG_ADDR(DT_NODELABEL(sram_heap0));
		*size = DT_REG_SIZE(DT_NODELABEL(sram_heap0));
		return 0;
	}

	/* TODO: Support additional devicetree node labels when needed. */
	return -ENOTSUP;
}

uint32_t con_boot_platform_read32(uint32_t address)
{
	return sys_read32(address);
}

void con_boot_platform_write32(uint32_t address, uint32_t value)
{
	sys_write32(value, address);
	__DMB();
}

int32_t con_boot_platform_flash_read(uint32_t offset, void *data, size_t size)
{
	const struct device *flash = CON_BOOT_FLASH_DEVICE;

	if (!device_is_ready(flash)) {
		return -ENODEV;
	}
	return flash_read(flash, offset, data, size);
}

int32_t con_boot_platform_file_read(const char *path, size_t offset,
                                     void *data, size_t size)
{
    struct fs_file_t file;
    int32_t ret;
    ssize_t read_size;
    int32_t close_ret;

    if (path == NULL || data == NULL || size == 0U)
        return -EINVAL;

    fs_file_t_init(&file);

    ret = fs_open(&file, path, FS_O_READ);
    if (ret < 0)
        return ret;

    ret = fs_seek(&file, (off_t)offset, FS_SEEK_SET);
    if (ret < 0) {
        fs_close(&file);
        return ret;
    }

    read_size = fs_read(&file, data, size);
    ret = read_size < 0 ? (int32_t)read_size :
          ((size_t)read_size == size ? 0 : -EIO);

    close_ret = fs_close(&file);
    return ret != 0 ? ret : close_ret;
}
