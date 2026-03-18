/*
 * Copyright (c) 2025 Synaptics Incorporated.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief System/hardware module for the Synaptics SR1xx SoC
 *
 * This module provides routines to initialize and support board-level hardware
 * for the Synaptics SR1xx SoC.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <soc.h>

#define GCR_NODE DT_NODELABEL(gcr)
#define GCR_BASE DT_REG_ADDR(GCR_NODE)
#define UART0_CLK_REG_OFFSET  0x50   /* UART offset */
#define UART0_CLK_REG (GCR_BASE + UART0_CLK_REG_OFFSET)

#define CLK_ENABLE2	0x04
#define I2C0_CTRL	0x58
#define I2C1_CTRL	0x5c

/**
 * @brief Perform basic hardware initialization at boot.
 *
 * Enable clocks not managed by corresponding device drivers.
 */
void soc_early_init_hook(void)
{
	const mem_addr_t swire_ctrl = DT_REG_ADDR(DT_NODELABEL(pinctrl_swire));
	const mem_addr_t clock_ctrl = DT_REG_ADDR(DT_NODELABEL(gcr));
	uint32_t value;

	/* Setup various clocks and wakeup sources */

	value = sys_read32(swire_ctrl);
	value &= ~(0x00800000); /* Disable SWIRE power down */
	value |= 0x7b7bc; /* Pull-up & increased drive-strength for I2C1 */
	sys_write32(value, swire_ctrl);

    /*Set UART0 Clock to 100Mhz */
	sys_write32(0x25, UART0_CLK_REG);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c0), okay)
	value = sys_read32(clock_ctrl + I2C0_CTRL);
	value |= 1;
	sys_write32(value, clock_ctrl + I2C0_CTRL);

	value = sys_read32(clock_ctrl + CLK_ENABLE2);
	value |= (1 << 10);
	sys_write32(value, clock_ctrl + CLK_ENABLE2);
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c1), okay)
	value = sys_read32(clock_ctrl + I2C1_CTRL);
	value |= 1;
	sys_write32(value, clock_ctrl + I2C1_CTRL);

	value = sys_read32(clock_ctrl + CLK_ENABLE2);
	value |= (1 << 11);
	sys_write32(value, clock_ctrl + CLK_ENABLE2);
#endif
}
