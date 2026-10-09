/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
#include <syn765x_memmap.h>
#include <cmsis_compiler.h>
#include <hw_reg_macro.h>
#include <xspi_lld.h>
#include <xspi_ctrl_registers.h>
#include <errno.h>
#include <stdio.h>

#include "con_boot_platform.h"
#include "con_boot_trace.h"



uint32_t con_boot_platform_read32(uint32_t address)
{
	return hw_reg_read(address);
}

void con_boot_platform_write32(uint32_t address, uint32_t value)
{
	hw_reg_write(address, value);
	__DMB();
}

int32_t con_boot_platform_flash_read(uint32_t offset, void *data, size_t size)
{
	xspi_lld_direct_read(XSPI_CTRL_BASE_ADDRESS, offset, (uint32_t)size, (char *)data);
	return 0;
}

int32_t con_boot_platform_file_read(const char *path, size_t offset,
                                     void *data, size_t size)
{
    FILE *file;
    size_t read_size;
    int32_t ret;
    int32_t close_ret;

    if (path == NULL || data == NULL || size == 0U) {
        CON_BOOT_TRACE("filesystem read invalid argument");
        return -EINVAL;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        ret = errno != 0 ? -errno : -EIO;
        CON_BOOT_TRACE("filesystem fopen path=%s failed error=%d", path, ret);
        return ret;
    }

    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        ret = errno != 0 ? -errno : -EIO;
        fclose(file);
        return ret;
    }

    read_size = fread(data, 1U, size, file);

    ret = read_size == size ? 0 : (ferror(file) ? -EIO : -ENODATA);

    close_ret = fclose(file) == 0 ? 0 : (errno == 0 ? -EIO : -errno);

    if (ret == 0 && close_ret != 0)
        ret = close_ret;

    if (ret != 0) {
        CON_BOOT_TRACE("filesystem fread path=%s offset=%u requested=%u actual=%u error=%d",
            path, (unsigned)offset, (unsigned)size, (unsigned)read_size, ret);
    }
    return ret;
}
