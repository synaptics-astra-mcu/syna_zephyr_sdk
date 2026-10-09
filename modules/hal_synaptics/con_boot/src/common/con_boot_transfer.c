/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
/* Common source reading, validation, and mapped-memory transfer. */

#include "con_boot_transfer.h"
#include "con_boot_time.h"
#include "con_boot_trace.h"
#include "con_boot_platform.h"
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>


int32_t con_boot_flash_read(uint32_t offset, void *data, size_t size)
{
	if (data == NULL || size == 0U ||
	    (uint64_t)size > (uint64_t)UINT32_MAX + 1ULL - offset) {
		return -EINVAL;
	}

	return con_boot_platform_flash_read(offset, data, size);
}


static inline bool timeout_expired(uint32_t start_ms, uint32_t timeout_ms)
{
	return (uint32_t)(con_boot_time_ms() - start_ms) >= timeout_ms;
}

static int32_t validate_files(const con_boot_file_info *files, size_t count)
{
	if (files == NULL || count == 0U) {
		return -EINVAL;
	}
	for (size_t i = 0U; i < count; ++i) {
		if (files[i].source_size == 0U ||
		    (uint64_t)files[i].dest_addr + files[i].source_size > UINT32_MAX + 1ULL ||
		    files[i].source_type < CON_BOOT_SOURCE_BUFFER ||
		    files[i].source_type > CON_BOOT_SOURCE_FILESYSTEM) {
			return -EINVAL;
		}

		/* A flash source is an offset encoded as a pointer; offset zero is valid. */
		if (files[i].source == NULL && files[i].source_type != CON_BOOT_SOURCE_FLASH) {
			return -EINVAL;
		}

		uintptr_t source = (uintptr_t)files[i].source;
		if (files[i].source_type == CON_BOOT_SOURCE_BUFFER &&
		    source + files[i].source_size < source) {
			return -EINVAL;
		}

		if (files[i].source_type == CON_BOOT_SOURCE_FLASH &&
		    (source > UINT32_MAX ||
		     files[i].source_size > (uint64_t)UINT32_MAX + 1ULL - (uint64_t)source)) {
			return -EINVAL;
		}
	}
	return 0;
}

int32_t con_boot_transfer_files(const con_boot_file_info *files,
																size_t file_count, uint32_t timeout_ms)
{
	int32_t ret = validate_files(files, file_count);
	if (ret != 0) 
		return ret;

	if (timeout_ms == 0U) 
		return -EINVAL;

	uint32_t start_ms = con_boot_time_ms();

	for (size_t i = 0U; i < file_count; ++i) {
		const con_boot_file_info *file = &files[i];
		void *destination = (void *)(uintptr_t)file->dest_addr;

		CON_BOOT_TRACE("file[%u] start source_type=%u source=0x%08x size=%u dest=0x%08x",
			(unsigned)i, (unsigned)file->source_type, (unsigned)(uintptr_t)file->source,
			(unsigned)file->source_size, (unsigned)file->dest_addr);

		if (timeout_expired(start_ms, timeout_ms)) {
			ret = -ETIMEDOUT;

		} else if (file->source_type == CON_BOOT_SOURCE_FLASH) {
			ret = con_boot_flash_read((uint32_t)(uintptr_t)file->source,
				destination, file->source_size);

		} else if (file->source_type == CON_BOOT_SOURCE_FILESYSTEM) {
			ret = con_boot_platform_file_read((const char *)file->source, 0U,
				destination, file->source_size);

		} else {
			memcpy(destination, file->source, file->source_size);
			ret = 0;
		}

		if (ret != 0) {
			CON_BOOT_TRACE("file[%u] failed ret=%d", (unsigned)i, (int)ret);
			break;
		}

		CON_BOOT_TRACE("file[%u] complete", (unsigned)i);
	}

	CON_BOOT_TRACE("file transfer finished ret=%d", (int)ret);

	return ret;
}
