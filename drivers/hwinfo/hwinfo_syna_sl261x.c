/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sl261x_hwinfo

#include <zephyr/drivers/hwinfo.h>
#include <soc.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>

ssize_t z_impl_hwinfo_get_device_id(uint8_t *buffer, size_t length)
{
	uint32_t addr = DT_INST_REG_ADDR_BY_IDX(0, 0);
	uint32_t id[3];
	uint32_t i;

	for (i = 0; i < 3; i++)
		id[i] = sys_read32(addr + (i * 4U));

	length = MIN(length, sizeof(id));
	memcpy(buffer, id, length);

	return length;
}
