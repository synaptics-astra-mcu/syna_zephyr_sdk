/*
 * Copyright (c) 2025 Synaptics Incorporated
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

#include <zephyr/cache.h>
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

#define AON_CONFIG	0x50350000
#define AON_POR_RST	0x50350038

#define LP_CLKRST	0xb5007000
#define LP_CLK_ENABLE	0x00
#define LP_STICKY_RST	0x14

#define LPS_G1_CLKRST		0xB48A0000
#define LPS_G1_CLK_ENABLE	0x00
#define LPS_G1_CLK_ENABLE_MASK	0x7FFFFF

#define AON_POR_RST_M4_WATCHDOG_BIT	(8)
#define AON_POR_RST_M55_WATCHDOG_BIT	(9)

#define SOC_BASE	0x50302000
#define DMA0_TRIG_EN	0x54
#define DMA1_TRIG_EN	0x84

#define NPU_CLK_ENABLE_REG	0x50330700U
#define NPU_CLK_ENABLE_BIT	BIT(3)
#define NPU_STICKY_RST_REG	0x50330658U
#define NPU_STICKY_RSTN_BIT	BIT(0)

#define SACFG_BASE		0x50080000U
#define SACFG_SPCSECCTRL	SACFG_BASE
#define SACFG_PERIPHSPPPCEXP0	(SACFG_BASE + 0x0C0U)
#define SACFG_NSMSCEXP		(SACFG_BASE + 0x0D0U)
#define SACFG_PERIPHSPPPCEXP_NS_ALL  0xFFFFU
#define SACFG_NSMSCEXP_CLEAR  0U

void soc_sr100_lp_jpeg_clocks_enable(void)
{
	sys_write32(LPS_G1_CLK_ENABLE_MASK, LPS_G1_CLKRST + LPS_G1_CLK_ENABLE);
}

void soc_sr100_lp_jpeg_clocks_disable(void)
{
	sys_write32(0U, LPS_G1_CLKRST + LPS_G1_CLK_ENABLE);
}

/**
 * @brief Perform basic hardware initialization at boot.
 *
 * Enable clocks not managed by corresponding device drivers.
 */
void soc_early_init_hook(void)
{
	const mem_addr_t swire_ctrl = DT_REG_ADDR(DT_NODELABEL(pinctrl_swire));
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c0), okay) || DT_NODE_HAS_STATUS(DT_NODELABEL(i2c1), okay)
	const mem_addr_t clock_ctrl = DT_REG_ADDR(DT_NODELABEL(gcr));
#endif
	uint32_t value;

	/* Enable caches */
	sys_cache_instr_enable();
#ifdef CONFIG_SOC_SR100_M55
	sys_cache_data_enable();

	/* Enable Loop and branch info cache */
	SCB->CCR |= SCB_CCR_LOB_Msk;
	__DSB();
	__ISB();
#endif

	/* Setup various clocks and wakeup sources */

	value = sys_read32(swire_ctrl);
	value &= ~(0x00800000); /* Disable SWIRE power down */
	value |= 0x7b7bc; /* Pull-up & increased drive-strength for I2C1 */
	sys_write32(value, swire_ctrl);

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

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart_lp_rx_b), okay)
	/* Set UART port sel to 1 */
	sys_write32(0x1, 0xb5007034);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(lp_gpio), okay)
	value = sys_read32(LP_CLKRST + LP_CLK_ENABLE);
	value |= BIT(17) | BIT(19) | BIT(20);
	sys_write32(value, LP_CLKRST + LP_CLK_ENABLE);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(lp_wdt), okay)
	value = sys_read32(LP_CLKRST + LP_CLK_ENABLE);
	value |= BIT(11) | BIT(16);
	sys_write32(value, LP_CLKRST + LP_CLK_ENABLE);
	value = sys_read32(LP_CLKRST + LP_STICKY_RST);
	value |= BIT(0) | BIT(1);
	sys_write32(value, LP_CLKRST + LP_STICKY_RST);

	sys_clear_bit(AON_POR_RST, AON_POR_RST_M4_WATCHDOG_BIT);
#endif

#ifdef CONFIG_BOARD_SR100_RDK_SR100_M55
	/* Set UART0 Clock to 100MHz */
	sys_write32(0x25, UART0_CLK_REG);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(dma0), okay)
	 /* enable trig-in */
	 sys_write32(0xffffffff, SOC_BASE + DMA0_TRIG_EN);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(dma1), okay)
	/* enable trig-in */
	sys_write32(0xffffffff, SOC_BASE + DMA1_TRIG_EN);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ethosu0), okay)
	/* Configure SACFG peripheral access; skip if locked by secure firmware */
	if (!(sys_read32(SACFG_SPCSECCTRL) & BIT(0))) {
		for (uint32_t off = 0U; off < 16U; off += 4U) {
			sys_write32(SACFG_PERIPHSPPPCEXP_NS_ALL,
				    SACFG_PERIPHSPPPCEXP0 + off);
		}
		sys_write32(SACFG_NSMSCEXP_CLEAR, SACFG_NSMSCEXP);

		/* Readback to ensure posted writes complete on the bus. */
		(void)sys_read32(SACFG_PERIPHSPPPCEXP0);
		(void)sys_read32(SACFG_NSMSCEXP);
	}

	/* Enable NPU clock and release sticky reset before Ethos-U driver init */
	value = sys_read32(NPU_CLK_ENABLE_REG);
	value |= NPU_CLK_ENABLE_BIT;
	sys_write32(value, NPU_CLK_ENABLE_REG);

	/* Clear latched sticky reset bits, then release NPU core reset */
	sys_write32(0U, NPU_STICKY_RST_REG);
	value = sys_read32(NPU_STICKY_RST_REG);
	value |= NPU_STICKY_RSTN_BIT;
	sys_write32(value, NPU_STICKY_RST_REG);
#endif
}

void soc_late_init_hook(void)
{
#ifdef CONFIG_SR100_RELEASE_M4_RESET
	/* Take M4 out of reset (AON_MAIN - AON_CONFIG/LPP_DEBUG_MODE = running) */
	uint32_t value = sys_read32(AON_CONFIG);
	value &= ~(0x3 << 8);
	sys_write32(value, AON_CONFIG);
#endif
}
