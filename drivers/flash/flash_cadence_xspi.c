/*
 * Copyright (c) 2025 Synaptics, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT cdns_xspi

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/cache.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>
#include "spi_nor.h"
#include "jesd216.h"

LOG_MODULE_REGISTER(xspi, CONFIG_FLASH_LOG_LEVEL);

struct cad_xspi_params {
	uintptr_t reg_base;
	uintptr_t xip_base;
	uint32_t xip_size;
	uintptr_t program_base;
};

struct flash_cad_xspi_priv {
	DEVICE_MMIO_NAMED_RAM(reg_base);
	DEVICE_MMIO_NAMED_RAM(xip_base);
	DEVICE_MMIO_NAMED_RAM(program_base);
	uint32_t xspi_base;
	uint32_t xip_base;
	struct cad_xspi_params params;
};

struct flash_cad_xspi_config {
	DEVICE_MMIO_NAMED_ROM(xspi_reg);
	DEVICE_MMIO_NAMED_ROM(xspi_xip);
	DEVICE_MMIO_NAMED_ROM(xspi_program);
	const struct pinctrl_dev_config *pcfg;
	bool use_direct_mode;
#if defined(CONFIG_CLOCK_CONTROL)
	const struct device *clk_dev;
	const clock_control_subsys_t clk_id;
	const uint32_t assigned_clock_rate;
#endif /* CONFIG_CLOCK_CONTROL */
#if defined(CONFIG_RESET)
	const struct reset_dt_spec reset;
#endif /* CONFIG_RESET */
};

#define DEV_DATA(dev) ((struct flash_cad_xspi_priv *)((dev)->data))
#define DEV_CFG(dev)  ((struct flash_cad_xspi_config *)((dev)->config))

static const struct flash_parameters flash_cad_xspi_parameters = {
	.write_block_size = SPI_NOR_PAGE_SIZE,
	.erase_value = 0xff,
};

/* CTRL regs base addresses */
#define XSPI_CTRL_CMD_REG0            0x000
#define XSPI_CTRL_CMD_REG1            0x004
#define XSPI_CTRL_CMD_REG2            0x008
#define XSPI_CTRL_CMD_REG3            0x00c
#define XSPI_CTRL_CMD_REG4            0x010
#define XSPI_CTRL_CMD_STATUS          0x044
#define XSPI_CTRL_CTRL_STATUS         0x100
#define XSPI_CTRL_INTR_STATUS         0x110
#define XSPI_CTRL_INTR_ENABLE         0x114
#define XSPI_CTRL_CTRL_CONFIG         0x230
#define XSPI_CTRL_DMA_SETTINGS        0x23c
#define XSPI_CTRL_SDMA_SIZE           0x240
#define XSPI_CTRL_SDMA_TRD_INFO       0x244
#define XSPI_CTRL_XIP_MODE_CFG        0x388
#define XSPI_CTRL_GLOBAL_SEQ_CFG      0x390
#define XSPI_CTRL_GLOBAL_SEQ_CFG_1    0x394
#define XSPI_CTRL_DIRECT_ACCESS_CFG   0x398
#define XSPI_CTRL_DIRECT_ACCESS_RMP   0x39c
#define XSPI_CTRL_DIRECT_ACCESS_RMP_1 0x3a0
#define XSPI_CTRL_PROG_SEQ_CFG_0      0x420
#define XSPI_CTRL_PROG_SEQ_CFG_1      0x424
#define XSPI_CTRL_PROG_SEQ_CFG_2      0x428
#define XSPI_CTRL_READ_SEQ_CFG_0      0x430
#define XSPI_CTRL_READ_SEQ_CFG_1      0x434
#define XSPI_CTRL_READ_SEQ_CFG_2      0x438
#define XSPI_CTRL_WE_SEQ_CFG_0        0x440
#define XSPI_CTRL_STAT_SEQ_CFG_0      0x450
#define XSPI_CTRL_STAT_SEQ_CFG_1      0x454
#define XSPI_CTRL_STAT_SEQ_CFG_2      0x458
#define XSPI_CTRL_STAT_SEQ_CFG_3      0x45c
#define XSPI_CTRL_STAT_SEQ_CFG_4      0x460
#define XSPI_CTRL_STAT_SEQ_CFG_5      0x464
#define XSPI_CTRL_STAT_SEQ_CFG_7      0x46c
#define XSPI_CTRL_STAT_SEQ_CFG_8      0x470
#define XSPI_CTRL_STAT_SEQ_CFG_10     0x478
#define XSPI_CTRL_VERSION             0xf00
#define XSPI_CTRL_FEATURES_REG        0xf04

/* CTRL regs values */
#define XSPI_CTRL_SDMA_ERR_EN     BIT(22)
#define XSPI_CTRL_SDMA_TRIGG_EN   BIT(21)
#define XSPI_CTRL_WORK_MODE_SHIFT 5
#define XSPI_CTRL_STATUS_BUSY     BIT(7)
#define XSPI_CTRL_PAGE_SIZE_256   0x8f

/* PHY regs base addresses */
#define XSPI_PHY_DLL_CTRL_REG        0x1034
#define XSPI_PHY_DQ_TIMING_REG       0x2000
#define XSPI_PHY_DQS_TIMING_REG      0x2004
#define XSPI_PHY_GATE_LPBK_CTRL_REG  0x2008
#define XSPI_PHY_DLL_MASTER_CTRL_REG 0x200c
#define XSPI_PHY_DLL_SLAVE_CTRL_REG  0x2010
#define XSPI_PHY_IE_TIMING_REG       0x2014
#define XSPI_PHY_DLL_OBS_REG_0       0x201c
#define XSPI_PHY_CTRL_REG            0x2080

/* Device configuration registers */
#define XSPI_DEV_WP_SETTINGS         0x1000
#define XSPI_DEV_RESET_PIN_SETTINGS  0x1004
#define XSPI_DEV_CLOCK_MODE_SETTINGS 0x1008
#define XSPI_DEV_JEDEC_RST_TIMING    0x100c
#define XSPI_DEV_DELAY_REG           0x1010
#define XSPI_DEV_ACTIVE_MAX_REG      0x1018
#define XSPI_DEV_DLL_PHY_UPDATE_CNT  0x1030

/* PHY regs init values */
#define XSPI_PHY_DLL_CTRL_INIT        0x1000707
#define XSPI_PHY_DQ_TIMING_INIT       0x101
#define XSPI_PHY_DQS_TIMING_INIT      0x700404
#define XSPI_PHY_GATE_LPBK_CTRL_INIT  0x200030
#define XSPI_PHY_DLL_MASTER_CTRL_INIT 0x14008e
#define XSPI_PHY_DLL_SLAVE_CTRL_INIT  0x3322
#define XSPI_PHY_IE_TIMING_INIT       0x100000
#define XSPI_PHY_CTRL_INIT            0x0

/* PHY regs values */
#define XSPI_PHY_DLL_OBS_LOCKED BIT(0)
#define XSPI_PHY_DLL_RST_N      BIT(24)

/* helpers */
#define XSPI_PHY_DLL_LOCK_TIMEOUT_MS 500
#define XSPI_CTRL_RMP_ADDR_EN        BIT(12)
#define XSPI_WAIT_LIMIT              (K_MSEC(100))

/* READ_SEQ_CFG_0 bit field positions */
#define XSPI_CTRL_READ_SEQ_CFG_0_CMD_VAL_POS      0
#define XSPI_CTRL_READ_SEQ_CFG_0_CMD_IOS_POS      8
#define XSPI_CTRL_READ_SEQ_CFG_0_CMD_EDGE_POS     11
#define XSPI_CTRL_READ_SEQ_CFG_0_ADDR_CNT_POS     12
#define XSPI_CTRL_READ_SEQ_CFG_0_ADDR_IOS_POS     16
#define XSPI_CTRL_READ_SEQ_CFG_0_ADDR_EDGE_POS    19
#define XSPI_CTRL_READ_SEQ_CFG_0_DATA_IOS_POS     20
#define XSPI_CTRL_READ_SEQ_CFG_0_DATA_EDGE_POS    23
#define XSPI_CTRL_READ_SEQ_CFG_0_DUMMY_CNT_POS    24

/* READ_SEQ_CFG_1 bit field positions */
#define XSPI_CTRL_READ_SEQ_CFG_1_CMD_EXT_EN_POS   0
#define XSPI_CTRL_READ_SEQ_CFG_1_CMD_EXT_VAL_POS  8
#define XSPI_CTRL_READ_SEQ_CFG_1_MB_DUMMY_CNT_POS 24
#define XSPI_CTRL_READ_SEQ_CFG_1_MB_EN_POS        31

/* PROG_SEQ_CFG_0 bit field positions */
#define XSPI_CTRL_PROG_SEQ_CFG_0_CMD_VAL_POS     0
#define XSPI_CTRL_PROG_SEQ_CFG_0_CMD_IOS_POS     8
#define XSPI_CTRL_PROG_SEQ_CFG_0_CMD_EDGE_POS    11
#define XSPI_CTRL_PROG_SEQ_CFG_0_ADDR_CNT_POS    12
#define XSPI_CTRL_PROG_SEQ_CFG_0_ADDR_IOS_POS    16
#define XSPI_CTRL_PROG_SEQ_CFG_0_ADDR_EDGE_POS   19
#define XSPI_CTRL_PROG_SEQ_CFG_0_DATA_IOS_POS    20
#define XSPI_CTRL_PROG_SEQ_CFG_0_DATA_EDGE_POS   23
#define XSPI_CTRL_PROG_SEQ_CFG_0_DUMMY_CNT_POS   24

/* STAT_SEQ_CFG_2 bit field positions */
#define XSPI_CTRL_STAT_SEQ_CFG_2_PROG_FAIL_CMD_VAL_POS 24

/* STAT_SEQ_CFG_5 masks */
#define XSPI_CTRL_STAT_SEQ_CFG_5_DEV_RDY_EN_MASK 0x1

/* Read sequence operation codes */
#define XSPI_OP_RD            0x03
#define XSPI_OP_RD_1_2_2      0xBB
#define XSPI_OP_RD_1_4_4      0xEB

/* Program and status operation codes */
#define XSPI_OP_PP            0x02
#define XSPI_OP_RDSR          0x05

typedef enum {
	IO1 = 0,
	IO2 = 1,
	IO4 = 2,
	IO8 = 3
} e_xspic_io_lines;

typedef enum {
	DIRECT = 0,
	STIG = 1
} e_xspic_work_mode;

#define XSPI_CMD_CFG_READ(cmd, cmd_w, addr, addr_b, addr_w, data_b, data_w, dummy)                 \
	do {                                                                                       \
		cmd_reg[0] = 0;                                                                    \
		cmd_reg[1] = 0x1 | (addr & 0xFF) << 24;                                            \
		cmd_reg[2] = (addr >> 8);                                                          \
		cmd_reg[3] = (cmd & 0xFF) << 16 | (addr_b & 0x7) << 28;                            \
		cmd_reg[4] = (addr_w & 0x3) | (cmd_w << 8);                                        \
		glue_reg[0] = 0;                                                                   \
		glue_reg[1] = 0x7F | (data_b > 2 ? 0 : 1) << 24;                                   \
		glue_reg[2] = (data_b & 0xFFFF) << 16;                                             \
		glue_reg[3] = (data_b >> 16) | (dummy << 20);                                      \
		glue_reg[4] = data_w << 8;                                                         \
	} while (0)

#define XSPI_CMD_CFG_WRITE(cmd, cmd_w, addr, addr_b, addr_w, data_b, data_w, data0, data1, dummy)  \
	do {                                                                                       \
		cmd_reg[0] = 0;                                                                    \
		cmd_reg[1] = (addr & 0xFF) << 24 | data0 << 8 | data1 << 16;                       \
		cmd_reg[2] = (addr >> 8);                                                          \
		cmd_reg[3] = (cmd & 0xFF) << 16 | (data_b > 2 ? 0 : data_b) << 24 |                \
			     (addr_b & 0x7) << 28;                                                 \
		cmd_reg[4] = (addr_w & 0x3) | (cmd_w << 8) | ((data_b > 2 ? 1 : 0) << 28);         \
		if (data_b > 2) {                                                                  \
			glue_reg[4] = (data_w << 8) | (1 << 4);                                    \
			glue_reg[3] = (data_b >> 16) | (dummy << 20);                              \
			glue_reg[2] = (data_b & 0xFFFF) << 16;                                     \
			glue_reg[1] = 0x7F;                                                        \
			glue_reg[0] = 0;                                                           \
		}                                                                                  \
	} while (0)



static void xspic_enter_reset_dll(const struct device *dev)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct cad_xspi_params *cad_params = &priv->params;
	uintptr_t base = cad_params->reg_base;
	uint32_t regval;

	regval = sys_read32(base + XSPI_PHY_DLL_CTRL_REG);
	regval &= ~XSPI_PHY_DLL_RST_N;
	sys_write32(regval, base + XSPI_PHY_DLL_CTRL_REG);
}

static int xspic_exit_reset_dll(const struct device *dev)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct cad_xspi_params *cad_params = &priv->params;
	uintptr_t base = cad_params->reg_base;
	uint32_t regval;

	regval = sys_read32(base + XSPI_PHY_DLL_CTRL_REG);
	regval |= XSPI_PHY_DLL_RST_N;
	sys_write32(regval, base + XSPI_PHY_DLL_CTRL_REG);

	if (!WAIT_FOR(sys_read32(base + XSPI_PHY_DLL_OBS_REG_0) & XSPI_PHY_DLL_OBS_LOCKED,
		      XSPI_PHY_DLL_LOCK_TIMEOUT_MS, k_msleep(100))) {
		regval = sys_read32(base + XSPI_PHY_DLL_OBS_REG_0);
		regval = sys_read32(base + XSPI_PHY_DLL_OBS_REG_0);
		LOG_ERR("Timeout while waiting for XSPI PHY DLL to lock");
		return -ETIMEDOUT;
	}
	return 0;
}

static int xspic_phy_init(const struct device *dev)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct cad_xspi_params *cad_params = &priv->params;
	uintptr_t base = cad_params->reg_base;
	int ret = 0;

	xspic_enter_reset_dll(dev);

	sys_write32(XSPI_PHY_DQ_TIMING_INIT, base + XSPI_PHY_DQ_TIMING_REG);
	sys_write32(XSPI_PHY_DQS_TIMING_INIT, base + XSPI_PHY_DQS_TIMING_REG);
	sys_write32(XSPI_PHY_GATE_LPBK_CTRL_INIT, base + XSPI_PHY_GATE_LPBK_CTRL_REG);
	sys_write32(XSPI_PHY_DLL_MASTER_CTRL_INIT, base + XSPI_PHY_DLL_MASTER_CTRL_REG);
	sys_write32(XSPI_PHY_DLL_SLAVE_CTRL_INIT, base + XSPI_PHY_DLL_SLAVE_CTRL_REG);
	sys_write32(XSPI_PHY_CTRL_INIT, base + XSPI_PHY_CTRL_REG);
	sys_write32(XSPI_PHY_DLL_CTRL_INIT, base + XSPI_PHY_DLL_CTRL_REG);

	ret = xspic_exit_reset_dll(dev);
	return ret;
}

static void xspic_direct_read_cfg(struct cad_xspi_params *cad_params, e_xspic_io_lines bits,
				  uint32_t dummy_cnt)
{
	uint32_t reg_val;

	switch (bits) {
	case IO4:
		/* 4-bit data/address with 3-byte address */
		reg_val = (XSPI_OP_RD_1_4_4 << XSPI_CTRL_READ_SEQ_CFG_0_CMD_VAL_POS) |
			  (IO1 << XSPI_CTRL_READ_SEQ_CFG_0_CMD_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_CMD_EDGE_POS) |
			  (3 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_CNT_POS) |
			  (IO4 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_EDGE_POS) |
			  (IO4 << XSPI_CTRL_READ_SEQ_CFG_0_DATA_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_DATA_EDGE_POS) |
			  (dummy_cnt << XSPI_CTRL_READ_SEQ_CFG_0_DUMMY_CNT_POS);
		break;

	case IO2:
		/* 2-bit data/address with 3-byte address */
		reg_val = (XSPI_OP_RD_1_2_2 << XSPI_CTRL_READ_SEQ_CFG_0_CMD_VAL_POS) |
			  (IO1 << XSPI_CTRL_READ_SEQ_CFG_0_CMD_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_CMD_EDGE_POS) |
			  (3 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_CNT_POS) |
			  (IO2 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_EDGE_POS) |
			  (IO2 << XSPI_CTRL_READ_SEQ_CFG_0_DATA_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_DATA_EDGE_POS) |
			  (dummy_cnt << XSPI_CTRL_READ_SEQ_CFG_0_DUMMY_CNT_POS);
		break;

	case IO1:
	default:
		/* 1-bit data/address with 3-byte address */
		reg_val = (XSPI_OP_RD << XSPI_CTRL_READ_SEQ_CFG_0_CMD_VAL_POS) |
			  (IO1 << XSPI_CTRL_READ_SEQ_CFG_0_CMD_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_CMD_EDGE_POS) |
			  (3 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_CNT_POS) |
			  (IO1 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_EDGE_POS) |
			  (IO1 << XSPI_CTRL_READ_SEQ_CFG_0_DATA_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_DATA_EDGE_POS) |
			  (dummy_cnt << XSPI_CTRL_READ_SEQ_CFG_0_DUMMY_CNT_POS);
		break;
	}

	sys_write32(reg_val, cad_params->reg_base + XSPI_CTRL_READ_SEQ_CFG_0);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_READ_SEQ_CFG_1);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_READ_SEQ_CFG_2);
    LOG_DBG("Direct read sequence configured with cmd=0x%02x, io=%d, dummy=%d", reg_val & 0xFF,
            (reg_val >> XSPI_CTRL_READ_SEQ_CFG_0_DATA_IOS_POS) & 0x3, (reg_val >> XSPI_CTRL_READ_SEQ_CFG_0_DUMMY_CNT_POS) & 0xFF);
}

static void xspic_direct_stat_cfg(struct cad_xspi_params *cad_params)
{
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_0);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_1);
	sys_write32((XSPI_OP_RDSR << XSPI_CTRL_STAT_SEQ_CFG_2_PROG_FAIL_CMD_VAL_POS) | XSPI_OP_RDSR,
		    cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_2);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_3);
	sys_write32(XSPI_CTRL_STAT_SEQ_CFG_5_DEV_RDY_EN_MASK,
		    cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_5);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_7);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_8);
    LOG_DBG("Direct status sequence configured with RDSR cmd=0x%02x", (XSPI_OP_RDSR << XSPI_CTRL_STAT_SEQ_CFG_2_PROG_FAIL_CMD_VAL_POS) | XSPI_OP_RDSR);
}

static void xspic_direct_prog_pp_cfg(struct cad_xspi_params *cad_params)
{
	uint32_t reg_val;

	reg_val = (XSPI_OP_PP << XSPI_CTRL_PROG_SEQ_CFG_0_CMD_VAL_POS) |
		  (IO1 << XSPI_CTRL_PROG_SEQ_CFG_0_CMD_IOS_POS) |
		  (0 << XSPI_CTRL_PROG_SEQ_CFG_0_CMD_EDGE_POS) |
		  (3 << XSPI_CTRL_PROG_SEQ_CFG_0_ADDR_CNT_POS) |
		  (IO1 << XSPI_CTRL_PROG_SEQ_CFG_0_ADDR_IOS_POS) |
		  (0 << XSPI_CTRL_PROG_SEQ_CFG_0_ADDR_EDGE_POS) |
		  (IO1 << XSPI_CTRL_PROG_SEQ_CFG_0_DATA_IOS_POS) |
		  (0 << XSPI_CTRL_PROG_SEQ_CFG_0_DATA_EDGE_POS) |
		  (0 << XSPI_CTRL_PROG_SEQ_CFG_0_DUMMY_CNT_POS);

	sys_write32(reg_val, cad_params->reg_base + XSPI_CTRL_PROG_SEQ_CFG_0);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_PROG_SEQ_CFG_1);
}

static uint32_t xspic_check_interrupts(struct cad_xspi_params *cad_params, uint32_t mask)
{
	uint32_t intr;

	intr = sys_read32(cad_params->reg_base + XSPI_CTRL_INTR_STATUS);
	/* Clear interrupt status */
	if (intr != 0) {
		sys_write32(intr, cad_params->reg_base + XSPI_CTRL_INTR_STATUS);
	}

	return intr & mask;
}

static int xspic_stig_wait_for_instr_end(struct cad_xspi_params *cad_params)
{
	uint32_t stig_status = 0;
	k_timepoint_t end = sys_timepoint_calc(XSPI_WAIT_LIMIT);

	do {
		stig_status = sys_read32(cad_params->reg_base + XSPI_CTRL_CMD_STATUS);
		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}
	} while (!stig_status);

	return 0;
}

static int xspic_stig_cmd(struct cad_xspi_params *cad_params, uint32_t cmd)
{
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_CMD_REG1);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_CMD_REG2);
	sys_write32(cmd << 16, cad_params->reg_base + XSPI_CTRL_CMD_REG3);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_CMD_REG4);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_CMD_REG0);

	return xspic_stig_wait_for_instr_end(cad_params);
}

static void xspic_trigger_command(struct cad_xspi_params *cad_params, uint32_t *cmd)
{
	sys_write32(cmd[4], cad_params->reg_base + XSPI_CTRL_CMD_REG4);
	sys_write32(cmd[3], cad_params->reg_base + XSPI_CTRL_CMD_REG3);
	sys_write32(cmd[2], cad_params->reg_base + XSPI_CTRL_CMD_REG2);
	sys_write32(cmd[1], cad_params->reg_base + XSPI_CTRL_CMD_REG1);
	sys_write32(cmd[0], cad_params->reg_base + XSPI_CTRL_CMD_REG0);
}

static int xspic_wait_for_idle(struct cad_xspi_params *cad_params)
{
	uint32_t status;
	k_timepoint_t end = sys_timepoint_calc(XSPI_WAIT_LIMIT);

	do {
		status = sys_read32(cad_params->reg_base + XSPI_CTRL_CTRL_STATUS);
		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}
	} while (status & XSPI_CTRL_STATUS_BUSY);

	return 0;
}

static uint32_t xspic_set_ctrl_work_mode(struct cad_xspi_params *cad_params,
					 e_xspic_work_mode new_mode)
{
	uint32_t mode;

	mode = (sys_read32(cad_params->reg_base + XSPI_CTRL_CTRL_CONFIG) >>
		XSPI_CTRL_WORK_MODE_SHIFT) &
	       0x1;
	if (mode != (uint32_t)new_mode) {
		sys_write32((uint32_t)new_mode << XSPI_CTRL_WORK_MODE_SHIFT,
			    cad_params->reg_base + XSPI_CTRL_CTRL_CONFIG);
	}

	return mode;
}

static uint32_t xspic_direct_mode_enable(struct cad_xspi_params *cad_params)
{
    uint32_t mode;
    xspic_direct_read_cfg(cad_params, IO1, 0);
    xspic_direct_stat_cfg(cad_params);
    xspic_direct_prog_pp_cfg(cad_params);
    mode = xspic_set_ctrl_work_mode(cad_params, DIRECT);

    return mode;
}

static int xspic_wait_for_data(struct cad_xspi_params *cad_params)
{
	k_timepoint_t end;
	uint32_t status_val = 0;

	/* Wait for transfer to finish */
	end = sys_timepoint_calc(XSPI_WAIT_LIMIT);
	do {
		status_val = xspic_check_interrupts(cad_params, XSPI_CTRL_SDMA_ERR_EN |
									XSPI_CTRL_SDMA_TRIGG_EN);
		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}
	} while (!status_val);

	if (status_val == XSPI_CTRL_SDMA_ERR_EN) {
		return -EIO;
	}

	return 0;
}

static void xspic_copy(void *dest, const void *src, size_t len)
{
	uint8_t *b_dest = (uint8_t *)dest;
	const uint8_t *b_src = (const uint8_t *)src;
	uint32_t *l_dest;
	const uint32_t *l_src;

	while ((uint32_t)b_dest & 3 && (len > 0)) {
		*b_dest++ = *b_src++;
		len--;
	}

	l_dest = (uint32_t *)b_dest;
	l_src = (const uint32_t *)b_src;
	while (len >= 4) {
		*l_dest++ = *l_src++;
		len -= 4;
	}

	b_dest = (uint8_t *)l_dest;
	b_src = (const uint8_t *)l_src;
	while (len-- > 0) {
		*b_dest++ = *b_src++;
	}
}

static uint32_t xspic_stig_read_data(struct cad_xspi_params *cad_params, uint32_t cmd, off_t offset,
				     uint8_t *buf, size_t len, int io4, uint32_t dummy)
{
	uint32_t cmd_reg[5];
	uint32_t glue_reg[5];
	uint32_t mode;
	int ret;

	ret = xspic_wait_for_idle(cad_params);
	if (ret < 0) {
		return ret;
	}

	/* Switch to STIG mode */
	mode = xspic_set_ctrl_work_mode(cad_params, STIG);

	XSPI_CMD_CFG_READ(cmd, (uint32_t)IO1, (uint32_t)offset, 3, (uint32_t)(io4 ? IO4 : IO1),
			  (uint32_t)len, (uint32_t)(io4 ? IO4 : IO1), dummy);
	xspic_trigger_command(cad_params, cmd_reg);
	xspic_trigger_command(cad_params, glue_reg);

	ret = xspic_wait_for_data(cad_params);
	if (ret < 0) {
		goto out;
	}

	xspic_copy(buf, (void *)cad_params->xip_base, len);
	barrier_dsync_fence_full();

	ret = xspic_stig_wait_for_instr_end(cad_params);

out:
	/* Switch back to original mode */
	xspic_set_ctrl_work_mode(cad_params, mode);

	return ret;
}

static uint32_t xspic_stig_cmd_read_status(struct cad_xspi_params *cad_params,
					   uint32_t status_reg_length, uint32_t status_cmd,
					   uint32_t *status_reg_value)
{
	uint32_t rdata;
	uint32_t cmd_reg[5];
	uint32_t glue_reg[5];
	int ret;

	XSPI_CMD_CFG_READ(status_cmd, (uint32_t)IO1, 0, 0, (uint32_t)IO1, status_reg_length,
			  (uint32_t)IO1, 0);

	xspic_trigger_command(cad_params, cmd_reg);
	xspic_trigger_command(cad_params, glue_reg);

	ret = xspic_stig_wait_for_instr_end(cad_params);
	if (ret < 0) {
		return ret;
	}

	rdata = sys_read32(cad_params->reg_base + XSPI_CTRL_CMD_STATUS);
	*status_reg_value = rdata >> 16;
	if (status_reg_length == 1) {
		*status_reg_value &= 0xff;
	}

	return 0;
}

static int xspi_set_write_enable(struct cad_xspi_params *cad_params)
{
	uint32_t sr1_value;
	k_timepoint_t end;
	int ret;

	end = sys_timepoint_calc(XSPI_WAIT_LIMIT);
	do {
		ret = xspic_stig_cmd(cad_params, SPI_NOR_CMD_WREN);
		if (ret < 0) {
			return ret;
		}

		ret = xspic_stig_cmd_read_status(cad_params, 1, SPI_NOR_CMD_RDSR, &sr1_value);
		if (ret < 0) {
			return ret;
		}

		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}
	} while (!(sr1_value & SPI_NOR_WEL_BIT));

	return 0;
}

static int xspic_poll_for_wip_bit(struct cad_xspi_params *cad_params)
{
	uint32_t sr1_value;
	k_timepoint_t end;
	int ret;

	end = sys_timepoint_calc(XSPI_WAIT_LIMIT);
	do {
		ret = xspic_stig_cmd_read_status(cad_params, 1, SPI_NOR_CMD_RDSR, &sr1_value);
		if (ret < 0) {
			return ret;
		}
		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}
	} while (sr1_value & SPI_NOR_WIP_BIT);

	return 0;
}

static int xspic_stig_cmd_write_data(struct cad_xspi_params *cad_params, uint32_t xfer_addr,
				     uint8_t *src, uint32_t size)
{
	uint32_t cmd_reg[5];
	uint32_t glue_reg[5];
	uint32_t mode = 0;
	int ret;
	unsigned int key;

	if (size > SPI_NOR_PAGE_SIZE) {
		return -EINVAL;
	}

	if (cad_params->program_base) {
		memcpy((void *)cad_params->program_base, src, size);
		barrier_dsync_fence_full();
		src = (uint8_t *)cad_params->program_base;
	}

	ret = xspic_wait_for_idle(cad_params);
	if (ret < 0) {
		return ret;
	}

	/* Switch to STIG mode */
	mode = xspic_set_ctrl_work_mode(cad_params, STIG);

	ret = xspi_set_write_enable(cad_params);
	if (ret < 0) {
		goto out;
	}

	XSPI_CMD_CFG_WRITE(SPI_NOR_CMD_PP, (uint32_t)IO1, xfer_addr, 3, (uint32_t)IO1, size,
			   (uint32_t)IO1, 0, 0, 0);
	xspic_trigger_command(cad_params, cmd_reg);
	xspic_trigger_command(cad_params, glue_reg);

	ret = xspic_wait_for_data(cad_params);
	if (ret < 0) {
		goto out;
	}

	key = irq_lock();
	xspic_copy((void *)cad_params->xip_base, src, size);
	irq_unlock(key);
	barrier_dsync_fence_full();

	ret = xspic_stig_wait_for_instr_end(cad_params);
	if (ret < 0) {
		goto out;
	}

	ret = xspic_poll_for_wip_bit(cad_params);

out:
	/* Switch back to original mode */
	xspic_set_ctrl_work_mode(cad_params, mode);

	return ret;
}

static int xspic_stig_cmd_erase(struct cad_xspi_params *cad_params, uint32_t offset)
{
	uint32_t mode;
	uint32_t cmd_reg[5];
	uint32_t glue_reg[5];
	int ret;

	ret = xspic_wait_for_idle(cad_params);
	if (ret < 0) {
		return ret;
	}

	/* Switch to STIG mode */
	mode = xspic_set_ctrl_work_mode(cad_params, STIG);

	ret = xspi_set_write_enable(cad_params);
	if (ret < 0) {
		goto out;
	}

	XSPI_CMD_CFG_WRITE(SPI_NOR_CMD_SE, IO1, offset, 3, IO1, 0, IO1, 0, 0, 0);

	xspic_trigger_command(cad_params, cmd_reg);
	xspic_stig_wait_for_instr_end(cad_params);

	ret = xspic_poll_for_wip_bit(cad_params);

out:
	/* Switch back to original mode */
	xspic_set_ctrl_work_mode(cad_params, mode);

	return ret;
}

static int flash_cad_xspi_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct flash_cad_xspi_config *config = (struct flash_cad_xspi_config *)dev->config;
	struct cad_xspi_params *cad_params = &priv->params;
	uint8_t *dest = (uint8_t *)data;

	if ((data == NULL) || (len == 0) || (offset + len > cad_params->xip_size)) {
		LOG_ERR("Invalid input parameter for XSPI Read!");
		return -EINVAL;
	}

	uint32_t size = len;
	uint32_t read_size = MIN(size, SPI_NOR_PAGE_SIZE);
	int ret = 0;

	while (size) {
		if (config->use_direct_mode) {
			/* Use direct mode with XIP access */
			xspic_copy(dest, (void *)(cad_params->xip_base + offset), read_size);
			barrier_dsync_fence_full();
            LOG_DBG("Direct read: offset=0x%08lx, size=%d\n", offset, read_size);
		} else {
			/* Use STIG mode */
			ret = xspic_stig_read_data(cad_params, 0x03, offset, dest, read_size, 0, 0);
			if (ret < 0) {
				break;
			}
		}

		offset += read_size;
		size -= read_size;
		dest += read_size;
		read_size = MIN(size, SPI_NOR_PAGE_SIZE);
	}

	return ret;
}

static int flash_cad_xspi_erase(const struct device *dev, off_t offset, size_t len)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct cad_xspi_params *cad_params = &priv->params;
	uint32_t subsector_offset = offset & (SPI_NOR_SECTOR_SIZE - 1);
	uint32_t erase_size = MIN(len, SPI_NOR_SECTOR_SIZE - subsector_offset);
	int ret = 0;

	while (len) {
		ret = xspic_stig_cmd_erase(cad_params, offset);
		if (ret < 0) {
			LOG_ERR("Cadence XSPI Flash Erase Failed!");
			break;
		}

		offset += erase_size;
		len -= erase_size;
		erase_size = MIN(len, SPI_NOR_SECTOR_SIZE);
	}

	return ret;
}

static int flash_cad_xspi_write(const struct device *dev, off_t offset, const void *data,
				size_t len)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct flash_cad_xspi_config *config = (struct flash_cad_xspi_config *)dev->config;
	struct cad_xspi_params *cad_params = &priv->params;
	uint32_t write_size = MIN(len, SPI_NOR_PAGE_SIZE);
	uint8_t *src = (uint8_t *)data;
	int ret = 0;

	if ((data == NULL) || (len == 0) || (offset + len > cad_params->xip_size)) {
		LOG_ERR("Invalid input parameter for XSPI Write!");
		return -EINVAL;
	}

	while (len) {
		if (config->use_direct_mode) {
			/* Use direct mode with configuration */
			ret = xspi_set_write_enable(cad_params);
			if (ret < 0) {
				LOG_ERR("Failed to set write enable!");
				break;
			}

			/* Copy data to XIP/program buffer */
			if (cad_params->program_base) {
				memcpy((void *)cad_params->program_base, src, write_size);
			} else {
				memcpy((void *)cad_params->xip_base, src, write_size);
			}
			barrier_dsync_fence_full();

			/* Poll for write completion */
			ret = xspic_poll_for_wip_bit(cad_params);
			if (ret < 0) {
				LOG_ERR("Cadence XSPI Flash Write polling failed!");
				break;
			}
		} else {
			/* Use STIG mode */
			ret = xspic_stig_cmd_write_data(cad_params, offset, src, write_size);
			if (ret < 0) {
				LOG_ERR("Cadence XSPI Flash Write Failed!");
				break;
			}
		}

		len -= write_size;
		offset += write_size;
		src += write_size;
		write_size = MIN(len, SPI_NOR_PAGE_SIZE);
	}

	return ret;
}

#if defined(CONFIG_FLASH_JESD216_API)
static int flash_cad_xspi_sfdp_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct cad_xspi_params *cad_params = &priv->params;

	return xspic_stig_read_data(cad_params, JESD216_CMD_READ_SFDP, offset, (uint8_t *)data, len,
				    0, 8);
}

static int flash_cad_xspi_read_jedec_id(const struct device *dev, uint8_t *id)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct cad_xspi_params *cad_params = &priv->params;

	return xspic_stig_read_data(cad_params, JESD216_CMD_READ_ID, 0, id, JESD216_READ_ID_LEN, 0,
				    0);
}
#endif /* CONFIG_FLASH_JESD216_API */

static const struct flash_parameters *flash_cad_xspi_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);

	return &flash_cad_xspi_parameters;
}

#if CONFIG_FLASH_PAGE_LAYOUT
#define FLASH_SIZE KB(CONFIG_FLASH_SIZE)

static const struct flash_pages_layout flash_cad_xspi_pages_layout = {
	.pages_count = FLASH_SIZE / SPI_NOR_SECTOR_SIZE,
	.pages_size = SPI_NOR_SECTOR_SIZE,
};

static void flash_cad_xspi_page_layout(const struct device *dev,
				       const struct flash_pages_layout **layout,
				       size_t *layout_size)
{
	*layout = &flash_cad_xspi_pages_layout;
	*layout_size = 1;
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */

static const struct flash_driver_api flash_cad_xspi_api = {
	.erase = flash_cad_xspi_erase,
	.write = flash_cad_xspi_write,
	.read = flash_cad_xspi_read,
#if defined(CONFIG_FLASH_JESD216_API)
	.sfdp_read = flash_cad_xspi_sfdp_read,
	.read_jedec_id = flash_cad_xspi_read_jedec_id,
#endif /* CONFIG_FLASH_JESD216_API */
	.get_parameters = flash_cad_xspi_get_parameters,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = flash_cad_xspi_page_layout,
#endif /* CONFIG_FLASH_PAGE_LAYOUT */
};

static int flash_cad_xspi_init(const struct device *dev)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct flash_cad_xspi_config *config = (struct flash_cad_xspi_config *)dev->config;
	struct cad_xspi_params *cad_params = &priv->params;
	int ret;

	DEVICE_MMIO_NAMED_MAP(dev, xspi_reg, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, xspi_xip, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, xspi_program, K_MEM_CACHE_NONE);

	cad_params->reg_base = DEVICE_MMIO_NAMED_GET(dev, xspi_reg);
	cad_params->xip_base = DEVICE_MMIO_NAMED_GET(dev, xspi_xip);
	cad_params->program_base = DEVICE_MMIO_NAMED_GET(dev, xspi_program);

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("XSPI pinctrl setup failed (%d)", ret);
		return ret;
	}

	sys_write32(cad_params->xip_base, cad_params->reg_base + XSPI_CTRL_DIRECT_ACCESS_RMP);
	sys_write32(XSPI_CTRL_RMP_ADDR_EN, cad_params->reg_base + XSPI_CTRL_DIRECT_ACCESS_CFG);
	/* Limit program page size to 256 bytes */
	sys_write32(XSPI_CTRL_PAGE_SIZE_256, cad_params->reg_base + XSPI_CTRL_GLOBAL_SEQ_CFG);

	if (config->use_direct_mode) {
		/* Initialize direct mode read/write configurations */
		LOG_DBG("Initializing XSPI in DIRECT mode\n");
        xspic_direct_mode_enable(cad_params);
		/* Switch to DIRECT mode */
		(void)xspic_set_ctrl_work_mode(cad_params, DIRECT);
		/* Disable Write-Enable command in direct mode */
		sys_write32(0, cad_params->reg_base + XSPI_CTRL_WE_SEQ_CFG_0);
	} else {
        LOG_DBG("Initializing XSPI in STIG mode\n");
		/* Switch to DIRECT mode for STIG access */
		(void)xspic_set_ctrl_work_mode(cad_params, DIRECT);
		/* Disable Write-Enable command in direct mode */
		sys_write32(0, cad_params->reg_base + XSPI_CTRL_WE_SEQ_CFG_0);
	}

#if defined(CONFIG_CLOCK_CONTROL)
	if (config->clk_dev) {
		ret = clock_control_on(config->clk_dev, config->clk_id);
		if (ret < 0) {
			LOG_ERR("Failed to enable clock (%d)", ret);
			return ret;
		}

		ret = clock_control_set_rate(
			config->clk_dev, config->clk_id,
			(clock_control_subsys_rate_t)config->assigned_clock_rate);
		if (ret < 0) {
			LOG_ERR("Failed to set clock rate (%d)", ret);
			return ret;
		}
	}
#endif

	ret = xspic_phy_init(dev);
	if (ret < 0) {
		LOG_ERR("Failed to initialize PHY (%d)", ret);
		return ret;
	}

#if defined(CONFIG_RESET)
	if (config->reset.dev != NULL) {
		ret = reset_line_deassert_dt(&config->reset);
		if (ret < 0) {
			LOG_ERR("Failed to de-assert reset");
			return ret;
		}
	}
#endif

	sys_write32(cad_params->xip_base, cad_params->reg_base + XSPI_CTRL_DIRECT_ACCESS_RMP);
	sys_write32(XSPI_CTRL_RMP_ADDR_EN, cad_params->reg_base + XSPI_CTRL_DIRECT_ACCESS_CFG);
	/* Limit program page size to 256 bytes */
	sys_write32(XSPI_CTRL_PAGE_SIZE_256, cad_params->reg_base + XSPI_CTRL_GLOBAL_SEQ_CFG);

	if (config->use_direct_mode) {
		/* Initialize direct mode read/write configurations */
		LOG_DBG("Initializing XSPI in DIRECT mode\n");
        xspic_direct_mode_enable(cad_params);
		/* Switch to DIRECT mode */
		(void)xspic_set_ctrl_work_mode(cad_params, DIRECT);
		/* Disable Write-Enable command in direct mode */
		sys_write32(0, cad_params->reg_base + XSPI_CTRL_WE_SEQ_CFG_0);
	} else {
        LOG_DBG("Initializing XSPI in STIG mode\n");
		/* Switch to DIRECT mode for STIG access */
		(void)xspic_set_ctrl_work_mode(cad_params, DIRECT);
		/* Disable Write-Enable command in direct mode */
		sys_write32(0, cad_params->reg_base + XSPI_CTRL_WE_SEQ_CFG_0);
	}

	return 0;
}

#if defined(CONFIG_CLOCK_CONTROL)
#define CLOCK_XSPI_CONFIG(n)                                                                       \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, clocks),                               \
		   (.clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),              \
		    .clk_id = (clock_control_subsys_t)                             \
			      DT_INST_CLOCKS_CELL(n, clkid),                       \
		    .assigned_clock_rate = DT_INST_PROP(n, assigned_clock_rates),))
#else
#define CLOCK_XSPI_CONFIG(n)
#endif

#if defined(CONFIG_RESET)
#define RESET_XSPI_CONFIG(n)                                                                       \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, resets),      \
		   (.reset = RESET_DT_SPEC_INST_GET(n),))
#else
#define RESET_XSPI_CONFIG(n)
#endif

#define CREATE_FLASH_CADENCE_XSPI_DEVICE(inst)                                                     \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static struct flash_cad_xspi_priv flash_cad_xspi_priv_##inst = {                           \
		.params =                                                                          \
			{                                                                          \
				.xip_size = DT_INST_REG_SIZE_BY_IDX(inst, 1),                      \
			},                                                                         \
	};                                                                                         \
                                                                                                   \
	static struct flash_cad_xspi_config flash_cad_xspi_config_##inst = {                       \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(xspi_reg, DT_DRV_INST(inst)),                   \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(xspi_xip, DT_DRV_INST(inst)),                   \
		.xspi_program = {.addr = (mm_reg_t)DT_REG_ADDR_BY_NAME_OR(DT_DRV_INST(inst),       \
									  xspi_program, 0)},       \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.use_direct_mode = DT_INST_PROP_OR(inst, use_direct_mode, false),                  \
		CLOCK_XSPI_CONFIG(inst) RESET_XSPI_CONFIG(inst)};                                  \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, flash_cad_xspi_init, NULL, &flash_cad_xspi_priv_##inst,        \
			      &flash_cad_xspi_config_##inst, POST_KERNEL,                          \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &flash_cad_xspi_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_FLASH_CADENCE_XSPI_DEVICE)
