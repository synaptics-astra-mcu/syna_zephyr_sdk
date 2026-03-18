/*
 * Copyright (c) 2025 Synaptics, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sr100_hwinfo

#include <zephyr/drivers/hwinfo.h>
#include <soc.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>

ssize_t z_impl_hwinfo_get_device_id(uint8_t *buffer, size_t length)
{
	uint32_t addr = DT_INST_REG_ADDR_BY_IDX(0, 0);
	uint32_t i, id[3];

	for (i = 0; i < 3; i++)
		id[i] = sys_read32(addr + (i * 4U));

	length = MIN(length, sizeof(id));
	memcpy(buffer, id, length);

	return length;
}

int z_impl_hwinfo_get_reset_cause(uint32_t *cause)
{
	uint32_t addr = DT_INST_REG_ADDR_BY_IDX(0, 1);
	uint32_t flags = 0;
	uint32_t val = sys_read32(addr);

	if (val & 0xFFU) /* AON or M4/LPP trigger */
		flags |= RESET_LOW_POWER_WAKE;
	if (val & (3U << 8)) /* M4 or M55 */
		flags |= RESET_WATCHDOG;
	if (val & (3U << 10)) /* BCM or SOC */
		flags |= RESET_SOFTWARE;
	if (val & (1U << 12)) /* Hardware PAD */
		flags |= RESET_PIN;
	if (val & (1U << 13)) /* Analog PMU */
		flags |= RESET_POR;
	if (val == 0U)
		flags |= RESET_POR;

	*cause = flags;

	return 0;
}

int z_impl_hwinfo_clear_reset_cause(void)
{
	uint32_t addr = DT_INST_REG_ADDR_BY_IDX(0, 1);

	sys_write32(0x3fff, addr);

	return 0;
}

int z_impl_hwinfo_get_supported_reset_cause(uint32_t *supported)
{
	*supported = (RESET_PIN
		      | RESET_WATCHDOG
		      | RESET_SOFTWARE
		      | RESET_LOW_POWER_WAKE
		      | RESET_POR);

	return 0;
}
