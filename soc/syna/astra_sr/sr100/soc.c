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
#include <zephyr/sys/util.h>
#include <soc.h>

#define GCR_NODE DT_NODELABEL(gcr)
#define GCR_BASE DT_REG_ADDR(GCR_NODE)
#define UART0_CLK_REG_OFFSET  0x50   /* UART offset */
#define UART1_CLK_REG_OFFSET  0x54   /* UART offset */
#define UART0_CLK_REG (GCR_BASE + UART0_CLK_REG_OFFSET)
#define UART1_CLK_REG (GCR_BASE + UART1_CLK_REG_OFFSET)

#define CLK_ENABLE2	0x04
#define I2C0_CTRL	0x58
#define I2C1_CTRL	0x5c

#define AON_CONFIG	0x50350000
#define AON_POR_RST	0x50350038

#define LP_CLKRST	0xb5007000
#define LP_CLK_ENABLE	0x00
#define LP_STICKY_RST	0x14
#define LP_UART_PORT_SEL 0x34

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

#define GLOBAL_BASE	            0x50330000U
#define I2S_APB_PERIF_RST	    0x068CU
#define I2S_APB_PERIF_RST_STS	0x0690U
#define I2S_CLK_ENABLE2	        0x0704U
#define I2S_CTRL	            0x07B4U
#define PERIF_I2S_CTRL	        0x0A88U
#define I2S_DICNTL	            0x884CU
#define I2S_FSYNCCNTL	        0x8854U
#define I2S_BCLKCNTL	        0x8858U
#define I2S_CLK_ENABLE2_ALL	    GENMASK(22, 14)
#define I2S_RST_SYNC	        BIT(10)
#define I2S_RST_TX_ALL	        GENMASK(14, 11)
#define I2S_RST_RX_ALL	        GENMASK(18, 15)
#define I2S_RST_ALL	            (I2S_RST_SYNC | I2S_RST_TX_ALL | I2S_RST_RX_ALL)
#define I2S_PAD_IE	            BIT(3)
#define I2S_PAD_PE	            BIT(4)
#define I2S_LRCK_SEL_RX_OUT	    BIT(0)
#define I2S_CTRL_TX_ENABLE	    BIT(0)
#define I2S_CTRL_RX_ENABLE	    BIT(1)
#define I2S_CTRL_TX_POL	        BIT(2)
#define I2S_CTRL_RX_POL	        BIT(3)
#define I2S_CTRL_TX_LRCK_POL	BIT(4)
#define I2S_CTRL_RX_BCLK_RST	BIT(5)
#define I2S_CTRL_TX_BCLK_RST	BIT(6)

#define ARM_SSE_COUNTER		0x58100000

void soc_sr100_lp_jpeg_clocks_enable(void)
{
	sys_write32(LPS_G1_CLK_ENABLE_MASK, LPS_G1_CLKRST + LPS_G1_CLK_ENABLE);
}

void soc_sr100_lp_jpeg_clocks_disable(void)
{
	sys_write32(0U, LPS_G1_CLKRST + LPS_G1_CLK_ENABLE);
}

#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2s0), okay)
static void soc_sr100_i2s_global_pre_config(void)
{
	const uint32_t pad_mask = I2S_PAD_IE | I2S_PAD_PE;
	uint32_t value;

	value = sys_read32(GLOBAL_BASE + PERIF_I2S_CTRL);
	value &= ~I2S_LRCK_SEL_RX_OUT;
	sys_write32(value, GLOBAL_BASE + PERIF_I2S_CTRL);

	value = sys_read32(GLOBAL_BASE + I2S_CTRL);
	value &= ~(I2S_CTRL_TX_ENABLE |
		   I2S_CTRL_RX_ENABLE |
		   I2S_CTRL_TX_POL |
		   I2S_CTRL_RX_POL |
		   I2S_CTRL_TX_LRCK_POL |
		   I2S_CTRL_RX_BCLK_RST |
		   I2S_CTRL_TX_BCLK_RST);
	sys_write32(value, GLOBAL_BASE + I2S_CTRL);

	value = sys_read32(GLOBAL_BASE + I2S_BCLKCNTL);
	value &= ~pad_mask;
	sys_write32(value, GLOBAL_BASE + I2S_BCLKCNTL);

	value = sys_read32(GLOBAL_BASE + I2S_FSYNCCNTL);
	value = (value & ~pad_mask) | I2S_PAD_IE;
	sys_write32(value, GLOBAL_BASE + I2S_FSYNCCNTL);

	value = sys_read32(GLOBAL_BASE + I2S_DICNTL);
	value = (value & ~pad_mask) | I2S_PAD_IE;
	sys_write32(value, GLOBAL_BASE + I2S_DICNTL);
}
#endif

#ifdef CONFIG_SR100_BOOTLOADER
#define GLOBAL_BOOTSTRAP			0xc
#define BOOTSTRAP_STRAP0			BIT(0)
#define BOOTSTRAP_STRAP1			BIT(1)
#define BOOTSTRAP_TEST				BIT(2)
#define GLOBAL_CLK_ENABLE1			0x700
#define GLOBAL_CPU_CLK_CTRL			0x720
#define GLOBAL_UART0_CORE_CLK_CTRL		0x750
#define GLOBAL_UART1_CORE_CLK_CTRL		0x754

#define AON_MAIN_BASE				0x50350000
#define AON_MAIN_AON_CONFIG			0x0
#define AON_MAIN_CONFIG_LPP_SECURITY		BIT(6)

#define AON_MAIN_TIMER_CONTROL			0xc
#define AON_MAIN_TIMER_ENABLE_TIMER1		BIT(3)

#define AON_MAIN_PMU_CNTL1			0x88
#define HPRC_CSEL				GENMASK(25, 20)
#define LPRC_CSEL				GENMASK(18, 15)
#define LPRC_IBIAS				GENMASK(14, 12)
#define VDD_AON_LDO_TRIM			GENMASK(3, 0)
#define VDD_ANA_LDO_TRIM			GENMASK(7, 4)
#define AON_MAIN_PMU_CNTL2			0x8c
#define ONTIMERDAC				GENMASK(28, 24)
#define OFFTIMERDAC				GENMASK(20, 16)
#define AON_MAIN_PMU_CNTL3			0x90
#define VOUT_CALIB				GENMASK(23, 19)
#define AON_MAIN_PMU_CNTL4			0x94
#define AON_MAIN_POR_CNTL			0x98
#define POR_VBG_BUF_DIV_TRIM			GENMASK(4, 0)

#define SYSPLL0_CTRLA				0xa0
#define SYSPLL0_CTRLD				0xa4
#define SYSPLL0_CTRLE				0xa8
#define SYSPLL1_CTRLA				0xac
#define SYSPLL1_CTRLD				0xb0
#define SYSPLL1_CTRLE				0xb4

#define APBL_VER_MAGIC_VAL			(0x4150424C)

#define WAIT_NOP_ANALOG_CALIB			(1000)

#define AON_MEM_BASE				0x50352000

#define LPS_G1_REGS				0xb48a0000

#define LPS_GEAR1_G1_RWTC_CTRL0_OFFSET		0x80
#define LPS_GEAR1_G1_RWTC_CTRL1_OFFSET		0x84
#define LPS_GEAR1_RWTC_UPDATE_CTRL		0x90
#define LPS_GEAR1_RWTC_UPDATE_CTRL__RWTC_FDTH_STATIC_EN	BIT(0)
#define LPS_GEAR1_RWTC_UPDATE_CTRL__UPDATE_RWTC		BIT(1)
#define LPS_GEAR1_RWTC_UPDATE_STS		0x94

#define LP_PROC_CLK_RST_LPP_RWTC_CTRL0_OFFSET	0x18
#define LP_PROC_CLK_RST_LPP_RWTC_CTRL1_OFFSET	0x1c

#define GLOBAL_SOC_RWTC_CTRL0_OFFSET		0x100
#define GLOBAL_SOC_RWTC_CTRL1_OFFSET		0x104

/// * [2:0]: EMA=011
/// * [4:3]: EMAW = 01
/// * [5]: EMAS=0
/// * [6]: RAWL=0
/// * [8:7]: RAWLM=00
/// * [9]: WABL=1
/// * [11:10]: WABLM=00
#define UCTRL0_RF1P_HDE		((3<<0) | (1<<3) | (0<<5) | (0<<6) | (0<<7) | (1<<9) | (0<<10))
/// * [1:0]:  RTSEL = 10 (EMA[1:0])
/// * [3:2]: PTSEL = 10 (KEN,EMA[2])
/// * [5:4]: TRB = 00

#define UCTRL0_ROM		((2<<0) | (2<<2) | (0<<4))
/// * [2:0]: EMAA=011
/// * [5:3]: EMAB = 011
/// * [6]: EMAS=0
/// * [7]: WABL=1
/// * [9:8]: WABLM=00
/// * [11:10]: EMAWA=10
/// * [13:12]: EMAWB=10

#define UCTRL0_SRDP_HDE		((3<<0) | (3<<3) | (0<<6) | (1<<7) | (0<<8) | (2<<10) | (2<<12))
/// * [2:0]: EMA=010
/// * [4:3]: EMAW = 00
/// * [5]: EMAS=0
/// * [6]: RAWL=1
/// * [8:7]: RAWLM=01
/// * [9]: WABL=1
/// * [12:10]: WABLM=000
/// * [13]: STOV=0

#define UCTRL1_SR1P_UHDE	((2<<0) | (0<<3) | (0<<5) | (1<<6) | (1<<7) | (1<<9) | (0<<10) | (0<<13))
/// * [2:0]: EMAA=011
/// * [5:3]: EMAB = 011
/// * [6]: EMASA=0
/// * [7]: WABL=0
/// * [9:8]: WABLM=00
/// * [10]: STOV=0
#define UCTRL1_RF2P_HD2C	((3<<0) | (3<<3) | (0<<6) | (0<<7) | (0<<8) | (0<<10))

/**
 * @union
 * @brief Union type to access the OTP PRC_CAL reg
 *
 */
typedef union
{
	struct {
		uint32_t hprc_csel:6;   // bit: 0..5
		uint32_t reserved1:4;   // bit: 6..9
		uint32_t hprc_calib:1;  // bit: 10
		uint32_t lprc_ibias:3;  // bit: 11..13
		uint32_t lprc_csel:4;   // bit: 14..17
		uint32_t lprc_calib:1;  // bit: 18
		uint32_t reserved2:13;  // bit: 19..31
	} bits;                   // Structure used for bit  access
	uint32_t word;            // Type      used for word access
} PRC_OTP;

/**
 * @union
 * @brief Union type to access the OTP BUCK_CAL reg.
 *
 */
typedef union
{
	struct {
		uint32_t onTimerDac:5;  // bit: 0..4
		uint32_t offTimerDac:5; // bit: 5..9
		uint32_t BUCK_calib:1;  // bit: 10
		uint32_t reserved1:21;  // bit: 11..31
	} bits;                   // Structure used for bit  access
	uint32_t word;            // Type      used for word access
} BUCK_OTP;

/**
 * @union
 * @brief Union type to access the OTP BG_CAL reg.
 *
 */
typedef union
{
	struct {
		uint32_t por_vbg_buf_div_trim:5; // bit: 0..4
		uint32_t reserved1:5;            // bit: 5..9
		uint32_t PMU_calib:1;            // bit: 10
		uint32_t reserved2:5;            // bit: 11..15
		uint32_t vout_calib:5;           // bit: 16..20
		uint32_t reserved3:3;            // bit: 21..23
		uint32_t vdd_ana_ldo_trim:4;     // bit: 24..27
		uint32_t vdd_aon_ldo_trim:4;     // bit: 28..31
	} bits;                            // Structure used for bit  access
	uint32_t word;                     // Type      used for word access
} PMU_OTP;

/**
 * @struct
 * @brief Calibration Parameters
 *
 */
typedef struct
{
	PRC_OTP		PRC_CAL;
	PMU_OTP		PMU_CAL;
	BUCK_OTP	BUCK_CAL;
} st_CALIB_PARAMS;

static inline void busy_loop(void)
{
	for (int i = 0; i < 100000; i++)
		;
}

static void p_set_analog_calib_params(st_CALIB_PARAMS *calib_params)
{
	uint32_t val;

	if (calib_params->PRC_CAL.bits.hprc_calib) {
		// hprc_calib field of OTP will never be programmed as 1.
		// SW need to calibrate HPRC/LPRC before using those clocks
		val = sys_read32(AON_MAIN_BASE + AON_MAIN_PMU_CNTL1);
		val &= ~HPRC_CSEL;
		val |= FIELD_PREP(HPRC_CSEL, calib_params->PRC_CAL.bits.hprc_csel);
		sys_write32(val, AON_MAIN_BASE + AON_MAIN_PMU_CNTL1);
	}
	if (calib_params->PRC_CAL.bits.lprc_calib) {
		val = sys_read32(AON_MAIN_BASE + AON_MAIN_PMU_CNTL1);
		val &= ~(LPRC_CSEL | LPRC_IBIAS);
		val |= FIELD_PREP(LPRC_CSEL, calib_params->PRC_CAL.bits.lprc_csel);
		val |= FIELD_PREP(LPRC_IBIAS, calib_params->PRC_CAL.bits.lprc_ibias);
		sys_write32(val, AON_MAIN_BASE + AON_MAIN_PMU_CNTL1);
	}

	if (calib_params->BUCK_CAL.bits.BUCK_calib) {
		// BUCK_calib field defined in OTP is obsolete and will never be programmed as 1
		val = sys_read32(AON_MAIN_BASE + AON_MAIN_PMU_CNTL2);
		val &= ~ONTIMERDAC;
		val &= ~OFFTIMERDAC;
		val |= calib_params->BUCK_CAL.bits.onTimerDac << 24;
		val |= calib_params->BUCK_CAL.bits.offTimerDac << 16;
		sys_write32(val, AON_MAIN_BASE + AON_MAIN_PMU_CNTL2);
	}

	calib_params->PMU_CAL.bits.vdd_aon_ldo_trim = 0x7;
	calib_params->PMU_CAL.bits.vdd_ana_ldo_trim = 0x7;
	if (calib_params->PMU_CAL.bits.PMU_calib) {
		val = sys_read32(AON_MAIN_BASE + AON_MAIN_POR_CNTL);
		val &= ~POR_VBG_BUF_DIV_TRIM;
		val |= calib_params->PMU_CAL.bits.por_vbg_buf_div_trim << 0;
		sys_write32(val, AON_MAIN_BASE + AON_MAIN_POR_CNTL);

		val = sys_read32(AON_MAIN_BASE + AON_MAIN_PMU_CNTL3);
		val &= ~VOUT_CALIB;
		val |= calib_params->PMU_CAL.bits.vout_calib << 19;
		sys_write32(val, AON_MAIN_BASE + AON_MAIN_PMU_CNTL3);

		val = sys_read32(AON_MAIN_BASE + AON_MAIN_PMU_CNTL1);
		val &= ~VDD_AON_LDO_TRIM;
		val &= ~VDD_ANA_LDO_TRIM;
		val |= calib_params->PMU_CAL.bits.vdd_aon_ldo_trim << 0;
		val |= calib_params->PMU_CAL.bits.vdd_ana_ldo_trim << 4;
		sys_write32(val, AON_MAIN_BASE + AON_MAIN_PMU_CNTL1);
	}

	busy_loop(); /* WAIT_NOP_ANALOG_CALIB */
}

static void p_rwtc_cfg_0p9(void)
{
	uint32_t val;
	uint32_t update_sts = 0;

	/* Write 1 to clear the rwtc_update_done flag/bit */
	sys_write32(1, LPS_G1_REGS + LPS_GEAR1_RWTC_UPDATE_STS);

	val = sys_read32(LPS_G1_REGS + LPS_GEAR1_RWTC_UPDATE_CTRL);
	// 0: Software to go through update procedure to take the new RWTC into effect
	val &= ~(LPS_GEAR1_RWTC_UPDATE_CTRL__RWTC_FDTH_STATIC_EN |
		 LPS_GEAR1_RWTC_UPDATE_CTRL__UPDATE_RWTC); // make sure RWTC update process is not triggered
	sys_write32(val, LPS_G1_REGS + LPS_GEAR1_RWTC_UPDATE_CTRL);

	sys_write32((UCTRL0_RF1P_HDE << 0) | (UCTRL0_ROM << 12) | (UCTRL0_SRDP_HDE << 18),
		    LP_CLKRST + LP_PROC_CLK_RST_LPP_RWTC_CTRL0_OFFSET);
	sys_write32((UCTRL1_SR1P_UHDE << 0) | (UCTRL1_RF2P_HD2C << 14),
		    LP_CLKRST + LP_PROC_CLK_RST_LPP_RWTC_CTRL1_OFFSET);

	sys_write32((UCTRL0_RF1P_HDE << 0) | (UCTRL0_ROM << 12) | (UCTRL0_SRDP_HDE << 18),
		    LPS_G1_REGS + LPS_GEAR1_G1_RWTC_CTRL0_OFFSET);
	sys_write32((UCTRL1_SR1P_UHDE << 0) | (UCTRL1_RF2P_HD2C << 14),
		    LPS_G1_REGS + LPS_GEAR1_G1_RWTC_CTRL1_OFFSET);

	sys_write32((UCTRL0_RF1P_HDE << 0) | (UCTRL0_ROM << 12) | (UCTRL0_SRDP_HDE << 18),
		    GLOBAL_BASE + GLOBAL_SOC_RWTC_CTRL0_OFFSET);
	sys_write32((UCTRL1_SR1P_UHDE << 0) | (UCTRL1_RF2P_HD2C << 14),
		    GLOBAL_BASE + GLOBAL_SOC_RWTC_CTRL1_OFFSET);

	val = sys_read32(LPS_G1_REGS + LPS_GEAR1_RWTC_UPDATE_CTRL);
	val |= LPS_GEAR1_RWTC_UPDATE_CTRL__UPDATE_RWTC;
	sys_write32(val, LPS_G1_REGS + LPS_GEAR1_RWTC_UPDATE_CTRL);

	// polling on 'done' flag to ensure RWTC update ended
	do {
		update_sts = sys_read32(LPS_G1_REGS + LPS_GEAR1_RWTC_UPDATE_STS);
	} while (!update_sts);

	busy_loop();
}

#define VOUT GENMASK(19, 16)
static void p_set_vcore_900mV(void)
{
	uint32_t val;

	val = sys_read32(AON_MAIN_BASE + AON_MAIN_PMU_CNTL3);
	val &= ~VOUT;
	val |= FIELD_PREP(VOUT, 0x5);
	sys_write32(val, AON_MAIN_BASE + AON_MAIN_PMU_CNTL3);

	busy_loop();
}

static bool p_is_pll_strap(void)
{
	uint32_t test_val = 0, strap0_val = 0;
	int ret = true;

	test_val = sys_read32(GLOBAL_BASE + GLOBAL_BOOTSTRAP) & BOOTSTRAP_TEST;
	strap0_val = sys_read32(GLOBAL_BASE + GLOBAL_BOOTSTRAP) & BOOTSTRAP_STRAP0;
	ret = ((0 == strap0_val) && test_val) ? true : false;

	return ret;
}

#define M55_APBL_VER_MAGIC_OFFSET	(0x44) // offset of the APBL magic number variable in the AON struct
#define M55_APBL_VER_NUMBER_OFFSET	(0x48) // offset of the APBL version number in the AON struct

#define VER_NUM_SIZE_IN_BYTES 4
const uint8_t VER_NUM[VER_NUM_SIZE_IN_BYTES] = {0x5B,0xB0,0x01,0x40}; // holds the APBL version number. [sabre prefix][chip][major num][minor num]

static uint32_t p_opcode_get_apbl_ver(void)
{
	uint32_t u32_ver = ((VER_NUM[0] << 24) | (VER_NUM[1] << 16) | (VER_NUM[2] << 8) | (VER_NUM[3]));

	return u32_ver;
}

#define OCD_CLIMIT GENMASK(6, 4)
static void p_set_ocd_limit(void)
{
	uint32_t val;

	val = sys_read32(AON_MAIN_BASE + AON_MAIN_PMU_CNTL3);
	val &= ~(OCD_CLIMIT);
	val |= 0x7 << 4; // increase current limit to 600mA
	sys_write32(val, AON_MAIN_BASE + AON_MAIN_PMU_CNTL3);

	val = sys_read32(AON_MAIN_BASE + AON_MAIN_PMU_CNTL4);
	val |= BIT(15); /* This bit controls a select line internal to PMU to select ocd_limit */
	sys_write32(val, AON_MAIN_BASE + AON_MAIN_PMU_CNTL4);
}

#define M55_SAU_CTRL		0xE000EDD0 // SAU Control Register
#define SAU_ENABLED		1

#define SACFG_BASE_ADDRESS	0x50080000
#define SACFG_NSMSCEXP_OFFSET	0x0D0

static int crypto_config_security_access(void)
{
	uint32_t val;

	__DMB();
	*(volatile unsigned int *)M55_SAU_CTRL = SAU_ENABLED;
	__DSB();
	__ISB();

	// set all MSC (master security controller) from non-secure to secure mode;
	sys_write32(0, SACFG_BASE_ADDRESS + SACFG_NSMSCEXP_OFFSET);

	val = sys_read32(AON_MAIN_BASE + AON_MAIN_AON_CONFIG);
	val |= AON_MAIN_CONFIG_LPP_SECURITY;
	sys_write32(val, AON_MAIN_BASE + AON_MAIN_AON_CONFIG);

	return 0;
}

#define LPS_GEAR1_REF_PLL	0x04
#define REF_PLL0_CLK_SEL0	BIT(0)
#define REF_PLL0_CLK_SEL1	BIT(1)
#define GEAR3_CLK_SEL		BIT(3)
#define REF_PLL1_CLK_SEL0	BIT(5)
#define REF_PLL1_CLK_SEL1	BIT(6)
#define LPS_GEAR1_PLL_STATUS	0x0c
#define PLL0_LOCK		BIT(1)
#define PLL1_LOCK		BIT(2)

#define SYSPLL0_CLKOUT		0
#define SYSPLL1_CLKOUT		0

#define CPU_CLK_CTRL_SWITCH	BIT(2)
#define CLK_CTRL_SEL		GENMASK(6, 4)

static void clk_prepare_clks_init(void)
{
	uint32_t val;

	val = sys_read32(LPS_G1_REGS + LPS_GEAR1_REF_PLL);
	val &= ~GEAR3_CLK_SEL;
	sys_write32(val, LPS_G1_REGS + LPS_GEAR1_REF_PLL);

	val = sys_read32(GLOBAL_BASE + GLOBAL_CPU_CLK_CTRL);
	val &= ~CPU_CLK_CTRL_SWITCH;
	sys_write32(val, GLOBAL_BASE + GLOBAL_CPU_CLK_CTRL);

	val = sys_read32(GLOBAL_BASE + GLOBAL_UART0_CORE_CLK_CTRL);
	val &= ~CLK_CTRL_SEL;
	val |= FIELD_PREP(CLK_CTRL_SEL, 2); // div by 4
	//sys_write32(val, GLOBAL_BASE + GLOBAL_UART0_CORE_CLK_CTRL);

	val = sys_read32(GLOBAL_BASE + GLOBAL_UART1_CORE_CLK_CTRL);
	val &= ~CLK_CTRL_SEL;
	val |= FIELD_PREP(CLK_CTRL_SEL, 2); // div by 4
	//sys_write32(val, GLOBAL_BASE + GLOBAL_UART1_CORE_CLK_CTRL);
}

typedef struct clk_pll_pre_calc_config
{
	uint32_t input_rate;
	uint32_t output_rate;
	uint32_t ref_div;
	uint32_t fbdiv;
	uint32_t frac;
	uint32_t postdiv0;
	uint32_t postdiv1;
} st_clk_pll_pre_calc_config;

static st_clk_pll_pre_calc_config pll0_rates = {
	/* in rate ,out rate, ref_div, fbdiv, frac, postdiv0, postdiv1 */
	24000000, 400000000, 2, 100, 0, 2, 3/*1*/ /* CLK_PLL_CFG_HIGH */
};

static st_clk_pll_pre_calc_config pll1_rates = {
	/* in rate , out rate , ref_div, fbdiv, frac, postdiv0, postdiv1 */
	24000000, 400000000, 2, 100, 0, 2, 3/*1*/ /* CLK_PLL_CFG_HIGH */
};

#define CTRL_A_BYPASS		BIT(1)
#define CTRL_A_DACPD		BIT(5)
#define CTRL_A_DSMPD		BIT(6)
#define CTRL_A_FOUTVCOPD	BIT(11)
#define CTRL_A_REFDIV_OFFSET	12
#define CTRL_A_FBDIV_OFFSET	18

static int clk_soc_set_pll_config(int clk_id)
{
	uint32_t ref_div_fb_div_control;
	int32_t timeout_us;
	uint32_t val;

	/* Configure PLL */
	if (clk_id == SYSPLL0_CLKOUT) {
		ref_div_fb_div_control = (pll0_rates.ref_div << CTRL_A_REFDIV_OFFSET) | \
					 (pll0_rates.fbdiv << CTRL_A_FBDIV_OFFSET);

		/* Enable bypass */
		val = sys_read32(LPS_G1_REGS + LPS_GEAR1_REF_PLL);
		val &= ~REF_PLL0_CLK_SEL0;
		val &= ~REF_PLL0_CLK_SEL1;
		sys_write32(val, LPS_G1_REGS + LPS_GEAR1_REF_PLL);

		/* Set the dividers */
		sys_write32(CTRL_A_BYPASS | CTRL_A_DACPD | \
			     CTRL_A_DSMPD | CTRL_A_FOUTVCOPD | ref_div_fb_div_control,
			    AON_MAIN_BASE + SYSPLL0_CTRLA);

		sys_write32(pll0_rates.frac, AON_MAIN_BASE + SYSPLL0_CTRLD);

		val = sys_read32(AON_MAIN_BASE + SYSPLL0_CTRLE);
		val &= ~0xff;
		val |= pll0_rates.postdiv0;
		val |= pll0_rates.postdiv1 << 4;
		sys_write32(val, AON_MAIN_BASE + SYSPLL0_CTRLE);

		/* Disable VCO PD */
		if (pll0_rates.frac == 0) {
			sys_write32(CTRL_A_BYPASS | CTRL_A_DACPD | CTRL_A_DSMPD |
				    ref_div_fb_div_control, AON_MAIN_BASE + SYSPLL0_CTRLA);
		} else {
			sys_write32(CTRL_A_BYPASS |
				    ref_div_fb_div_control, AON_MAIN_BASE + SYSPLL0_CTRLA);
		}

		/* Wait for PLL lock */
		timeout_us = 1000;
		while (timeout_us > 0) {
			if (sys_read32(LPS_G1_REGS + LPS_GEAR1_PLL_STATUS) & PLL0_LOCK) {
				break;
			}
			timeout_us--;
		}

		if (timeout_us <= 0) {
			return -1;
		}

		/* Enable post div */
		sys_write32((0xF << 1) | (0xE << 7 /*PLL_FOUTPOSTDIVPD_POS*/) |
			    ref_div_fb_div_control, AON_MAIN_BASE + SYSPLL0_CTRLA);

		/* Disable bypass */
		//sys_write32((0xE << 1) | (0xE << 7 /*PLL_FOUTPOSTDIVPD_POS*/) |
		sys_write32((0x0 << 1) | (0xE << 7 /*PLL_FOUTPOSTDIVPD_POS*/) | 0x800 |
			    ref_div_fb_div_control, AON_MAIN_BASE + SYSPLL0_CTRLA);

		/* Disable global bypass */
		val = sys_read32(LPS_G1_REGS + LPS_GEAR1_REF_PLL);
		//val |= REF_PLL0_CLK_SEL0;
		val |= REF_PLL0_CLK_SEL1;
		sys_write32(val, LPS_G1_REGS + LPS_GEAR1_REF_PLL);
	} else {
		/* Configure PLL1 */
		ref_div_fb_div_control = (pll1_rates.ref_div << CTRL_A_REFDIV_OFFSET) | \
					 (pll1_rates.fbdiv << CTRL_A_FBDIV_OFFSET);

		/* Enable bypass */
		val = sys_read32(LPS_G1_REGS + LPS_GEAR1_REF_PLL);
		val &= ~REF_PLL1_CLK_SEL0;
		val &= ~REF_PLL1_CLK_SEL1;
		sys_write32(val, LPS_G1_REGS + LPS_GEAR1_REF_PLL);

		/* Set the dividers */
		sys_write32(CTRL_A_BYPASS | CTRL_A_DACPD | \
			    CTRL_A_DSMPD | CTRL_A_FOUTVCOPD | ref_div_fb_div_control,
			    AON_MAIN_BASE + SYSPLL1_CTRLA);
		sys_write32(pll0_rates.frac, AON_MAIN_BASE + SYSPLL1_CTRLD);

		val = sys_read32(AON_MAIN_BASE + SYSPLL1_CTRLE);
		val &= 0xff;
		val |= pll1_rates.postdiv0;
		val |= pll1_rates.postdiv1 << 4;
		sys_write32(val, AON_MAIN_BASE + SYSPLL1_CTRLE);

		/* Disable VCO PD */
		if (pll1_rates.frac == 0) {
			sys_write32(CTRL_A_BYPASS | CTRL_A_DACPD | CTRL_A_DSMPD |
				    ref_div_fb_div_control, AON_MAIN_BASE + SYSPLL1_CTRLA);
		} else {
			sys_write32(CTRL_A_BYPASS |
				    ref_div_fb_div_control, AON_MAIN_BASE + SYSPLL1_CTRLA);
		}

		/* Wait for PLL lock */
		timeout_us = 1000;
		while (timeout_us > 0) {
			if (sys_read32(LPS_G1_REGS + LPS_GEAR1_PLL_STATUS) & PLL1_LOCK) {
				break;
			}
			timeout_us--;
		}

		if (timeout_us <= 0) {
			return -1;
		}

		/* Enable post div */
		sys_write32((0xF << 1) | (0xE << 7 /*PLL_FOUTPOSTDIVPD_POS*/) |
			    ref_div_fb_div_control, AON_MAIN_BASE + SYSPLL1_CTRLA);

		/* Disable bypass */
		sys_write32((0xE << 1) | (0xE << 7 /*PLL_FOUTPOSTDIVPD_POS*/) |
			    ref_div_fb_div_control, AON_MAIN_BASE + SYSPLL1_CTRLA);

		/* Disable global bypass */
		val = sys_read32(LPS_G1_REGS + LPS_GEAR1_REF_PLL);
		val |= REF_PLL1_CLK_SEL0;
		val |= REF_PLL1_CLK_SEL1;
		sys_write32(val, LPS_G1_REGS + LPS_GEAR1_REF_PLL);
	}

	return 0;
}

static int clk_pll_init(void)
{
	uint32_t val;

	clk_soc_set_pll_config(SYSPLL0_CLKOUT);
	clk_soc_set_pll_config(SYSPLL1_CLKOUT);

	val = sys_read32(GLOBAL_BASE + GLOBAL_CLK_ENABLE1);
	val |= BIT(2); /* AXI */
	sys_write32(val, GLOBAL_BASE + GLOBAL_CLK_ENABLE1);

	return 0;
}

#define CRYPTO_OTP_DUMP_START_ADDRESS	0x33F00000
#define CRYPTO_OTP_DUMP_ANALOG_BUCK_CAL	(0x03C8)
#define CRYPTO_OTP_DUMP_ANALOG_BG_CAL	(0x03CC)
#define CRYPTO_OTP_DUMP_ANALOG_PRC_CAL	(0x03D0)

static int crypto_get_calibration_regs(uint32_t *prc_cal, uint32_t *bg_cal, uint32_t *buck_cal)
{
	*prc_cal  = sys_read32(CRYPTO_OTP_DUMP_START_ADDRESS + CRYPTO_OTP_DUMP_ANALOG_PRC_CAL);
	*bg_cal   = sys_read32(CRYPTO_OTP_DUMP_START_ADDRESS + CRYPTO_OTP_DUMP_ANALOG_BG_CAL);
	*buck_cal = sys_read32(CRYPTO_OTP_DUMP_START_ADDRESS + CRYPTO_OTP_DUMP_ANALOG_BUCK_CAL);

	return 0;
}

static int bootloader(void)
{
	st_CALIB_PARAMS calib_params;
	uint32_t ret;
	uint32_t apbl_ver_number;
	uint32_t apbl_magic_number;
	uint32_t val;

	val = sys_read32(AON_MAIN_BASE + AON_MAIN_TIMER_CONTROL);
	val &= ~AON_MAIN_TIMER_ENABLE_TIMER1;
	sys_write32(val, AON_MAIN_BASE + AON_MAIN_TIMER_CONTROL);

	// Save APBL version number in AON memory for later use by the FW
	apbl_ver_number = p_opcode_get_apbl_ver();
	apbl_magic_number = APBL_VER_MAGIC_VAL;
	// write version number in AON memory
	sys_write32(apbl_ver_number, AON_MEM_BASE + M55_APBL_VER_NUMBER_OFFSET);
	// write magic number to indicate version number is valid
	sys_write32(apbl_magic_number, AON_MEM_BASE + M55_APBL_VER_MAGIC_OFFSET);

	ret = crypto_config_security_access(); // Set MPC, PPC and SAU
	if (ret == 0) {
		if (p_is_pll_strap()) {
			// Get calib params (ask BCM for the calibration data for HPRC, LPRC and Buck)
			ret = crypto_get_calibration_regs(&calib_params.PRC_CAL.word,
							  &calib_params.PMU_CAL.word,
							  &calib_params.BUCK_CAL.word);
			if (ret == 0) {
				// Set calib params into analog regs
				p_set_analog_calib_params(&calib_params);
				/* Change EMA values to 0.9 via RWTC (Read Write Timing control)
				 * configuration to 0.9V prior to clock increasing
				 */
				p_rwtc_cfg_0p9();
				p_set_vcore_900mV();		// Set output level into 900mV
				clk_prepare_clks_init();	// maybe after clk_pll_init()
				ret = clk_pll_init();		// Switch CPU_CLK into 400Mhz
				//p_set_ocd_limit();
			}
		}
	}

	return ret;
}
#endif

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

#ifdef CONFIG_SR100_BOOTLOADER
	bootloader();
#endif

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

#ifdef CONFIG_BOARD_SR100_RDK_SR100_M4
#if DT_NODE_HAS_STATUS(DT_NODELABEL(ns16550_lp_uart), okay)
	/* Set LP UART instance selector to value 2 (UARTLP on CM4) */
	sys_write32(0x2, LP_CLKRST + LP_UART_PORT_SEL);

	/* Enable LP UART clock and release reset */
	value = sys_read32(LP_CLKRST + LP_CLK_ENABLE);
	value |= BIT(3);
	sys_write32(value, LP_CLKRST + LP_CLK_ENABLE);

	value = sys_read32(LP_CLKRST + LP_STICKY_RST);
	value |= BIT(2) | BIT(3);
	sys_write32(value, LP_CLKRST + LP_STICKY_RST);
#endif
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
	sys_write32(0x45, UART1_CLK_REG);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(dma0), okay)
	 /* enable trig-in */
	 sys_write32(0xffffffff, SOC_BASE + DMA0_TRIG_EN);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(dma1), okay)
	/* enable trig-in */
	sys_write32(0xffffffff, SOC_BASE + DMA1_TRIG_EN);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2s0), okay)
	value = sys_read32(GLOBAL_BASE + I2S_CLK_ENABLE2);
	value |= I2S_CLK_ENABLE2_ALL;
	sys_write32(value, GLOBAL_BASE + I2S_CLK_ENABLE2);

	value = sys_read32(GLOBAL_BASE + I2S_APB_PERIF_RST);
	value |= I2S_RST_ALL;
	sys_write32(value, GLOBAL_BASE + I2S_APB_PERIF_RST);
	(void)sys_read32(GLOBAL_BASE + I2S_APB_PERIF_RST_STS);

	value &= ~I2S_RST_ALL;
	sys_write32(value, GLOBAL_BASE + I2S_APB_PERIF_RST);
	(void)sys_read32(GLOBAL_BASE + I2S_APB_PERIF_RST_STS);

	soc_sr100_i2s_global_pre_config();
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(timer0), okay)
	sys_write32(1, ARM_SSE_COUNTER);
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

#define LP_MEMA 0x34000000

void soc_late_init_hook(void)
{
#ifdef CONFIG_SR100_RELEASE_M4_RESET
#ifdef CONFIG_BOOTLOADER_MCUBOOT
	memcpy((void *)LP_MEMA, (void *)(LP_MEMA + 0x400), 0x400);
	(void)sys_cache_data_flush_range((void *)LP_MEMA, 0x400);
#endif

	/* Take M4 out of reset (AON_MAIN - AON_CONFIG/LPP_DEBUG_MODE = running) */
	uint32_t value = sys_read32(AON_CONFIG);

	value &= ~(0x3 << 8);
	sys_write32(value, AON_CONFIG);
#endif
}
