/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <soc.h>
#if defined(CONFIG_FLASH_CAD_XSPI)
#include <zephyr/drivers/flash/cadence_xspi_soc.h>
#endif

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
		    GLOBAL_SEC_BASE_ADDRESS + MCU_GBL_CFG_SEC_RST_SYNC_STICKY_RST0_OFFSET);
	/* sticky_rst0 */
	sys_write32(0xFFFFFFFF,
		    GLOBAL_BASE_ADDRESS + GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST0_OFFSET);
	/* sticky_rst1 */
	sys_write32(0xFFFFFFFF,
		    GLOBAL_BASE_ADDRESS + GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1_OFFSET);

	/* Enable ROM power */
	sys_write32(0, GLOBAL_SEC_BASE_ADDRESS + MCU_GBL_CFG_SEC_SYS_ROM_PWR_CTRL_OFFSET);
}

static int mcu_configure_pll(bool io_pll, uint32_t div)
{
	uint32_t sec_pll_base = GLOBAL_SEC_BASE_ADDRESS + (io_pll ? 0x304 : 0x300);

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
	uint32_t sec_clk_base = 0x58024600;

	/* change main_clk divider to 1 */
	uint32_t reg_val = sys_read32(sec_clk_base + PLL_MCU_MAIN_CTRL);
	reg_val = (reg_val & (~(DIV_CH_EN))) |
		  ((0 << DIV_CH_BIT) & (DIV_CH_EN));
	sys_write32(reg_val, sec_clk_base + PLL_MCU_MAIN_CTRL);
	reg_val = sys_read32(sec_clk_base + PLL_MCU_MAIN_CTRL);
	reg_val = (reg_val & (~(0xFF << DIV_CTRL_BIT))) |
		   (((0 << DIV_CTRL_BIT)) & (0xFF << DIV_CTRL_BIT));
	sys_write32(reg_val, sec_clk_base + PLL_MCU_MAIN_CTRL);
	reg_val = sys_read32(sec_clk_base + PLL_MCU_MAIN_CTRL);
	reg_val = (reg_val & (~(DIV_CH_EN))) |
		  ((1 << DIV_CH_BIT) & (DIV_CH_EN));
	sys_write32(reg_val, sec_clk_base + PLL_MCU_MAIN_CTRL);

	srw1500_nop_delay(20);

	reg_val = sys_read32(sec_clk_base + PLL_MCU_MAIN_CTRL);
	reg_val = (reg_val & (~(DIV_CH_EN))) |
		  ((0 << DIV_CH_BIT) & (DIV_CH_EN));
	sys_write32(reg_val, sec_clk_base + PLL_MCU_MAIN_CTRL);

	mcu_configure_pll(false, 1); /* MCU PLL */
	mcu_configure_pll(true, 1); /* IO PLL */

	return 0;

}

static void power_on_all_sram(void)
{
	uint32_t addr;

	for (addr = GLOBAL_SEC_BASE_ADDRESS + MCU_GBL_CFG_SEC_SPROT_ROM_0_PWR_CTRL_OFFSET;
	     addr <= GLOBAL_SEC_BASE_ADDRESS + MCU_GBL_CFG_SEC_BMC_BANK3_PWR_CTRL_OFFSET;
	     addr += sizeof(uint32_t)) {
		sys_write32(0, addr);
	}
}

static void enable_other_clocks(void)
{
	uint32_t gbl_clk_base = 0x58025600;
	uint32_t sec_clk_base = 0x58024600;
	uint32_t clk_val;

	clk_val = sys_read32(sec_clk_base + SEC_clk_mcu_main_ctrl);
	clk_val = (clk_val & (~(0x1))) | (0x1);
	sys_write32(clk_val, sec_clk_base + SEC_clk_mcu_main_ctrl);
	clk_val = sys_read32(sec_clk_base + SEC_clk_mcu_ahb_ctrl);
	clk_val = (clk_val & (~(0x1))) | (0x1);
	sys_write32(clk_val, sec_clk_base + SEC_clk_mcu_ahb_ctrl);
	clk_val = sys_read32(gbl_clk_base + clk_ser_i2cm0_ctrl);
	clk_val = (clk_val & (~(0x1))) | (0x1);
	sys_write32(clk_val, gbl_clk_base + clk_ser_i2cm0_ctrl);

	sys_write32(0xFFFFFFFF, sec_clk_base);
	sys_write32(0xFFFFFFFF, gbl_clk_base);
}

void soc_early_init_hook(void)
{
	update_pll_setting();
	release_all_resets();
	power_on_all_sram();
	enable_other_clocks();
}

#if defined(CONFIG_FLASH_CAD_XSPI)
#define SRW1500_GBL_SEC_BASE                     0x58024000u
#define SRW1500_GLOBAL_BASE                      0x58025000u
#define SRW1500_XSPI_SRAM_PWR_OFFSET             0x0A00u
#define SRW1500_SER_XSPI_CTRL_OFFSET             0x0628u
#define SRW1500_GBL_SEC_OTF_CLK_OFFSET           0x0600u
#define SRW1500_GLOBAL_CLK_OFFSET                0x0600u
#define SRW1500_GBL_SEC_RST0_OFFSET              0x0418u
#define SRW1500_GLOBAL_RST1_OFFSET               0x0404u
#define SRW1500_GLOBAL_PERIF_XSPI_CTRL0_OFFSET   0x0824u
#define SRW1500_GLOBAL_PERIF_XSPI_CTRL1_OFFSET   0x0828u
#define SRW1500_GLOBAL_PERIF_XSPI_CTRL2_OFFSET   0x082Cu
#define SRW1500_GLOBAL_PERIF_XSPI_CTRL3_OFFSET   0x0830u
#define SRW1500_GLOBAL_PERIF_XSPI_RB_VALID_TIME  0x0834u
#define SRW1500_GLOBAL_PHY_DQ_TIMING_OFFSET      0x0838u
#define SRW1500_GLOBAL_PHY_DQS_TIMING_OFFSET     0x083Cu
#define SRW1500_GLOBAL_PHY_GATE_LPBK_OFFSET      0x0840u
#define SRW1500_GLOBAL_PHY_DLL_MASTER_OFFSET     0x0844u
#define SRW1500_GLOBAL_PHY_DLL_SLAVE_OFFSET      0x0848u

#define SRW1500_XSPI_CTRL_INIT_COMPLETE          BIT(16)
#define SRW1500_OTF_CRYPTO_STICKY_PRSTN          BIT(5)
#define SRW1500_XSPI_RESETS                      0x7u
#define SRW1500_GLOBAL_PERIF_XSPI_DQS_REMOD_EN   BIT(1)

static const struct cadence_xspi_phy_config srw1500_xspi_phy_config = {
	.dll_ctrl = 0x707,
	.dq_timing = 0x101,
	.dqs_timing = 0x300404,
	.gate_lpbk_ctrl = 0x200030,
	.dll_master_ctrl = 0x140080,
	.dll_slave_ctrl = 0x00003333,
};

static int srw1500_otf_enable_clock(void)
{
	sys_write32(sys_read32(SRW1500_GLOBAL_BASE + SRW1500_XSPI_SRAM_PWR_OFFSET) & ~0x1u,
		    SRW1500_GLOBAL_BASE + SRW1500_XSPI_SRAM_PWR_OFFSET);
	srw1500_nop_delay(20);
	sys_write32(sys_read32(SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_OTF_CLK_OFFSET) | 0x00010000u,
		    SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_OTF_CLK_OFFSET);
	srw1500_nop_delay(20);

	return 0;
}

static void srw1500_xspi_init_bootstrap(void)
{
	sys_write32(0x0000000Au, SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PERIF_XSPI_RB_VALID_TIME);
	sys_write32(0x00000002u, SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PERIF_XSPI_CTRL0_OFFSET);
	sys_write32(0x000498EBu, SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PERIF_XSPI_CTRL3_OFFSET);
	sys_write32(0x0000030Eu, SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PERIF_XSPI_CTRL2_OFFSET);

	sys_write32(sys_read32(SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PERIF_XSPI_CTRL1_OFFSET) |
		    SRW1500_GLOBAL_PERIF_XSPI_DQS_REMOD_EN,
		    SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PERIF_XSPI_CTRL1_OFFSET);

	sys_write32(0x00000101u, SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PHY_DQ_TIMING_OFFSET);
	sys_write32(0x00300404u, SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PHY_DQS_TIMING_OFFSET);
	sys_write32(0x00200030u, SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PHY_GATE_LPBK_OFFSET);
	sys_write32(0x00140080u, SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PHY_DLL_MASTER_OFFSET);
	sys_write32(0x00003333u, SRW1500_GLOBAL_BASE + SRW1500_GLOBAL_PHY_DLL_SLAVE_OFFSET);
}

static void srw1500_otf_reset(void)
{
	uint32_t sec_rst0;

	srw1500_nop_delay(20);
	sec_rst0 = sys_read32(SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_RST0_OFFSET);
	sys_write32(sec_rst0 & ~SRW1500_OTF_CRYPTO_STICKY_PRSTN,
		    SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_RST0_OFFSET);
	srw1500_nop_delay(20);
	sys_write32(sec_rst0 | SRW1500_OTF_CRYPTO_STICKY_PRSTN,
		    SRW1500_GBL_SEC_BASE + SRW1500_GBL_SEC_RST0_OFFSET);
	srw1500_nop_delay(20);
}

static int srw1500_xspi_wait_init_complete(uintptr_t reg_base, uint32_t timeout_cnt)
{
	while (timeout_cnt > 0U) {
		if (sys_read32(reg_base + 0x0100u) & SRW1500_XSPI_CTRL_INIT_COMPLETE) {
			return 0;
		}

		srw1500_nop_delay(10);
		timeout_cnt--;
	}

	return -ETIMEDOUT;
}

const struct cadence_xspi_phy_config *cadence_xspi_soc_phy_config(void)
{
	return &srw1500_xspi_phy_config;
}

int cadence_xspi_soc_pre_init(uintptr_t reg_base)
{
	int ret;

	ret = srw1500_otf_enable_clock();
	if (ret < 0) {
		return ret;
	}

	srw1500_xspi_init_bootstrap();
	srw1500_otf_reset();

	return srw1500_xspi_wait_init_complete(reg_base, 10000);
}
#endif /* CONFIG_FLASH_CAD_XSPI */
