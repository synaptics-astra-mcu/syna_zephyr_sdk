/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
/* Common word-oriented bus transfers. */
#include "con_boot.h"
#include "con_boot_platform.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>

int32_t con_boot_bus_read(uint32_t address, void *data, size_t size)
{
	uint8_t *dst = data;

	if (data == NULL || size == 0U || (address & 3U) != 0U ||
	    (uint64_t)size > (uint64_t)UINT32_MAX + 1ULL - address) {
		return -EINVAL;
	}

	for (size_t offset = 0U; offset + sizeof(uint32_t) <= size; offset += sizeof(uint32_t)) {
		uint32_t value = con_boot_platform_read32(address + (uint32_t)offset);
		memcpy(dst + offset, &value, sizeof(value));
	}

	if ((size & 3U) != 0U) {
		size_t offset = size & ~((size_t)3U);
		uint32_t value = con_boot_platform_read32(address + (uint32_t)offset);
		memcpy(dst + offset, &value, size - offset);
	}

	return 0;
}

int32_t con_boot_bus_write(uint32_t address, const void *data, size_t size)
{
	const uint8_t *src = data;

	if (data == NULL || size == 0U || (address & 3U) != 0U ||
	    (uint64_t)size > (uint64_t)UINT32_MAX + 1ULL - address) {
		return -EINVAL;
	}

	for (size_t offset = 0U; offset + sizeof(uint32_t) <= size; offset += sizeof(uint32_t)) {
		uint32_t value;
		memcpy(&value, src + offset, sizeof(value));
		con_boot_platform_write32(address + (uint32_t)offset, value);
	}

	if ((size & 3U) != 0U) {
		size_t offset = size & ~((size_t)3U);
		uint32_t value = 0U;
		memcpy(&value, src + offset, size - offset);

		con_boot_platform_write32(address + (uint32_t)offset, value);
	}
	return 0;
}

int32_t con_boot_bus_read32(uint32_t address, uint32_t *value)
{
	if (value == NULL || (address & 3U) != 0U) {
		return -EINVAL;
	}
	*value = con_boot_platform_read32(address);
	return 0;
}

int32_t con_boot_bus_write32(uint32_t address, uint32_t value)
{
	if ((address & 3U) != 0U) {
		return -EINVAL;
	}
	con_boot_platform_write32(address, value);
	return 0;
}
