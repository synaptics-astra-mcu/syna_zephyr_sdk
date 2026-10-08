/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "con_boot_windows.h"
#include "con_boot.h"
#include "con_boot_platform.h"
#include "con_boot_trace.h"
#include "con_boot_srw1500_regs.h"

#define CON_BOOT_CSS2MCU_FIELD_MASK 0x7FFU
#define CON_BOOT_CSS2MCU_OFFSET_FIELD_MASK 0xFFFFFU
#define CON_BOOT_MCU2CSS_FIELD_MASK 0xFFFFFU


/* GSC windows are configured once during sequential cold boot; hence atomic operation is omitted. */
static void gsc_update(uint32_t offset, uint32_t mask, uint32_t value)
{
	uint32_t current;

	con_boot_bus_read32(CON_BOOT_GSC_BASE + offset, &current);
	con_boot_bus_write32(CON_BOOT_GSC_BASE + offset, (current & ~mask) | (value & mask));
}

static inline void gsc_set_valid(uint32_t offset, uint8_t index)
{
	gsc_update(offset, 1U << index, 1U << index);
}

static inline uint32_t window_page(uint32_t address)
{
	return address >> 12;
}

static inline uint32_t window_end(const con_boot_window *window)
{
	return window->aperture + window->size - 1U;
}

static inline uint32_t window_offset(const con_boot_window *window, bool css2mcu)
{
	uint32_t base = css2mcu ? (window->aperture & 0x7FFFFFU) : window->aperture;
	return window_page(window->target - base);
}

static void configure_direction(const con_boot_window *windows, size_t count,
																uint32_t valid, uint32_t start0, uint32_t end0, uint32_t offset0,
																uint32_t field_mask, uint32_t offset_mask, bool css2mcu)
{
	for (size_t i = 0U; i < count; ++i) {
		const con_boot_window *window = &windows[i];
		uint32_t end = window_end(window);
		uint32_t offset = window_offset(window, css2mcu);

		gsc_update(start0 + window->index * 4U, field_mask, window_page(window->aperture));
		gsc_update(end0 + window->index * 4U, field_mask, window_page(end));
		gsc_update(offset0 + window->index * 4U, offset_mask, offset);

		gsc_set_valid(valid, window->index);
	}
}

int32_t con_boot_configure_windows(const con_boot_window_config *config)
{
	if (config == NULL) {
		return -EINVAL;
	}

	CON_BOOT_TRACE("window configuration start MCU2CSS=%u CSS2MCU=%u",
		(unsigned)config->mcu2css_count, (unsigned)config->css2mcu_count);

	configure_direction(config->mcu2css, config->mcu2css_count, GSC_MCU2CSS_VALID_OFFSET,
		GSC_MCU2CSS_START0_OFFSET, GSC_MCU2CSS_END0_OFFSET, GSC_MCU2CSS_OFFSET0_OFFSET,
		CON_BOOT_MCU2CSS_FIELD_MASK, CON_BOOT_MCU2CSS_FIELD_MASK, false);

	configure_direction(config->css2mcu, config->css2mcu_count, GSC_CSS2MCU_VALID_OFFSET,
		GSC_CSS2MCU_START0_OFFSET, GSC_CSS2MCU_END0_OFFSET, GSC_CSS2MCU_OFFSET0_OFFSET,
		CON_BOOT_CSS2MCU_FIELD_MASK, CON_BOOT_CSS2MCU_OFFSET_FIELD_MASK, true);

	__DSB();

	return 0;
}
