/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/cache.h>
#if DT_HAS_COMPAT_STATUS_OKAY(arm_ahb_cache)
#include <zephyr/drivers/cache/arm_ahb_cache.h>
#endif
#include <zephyr/sys/sys_io.h>
#include <soc.h>
#include "soc_regs.h"

#if DT_HAS_COMPAT_STATUS_OKAY(cdns_xspi)
#include <zephyr/drivers/flash/cadence_xspi_soc.h>

#if defined(CONFIG_RESET)
#include <zephyr/drivers/reset.h>

#define SRW1500_XSPI_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(cdns_xspi)

static const struct reset_dt_spec srw1500_xspi_reset =
	RESET_DT_SPEC_GET_BY_IDX(SRW1500_XSPI_NODE, 0);

static const struct reset_dt_spec srw1500_otf_reset =
	RESET_DT_SPEC_GET_BY_IDX(SRW1500_XSPI_NODE, 1);
#endif
#endif

static void srw1500_nop_delay(uint32_t n)
{
	while (n-- > 0U) {
		__asm volatile ("nop");
	}
}

static void release_all_resets()
{
	/* Reset programming*/
	sys_write32(0xFFFFFFFF,
		    SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_RST0_OFFSET);
	/* sticky_rst0 */
	sys_write32(0xFFFFFFFF,
		    SRW1500_GBL_BASE + SRW1500_GBL_RST0_OFFSET);
	/* sticky_rst1 */
	sys_write32(0xFFFFFFFF,
		    SRW1500_GBL_BASE + SRW1500_GBL_RST1_OFFSET);

	/* Enable ROM power */
	sys_write32(0, SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_SYS_ROM_PWR_CTRL_OFFSET);
}

static int mcu_configure_pll(bool io_pll, uint32_t div)
{
	uint32_t sec_pll_base = SRW1500_GBL_SEC_BASE +
		(io_pll ? SRW1500_GBL_SEC_PLL_IO_OFFSET : SRW1500_GBL_SEC_PLL_MCU_OFFSET);

	/* MCPU PLL from MCUPLLF (384MHz) */
	uint32_t val = sys_read32(sec_pll_base);
	val = (val & (~(0x80))) | ((0 << 0x7) & (0x80));
	sys_write32(val, sec_pll_base);

	/* set pll_ref_sel to 1 to switch to XTAL.*/
	if(!io_pll) {
		val = sys_read32(sec_pll_base);
		val = (val & (~(0x100))) | ((1 << 8) & (0x100));
		sys_write32(val, sec_pll_base);
	}

	val = sys_read32(sec_pll_base);
	val = (val & (~(0x10))) | ((0 << 4) & (0x10));
	sys_write32(val, sec_pll_base);
	/* change IPLL default divider from 2 to 1 to get 192MHz. */
	val = sys_read32(sec_pll_base);
	val = (val & (~(0xF))) | ((div & 0xF) & (0xF));
	sys_write32(val, sec_pll_base);
	/* update pll divider */
	val = sys_read32(sec_pll_base);
	val = (val & (~(0x10))) | ((1 << 4) & (0x10));
	sys_write32(val, sec_pll_base);
	val = sys_read32(sec_pll_base);
	val = (val & (~(0x10))) | ((0 << 4) & (0x10));
	sys_write32(val, sec_pll_base);

	/* disable pll bypass */
	val = sys_read32(sec_pll_base);
	val = (val & (~(0x80))) | ((1 << 0x7) & (0x80));
	sys_write32(val, sec_pll_base);
	return 0;
}

int update_pll_setting(void)
{
	uint32_t sec_clk_base = SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_CLK_OFFSET;

	/* change main_clk divider to 1 */
	uint32_t reg_val = sys_read32(sec_clk_base + SRW1500_CLK_MCU_MAIN_CTRL_OFFSET);
	reg_val = (reg_val & (~(SRW1500_PLL_DIV_CH_EN))) |
		  ((0 << SRW1500_PLL_DIV_CH_BIT) & (SRW1500_PLL_DIV_CH_EN));
	sys_write32(reg_val, sec_clk_base + SRW1500_CLK_MCU_MAIN_CTRL_OFFSET);
	reg_val = sys_read32(sec_clk_base + SRW1500_CLK_MCU_MAIN_CTRL_OFFSET);
	reg_val = (reg_val & (~(0xFF << SRW1500_PLL_DIV_CTRL_BIT))) |
		   (((0 << SRW1500_PLL_DIV_CTRL_BIT)) & (0xFF << SRW1500_PLL_DIV_CTRL_BIT));
	sys_write32(reg_val, sec_clk_base + SRW1500_CLK_MCU_MAIN_CTRL_OFFSET);
	reg_val = sys_read32(sec_clk_base + SRW1500_CLK_MCU_MAIN_CTRL_OFFSET);
	reg_val = (reg_val & (~(SRW1500_PLL_DIV_CH_EN))) |
		  ((1 << SRW1500_PLL_DIV_CH_BIT) & (SRW1500_PLL_DIV_CH_EN));
	sys_write32(reg_val, sec_clk_base + SRW1500_CLK_MCU_MAIN_CTRL_OFFSET);

	srw1500_nop_delay(20);

	reg_val = sys_read32(sec_clk_base + SRW1500_CLK_MCU_MAIN_CTRL_OFFSET);
	reg_val = (reg_val & (~(SRW1500_PLL_DIV_CH_EN))) |
		  ((0 << SRW1500_PLL_DIV_CH_BIT) & (SRW1500_PLL_DIV_CH_EN));
	sys_write32(reg_val, sec_clk_base + SRW1500_CLK_MCU_MAIN_CTRL_OFFSET);

	mcu_configure_pll(false, 1); /* MCU PLL */
	mcu_configure_pll(true, 1); /* IO PLL */

	return 0;

}

static void power_on_all_sram(void)
{
	uint32_t addr;

	for (addr = SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_SPROT_ROM_0_PWR_CTRL_OFFSET;
	     addr <= SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_BMC_BANK3_PWR_CTRL_OFFSET;
	     addr += sizeof(uint32_t)) {
		sys_write32(0, addr);
	}
}

static void enable_other_clocks(void)
{
	uint32_t gbl_clk_base = SRW1500_GBL_BASE + SRW1500_GBL_CLK_OFFSET;
	uint32_t sec_clk_base = SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_CLK_OFFSET;
	uint32_t clk_val;

	clk_val = sys_read32(sec_clk_base + SRW1500_CLK_MCU_MAIN_CTRL_OFFSET);
	clk_val = (clk_val & (~(0x1))) | (0x1);
	sys_write32(clk_val, sec_clk_base + SRW1500_CLK_MCU_MAIN_CTRL_OFFSET);
	clk_val = sys_read32(sec_clk_base + SRW1500_CLK_MCU_AHB_CTRL_OFFSET);
	clk_val = (clk_val & (~(0x1))) | (0x1);
	sys_write32(clk_val, sec_clk_base + SRW1500_CLK_MCU_AHB_CTRL_OFFSET);

	sys_write32(0xFFFFFFFF, sec_clk_base);
	sys_write32(0xFFFFFFFF, gbl_clk_base);
}

#if DT_HAS_COMPAT_STATUS_OKAY(arm_ethos_u)
static void srw1500_npu_clock_reset_init(void)
{
	uint32_t val;
	uint32_t count = SRW1500_NPU_RESET_WAIT_COUNT;

	val = sys_read32(SRW1500_NPU_CLK_EN_REG);
	sys_write32(val | SRW1500_NPU_CLK_EN_MASK, SRW1500_NPU_CLK_EN_REG);

	val = sys_read32(SRW1500_NPU_RESET_REG);
	sys_write32(val | SRW1500_NPU_RESET_RELEASE, SRW1500_NPU_RESET_REG);

	while ((sys_read32(SRW1500_NPU_RESET_STATUS_REG) & SRW1500_NPU_RESET_ACTIVE) != 0U &&
	       count-- > 0U) {
		srw1500_nop_delay(1);
	}
}
#endif

void soc_early_init_hook(void)
{
	update_pll_setting();
	release_all_resets();
	power_on_all_sram();
	enable_other_clocks();
#if DT_HAS_COMPAT_STATUS_OKAY(arm_ethos_u)
	srw1500_npu_clock_reset_init();
#endif
	/* Enable Cotex-M52 I-cache and D-cache*/
	sys_cache_instr_enable();
	sys_cache_data_enable();
}

#if DT_HAS_COMPAT_STATUS_OKAY(arm_ahb_cache)
static int srw1500_otf_cache_init(void)
{
	arm_ahb_cache_enable();

	return 0;
}

SYS_INIT(srw1500_otf_cache_init, APPLICATION, 0);
#endif

#if defined(CONFIG_FLASH_CAD_XSPI)
static int srw1500_otf_enable_clock(void)
{
	sys_write32(sys_read32(SRW1500_GBL_BASE + SRW1500_GBL_XSPI_SRAM_PWR_OFFSET) & ~0x1u,
		    SRW1500_GBL_BASE + SRW1500_GBL_XSPI_SRAM_PWR_OFFSET);
	srw1500_nop_delay(20);
	sys_write32(sys_read32(SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_CLK_OFFSET) | 0x00010000u,
		    SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_CLK_OFFSET);
	srw1500_nop_delay(20);

	return 0;
}

static void srw1500_xspi_init_bootstrap(void)
{
	sys_write32(0x0000000Au,
		    SRW1500_GBL_BASE + SRW1500_GBL_PERIF_XSPI_RB_VALID_TIME_OFFSET);
	sys_write32(0x00000002u, SRW1500_GBL_BASE + SRW1500_GBL_PERIF_XSPI_CTRL0_OFFSET);
	sys_write32(0x000498EBu, SRW1500_GBL_BASE + SRW1500_GBL_PERIF_XSPI_CTRL3_OFFSET);
	sys_write32(0x0000030Eu, SRW1500_GBL_BASE + SRW1500_GBL_PERIF_XSPI_CTRL2_OFFSET);

	sys_write32(sys_read32(SRW1500_GBL_BASE + SRW1500_GBL_PERIF_XSPI_CTRL1_OFFSET) |
		    SRW1500_GBL_PERIF_XSPI_DQS_REMOD_EN,
		    SRW1500_GBL_BASE + SRW1500_GBL_PERIF_XSPI_CTRL1_OFFSET);

	sys_write32(0x00000101u, SRW1500_GBL_BASE + SRW1500_GBL_PHY_DQ_TIMING_OFFSET);
	sys_write32(0x00300404u, SRW1500_GBL_BASE + SRW1500_GBL_PHY_DQS_TIMING_OFFSET);
	sys_write32(0x00200030u, SRW1500_GBL_BASE + SRW1500_GBL_PHY_GATE_LPBK_OFFSET);
	sys_write32(0x00140080u, SRW1500_GBL_BASE + SRW1500_GBL_PHY_DLL_MASTER_OFFSET);
	sys_write32(0x00003333u, SRW1500_GBL_BASE + SRW1500_GBL_PHY_DLL_SLAVE_OFFSET);
}

static int srw1500_xspi_wait_init_complete(uintptr_t reg_base, uint32_t timeout_cnt)
{
	while (timeout_cnt > 0U) {
		if (sys_read32(reg_base + SRW1500_XSPI_CTRL_STATUS_OFFSET) & SRW1500_XSPI_CTRL_INIT_COMPLETE) {
			return 0;
		}

		srw1500_nop_delay(10);
		timeout_cnt--;
	}

	return -ETIMEDOUT;
}

int cadence_xspi_soc_pre_init(uintptr_t reg_base)
{
	int ret;

	if ((sys_read32(reg_base + SRW1500_XSPI_CTRL_STATUS_OFFSET) &
	     SRW1500_XSPI_CTRL_INIT_COMPLETE) != 0U) {
		return 0;
	}

	ret = srw1500_otf_enable_clock();
	if (ret < 0) {
		return ret;
	}

	srw1500_xspi_init_bootstrap();

#if defined(CONFIG_RESET)
	if (!device_is_ready(srw1500_xspi_reset.dev) ||
	    !device_is_ready(srw1500_otf_reset.dev)) {
		return -ENODEV;
	}

	ret = reset_line_assert_dt(&srw1500_otf_reset);
	if (ret < 0) {
		return ret;
	}

	ret = reset_line_assert_dt(&srw1500_xspi_reset);
	if (ret < 0) {
		return ret;
	}

	srw1500_nop_delay(20);

	ret = reset_line_deassert_dt(&srw1500_otf_reset);
	if (ret < 0) {
		return ret;
	}

	ret = reset_line_deassert_dt(&srw1500_xspi_reset);
	if (ret < 0) {
		return ret;
	}

	srw1500_nop_delay(20);
#endif

	return srw1500_xspi_wait_init_complete(reg_base, 10000);
}
#endif
