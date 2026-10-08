/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Base addresses and register offsets for the srw1500 GBL (Global config),
 * GBL_SEC (Global Secure config), SEC_CTRL and GSC register spaces.
 *
 * Base addresses are sourced from devicetree (see the `gbl`, `gbl_sec`,
 * `sec_ctrl` and `gsc` syscon nodes in srw1500.dtsi) instead of being
 * hardcoded here, so soc.c never duplicates the physical memory map.
 */

#ifndef ZEPHYR_SOC_SYNA_ASTRA_SR_SRW1500_SOC_REGS_H_
#define ZEPHYR_SOC_SYNA_ASTRA_SR_SRW1500_SOC_REGS_H_

#include <zephyr/devicetree.h>

#define SRW1500_GBL_SEC_BASE  DT_REG_ADDR(DT_NODELABEL(gbl_sec))
#define SRW1500_GBL_BASE      DT_REG_ADDR(DT_NODELABEL(gbl))
#define SRW1500_SEC_CTRL_BASE DT_REG_ADDR(DT_NODELABEL(sec_ctrl))
#define SRW1500_GSC_BASE      DT_REG_ADDR(DT_NODELABEL(gsc))
#define SRW1500_GGC_BASE      DT_REG_ADDR(DT_NODELABEL(ggc))
#define SRW1500_USB_CTRL_BASE DT_REG_ADDR(DT_NODELABEL(zephyr_udc0))

/* GGC and USB controller registers used during USB device initialization */
#define SRW1500_USB_CFG_CTRL1_REG          DT_REG_ADDR_BY_NAME(DT_NODELABEL(zephyr_udc0), wrapper_ctrl1)
#define SRW1500_GGC_CLK_CTL0_REG           (SRW1500_GGC_BASE + 0x0034u)
#define SRW1500_USB_BVALOEN                BIT(6)
#define SRW1500_USB_BVALOVAL               BIT(7)
#define SRW1500_USB_DEVICE_CTRL1           0x0101D4C0u

/* GBL_SEC: PLL control */
#define SRW1500_GBL_SEC_PLL_MCU_OFFSET     0x0300u
#define SRW1500_GBL_SEC_PLL_IO_OFFSET      0x0304u

/* GBL_SEC / GBL: clock control (mirrors clkctrl's "global_sec"/"global" sub-blocks) */
#define SRW1500_GBL_SEC_CLK_OFFSET         0x0600u
#define SRW1500_GBL_CLK_OFFSET             0x0600u
#define SRW1500_CLK_MCU_MAIN_CTRL_OFFSET   0x0004u
#define SRW1500_CLK_MCU_AHB_CTRL_OFFSET    0x0008u
#define SRW1500_CLK_SER_UART1_CTRL_OFFSET  0x0004u
#define SRW1500_CLK_SER_UART2_CTRL_OFFSET  0x0008u
#define SRW1500_CLK_SER_UART3_CTRL_OFFSET  0x000Cu
#define SRW1500_CLK_DEB_GPIO_CTRL_OFFSET   0x0010u
#define SRW1500_CLK_SER_I2CM0_CTRL_OFFSET  0x0014u
#define SRW1500_CLK_SER_I2CM1_CTRL_OFFSET  0x0018u
#define SRW1500_CLK_SER_SPIM_CTRL_OFFSET   0x001Cu
#define SRW1500_CLK_SER_SPIS_CTRL_OFFSET   0x0020u
#define SRW1500_CLK_SER_I3C_CTRL_OFFSET    0x0024u
#define SRW1500_CLK_SER_XSPI_CTRL_OFFSET   0x0028u
#define SRW1500_CLK_HS_PVT_CTRL_OFFSET     0x002Cu
#define SRW1500_CLK_CORE_ADC_CTRL_OFFSET   0x0030u
#define SRW1500_CLK_PER_PWM_CTRL_OFFSET    0x0034u
#define SRW1500_CLK_SER_CAN0_CTRL_OFFSET   0x0038u
#define SRW1500_CLK_SER_CAN1_CTRL_OFFSET   0x003Cu
#define SRW1500_CLK_SER_PDM_CTRL_OFFSET    0x0040u

/* Bit positions (shift amounts), not masks */
#define SRW1500_PLL_DIV_CH_BIT   1u
#define SRW1500_PLL_DIV_CTRL_BIT 2u
#define SRW1500_PLL_DIV_CH_EN    BIT(SRW1500_PLL_DIV_CH_BIT)

/* GBL_SEC / GBL: reset control (mirrors reset controller's "global_sec"/"global" reg blocks) */
#define SRW1500_GBL_SEC_RST0_OFFSET 0x0418u
#define SRW1500_GBL_RST0_OFFSET     0x0400u
#define SRW1500_GBL_RST1_OFFSET     0x0404u

/* GBL_SEC: power control */
#define SRW1500_GBL_SEC_SYS_ROM_PWR_CTRL_OFFSET      0x0A34u
#define SRW1500_GBL_SEC_SPROT_ROM_0_PWR_CTRL_OFFSET  0x0A0Cu
#define SRW1500_GBL_SEC_BMC_BANK3_PWR_CTRL_OFFSET    0x0A88u

/* GBL: XSPI/OTF */
#define SRW1500_GBL_XSPI_SRAM_PWR_OFFSET             0x0A00u
#define SRW1500_GBL_PERIF_XSPI_CTRL0_OFFSET          0x0824u
#define SRW1500_GBL_PERIF_XSPI_CTRL1_OFFSET          0x0828u
#define SRW1500_GBL_PERIF_XSPI_CTRL2_OFFSET          0x082Cu
#define SRW1500_GBL_PERIF_XSPI_CTRL3_OFFSET          0x0830u
#define SRW1500_GBL_PERIF_XSPI_RB_VALID_TIME_OFFSET  0x0834u
#define SRW1500_GBL_PHY_DQ_TIMING_OFFSET             0x0838u
#define SRW1500_GBL_PHY_DQS_TIMING_OFFSET            0x083Cu
#define SRW1500_GBL_PHY_GATE_LPBK_OFFSET             0x0840u
#define SRW1500_GBL_PHY_DLL_MASTER_OFFSET            0x0844u
#define SRW1500_GBL_PHY_DLL_SLAVE_OFFSET             0x0848u

#define SRW1500_XSPI_CTRL_STATUS_OFFSET      0x0100u
#define SRW1500_XSPI_CTRL_INIT_COMPLETE      BIT(16)
#define SRW1500_OTF_CRYPTO_STICKY_PRSTN      BIT(5)
#define SRW1500_GBL_PERIF_XSPI_DQS_REMOD_EN  BIT(1)


#if DT_HAS_COMPAT_STATUS_OKAY(arm_ethos_u)
/* GGC: NPU clock/reset control; NPU_RESET_STATUS_REG is the Ethos-U's own base */
#define SRW1500_NPU_CLK_EN_REG          (SRW1500_GGC_BASE + 0x0034u)
#define SRW1500_NPU_CLK_EN_MASK         BIT(0)
#define SRW1500_NPU_RESET_REG           (SRW1500_GGC_BASE + 0x0030u)
#define SRW1500_NPU_RESET_RELEASE       BIT(3)
#define SRW1500_NPU_RESET_STATUS_REG    (DT_REG_ADDR(DT_NODELABEL(ethosu)) + 0x0004u)
#define SRW1500_NPU_RESET_ACTIVE        BIT(3)
#define SRW1500_NPU_RESET_WAIT_COUNT    1000u
#endif

#endif /* ZEPHYR_SOC_SYNA_ASTRA_SR_SRW1500_SOC_REGS_H_ */
