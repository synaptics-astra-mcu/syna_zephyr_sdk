/*
 * Copyright (c) 2026 Synaptics Incorporated.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief System/hardware module for the Synaptics SR22x SoC
 *
 * This module provides routines to initialize and support board-level hardware
 * for the Synaptics SR22x SoC.
 */

#include <zephyr/linker/linker-defs.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <soc.h>
#include <asoc.h>

#define GLOBAL_SEC_BASE_ADDRESS	0x58024000
#define GLOBAL_BASE_ADDRESS		0x58025000

#define PLL_MCU_MAIN_CTRL	0x0004
#define DIV_CH_BIT			0x1
#define DIV_CTRL_BIT		0x2
#define DIV_CH_EN			0x2

#define SEC_clk_mcu_main_ctrl  0x4
#define SEC_clk_mcu_ahb_ctrl   0x8

#define clk_ser_uart1_ctrl	0x0004
#define clk_ser_uart2_ctrl	0x0008
#define clk_ser_uart3_ctrl	0x000C
#define clk_deb_gpio_ctrl	0x0010
#define clk_ser_i2cm0_ctrl	0x0014
#define clk_ser_i2cm1_ctrl	0x0018
#define clk_ser_spim_ctrl	0x001C
#define clk_ser_spis_ctrl	0x0020
#define clk_ser_i3c_ctrl	0x0024
#define clk_ser_xspi_ctrl	0x0028
#define clk_hs_pvt_ctrl		0x002C
#define clk_core_adc_ctrl	0x0030
#define clk_per_pwm_ctrl	0x0034
#define clk_ser_can0_ctrl	0x0038
#define clk_ser_can1_ctrl	0x003C
#define clk_ser_pdm_ctrl	0x0040
#define MCU_GBL_CFG_SEC_RST_SYNC_STICKY_RST0_OFFSET	0x418
#define GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1_OFFSET	0x404
#define GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST0_OFFSET	0x400
#define MCU_GBL_CFG_SEC_SYS_ROM_PWR_CTRL_OFFSET		0xA34

#define MCU_GBL_CFG_SEC_SPROT_ROM_0_PWR_CTRL_OFFSET		0x00000A0C
#define MCU_GBL_CFG_SEC_BMC_BANK3_PWR_CTRL_OFFSET		0x00000A88

static void release_all_resets()
{
	/* Reset programming*/
	sys_write32(0xFFFFFFFF, GLOBAL_SEC_BASE_ADDRESS + MCU_GBL_CFG_SEC_RST_SYNC_STICKY_RST0_OFFSET); /*sticky_rst0*/
	sys_write32(0xFFFFFFFF, GLOBAL_BASE_ADDRESS + GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST0_OFFSET);      /*sticky_rst0*/
	sys_write32(0xFFFFFFFF, GLOBAL_BASE_ADDRESS + GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1_OFFSET);      /*sticky_rst1*/

	/* Enable ROM power */
	sys_write32(0, GLOBAL_SEC_BASE_ADDRESS + MCU_GBL_CFG_SEC_SYS_ROM_PWR_CTRL_OFFSET);
}

static int mcu_configure_pll(bool io_pll, uint32_t div)
{
	uint32_t sec_pll_base = GLOBAL_SEC_BASE_ADDRESS + (io_pll ? 0x304 : 0x300);

	/* MCPU PLL from MCUPLLF (400MHz) */
	SET_VALUE_MASK(sec_pll_base, 0 << 0x7, 0x80);

	/* set pll_ref_sel to 1 to switch to XTAL.*/
	if(!io_pll)
		SET_VALUE_MASK(sec_pll_base, 1 << 8, 0x100);

	SET_VALUE_MASK(sec_pll_base, 0 << 4 , 0x10);
	/* change IPLL default divider from 2 to 1 to get 400Mhz. */
	SET_VALUE_MASK(sec_pll_base, div & 0xF, 0xF);
	/* update pll divider */
	SET_VALUE_MASK(sec_pll_base, 1 << 4 , 0x10);
	SET_VALUE_MASK(sec_pll_base, 0 << 4 , 0x10);

	/* disable pll bypass */
	SET_VALUE_MASK(sec_pll_base, 1 << 0x7, 0x80);
	return 0;
}

int update_pll_setting(void)
{
	uint32_t sec_clk_base = 0x58024600;

	/* change main_clk divider to 2 */
	SET_VALUE_MASK(sec_clk_base + PLL_MCU_MAIN_CTRL, 0 << DIV_CH_BIT, DIV_CH_EN);
	SET_VALUE_MASK(sec_clk_base + PLL_MCU_MAIN_CTRL, (2-1)<<DIV_CTRL_BIT, 0xFF << DIV_CTRL_BIT);
	SET_VALUE_MASK(sec_clk_base + PLL_MCU_MAIN_CTRL, 1 << DIV_CH_BIT, DIV_CH_EN);
	for(volatile int i = 0; i < 1000; i++);
	SET_VALUE_MASK(sec_clk_base + PLL_MCU_MAIN_CTRL, 0 << DIV_CH_BIT, DIV_CH_EN);

	mcu_configure_pll(false, 0); /* MCU PLL */
	mcu_configure_pll(true, 1); /* IO PLL */

	return 0;

}

static void power_on_all_sram()
{
	uint32_t addr;

	/* Enable all PWR_CTRL under Global */
	for (addr = (GLOBAL_SEC_BASE_ADDRESS + MCU_GBL_CFG_SEC_SPROT_ROM_0_PWR_CTRL_OFFSET);
		addr <= (GLOBAL_SEC_BASE_ADDRESS + MCU_GBL_CFG_SEC_BMC_BANK3_PWR_CTRL_OFFSET); addr += 4) {
		sys_write32(0, addr);
	}
}

static void enable_other_clocks()
{
	uint32_t gbl_clk_base = 0x58025600;
	uint32_t sec_clk_base = 0x58024600;

	SET_VALUE_MASK(sec_clk_base + SEC_clk_mcu_main_ctrl,  0x1, 0x1);
	SET_VALUE_MASK(sec_clk_base + SEC_clk_mcu_ahb_ctrl,   0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_uart1_ctrl, 0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_uart2_ctrl, 0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_uart3_ctrl, 0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_deb_gpio_ctrl,  0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_i2cm0_ctrl, 0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_i2cm1_ctrl, 0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_spim_ctrl,  0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_spis_ctrl,  0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_i3c_ctrl,   0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_xspi_ctrl,  0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_hs_pvt_ctrl,	0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_core_adc_ctrl,  0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_per_pwm_ctrl,   0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_can0_ctrl,  0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_can1_ctrl,  0x1, 0x1);
	SET_VALUE_MASK(gbl_clk_base + clk_ser_pdm_ctrl,   0x1, 0x1);

	/* CLK gate */
	sys_write32(0xFFFFFFFF, sec_clk_base);
	sys_write32(0xFFFFFFFF, gbl_clk_base);

	/* Enable LPRCOSC */
	SET_VALUE_MASK(GLOBAL_SEC_BASE_ADDRESS + 0x030C, 0x1, 0x1);
}

extern uint8_t z_main_stack[];
extern char z_interrupt_stacks[];

/** noinit_sec_clean
	* @brief Clean the noinit section which is used for idle stack and main stack to make sure they are in a known state before use.
	* This is required for TCM memory with ECC enabled.
	* @return 0 on success
 */
static int noinit_sec_clean(void)
{
	volatile uint32_t *ptr;
	volatile uint32_t *end;

	/* Initialize uninitialized DTCM - from the point after interrupt stacks to end */
	ptr = (volatile uint32_t *)((uintptr_t)z_interrupt_stacks+CONFIG_ISR_STACK_SIZE);
	end = (uint32_t *)&__kernel_ram_end;
	while (ptr < end) {
		sys_write32(0x00000000U, (uintptr_t)ptr++);
	}

	return 0;
}

/**
 * @brief Perform early system initialization at boot.
 *
 * This is called before the main stack and idle stack are initalized, so only
 * minimal hardware initialization should be performed here. The main purpose of
 * this function is to clean the noinit section used for idle and main stacks to
 * make sure they are in a known state before use.
 */
void soc_prep_hook(void)
{
	noinit_sec_clean();
}


/**
 * @brief Perform basic hardware initialization at boot.
 *
 * Enable clocks not managed by corresponding device drivers.
 */
void soc_early_init_hook(void)
{
	/* Setup various clocks and wakeup sources */
	update_pll_setting();
	release_all_resets();
	power_on_all_sram();
	enable_other_clocks();

#if defined(CONFIG_ASOC)
	/* A55 setup */
	asoc_setup();
#endif

}
