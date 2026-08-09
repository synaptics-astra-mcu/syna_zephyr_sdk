/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT cdns_xspi

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/cadence_xspi_soc.h>
#if defined(CONFIG_PINCTRL)
#include <zephyr/drivers/pinctrl.h>
#endif
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
	uint8_t current_io;
	uint32_t read_dummy;
	uint8_t stig_chip_select;
};

struct flash_cad_xspi_priv {
	DEVICE_MMIO_NAMED_RAM(reg_base);
	DEVICE_MMIO_NAMED_RAM(xip_base);
	struct cad_xspi_params params;
	struct k_sem sem;
#if defined(CONFIG_DMA)
	struct k_sem dma_sync;
	uint32_t dma_status;
	const struct device *dma_dev;
	uint8_t dma_channel;
	struct dma_config dma_cfg;
	struct dma_block_config dma_block;
#endif /* CONFIG_DMA */
};

struct flash_cad_xspi_config {
	DEVICE_MMIO_NAMED_ROM(xspi_reg);
	DEVICE_MMIO_NAMED_ROM(xspi_xip);
#if defined(CONFIG_PINCTRL)
	const struct pinctrl_dev_config *pcfg;
#endif
	bool use_direct_mode;
	bool quad_mode;
	bool octa_mode;
	uint8_t stig_chip_select;
	uint8_t quad_read_dummy;
	uint8_t quad_enable_command;
	uint8_t quad_enable_bit;
	uint8_t quad_read_sr1_command;
	uint8_t quad_read_sr2_command;
	uint8_t quad_write_sr_command;
	const struct flash_parameters *parameters;
#if defined(CONFIG_CLOCK_CONTROL)
	const struct device *clk_dev;
	const clock_control_subsys_t clk_id;
	const uint32_t assigned_clock_rate;
#endif /* CONFIG_CLOCK_CONTROL */
#if defined(CONFIG_RESET)
	const struct reset_dt_spec reset;
#endif /* CONFIG_RESET */
#if CONFIG_FLASH_PAGE_LAYOUT
	const struct flash_pages_layout pages_layout;
#endif /* CONFIG_FLASH_PAGE_LAYOUT */
};

#define DEV_DATA(dev) ((struct flash_cad_xspi_priv *)((dev)->data))
#define DEV_CFG(dev)  ((const struct flash_cad_xspi_config *)((dev)->config))

#define DMA_THRESHOLD 16

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

#define XSPI_CTRL_CMD_COMPLETE    BIT(15)
#define XSPI_SDMA_TRD_INFO_DIR    BIT(8)

/* CTRL regs values */
#define XSPI_CTRL_SDMA_ERR_EN     BIT(22)
#define XSPI_CTRL_SDMA_TRIGG_EN   BIT(21)
#define XSPI_CTRL_STIG_DONE_EN    BIT(23)
#define XSPI_CTRL_INTR_EN         BIT(31)
#define XSPI_CTRL_WORK_MODE_SHIFT 5
#define XSPI_CTRL_STATUS_BUSY     BIT(7)
#define XSPI_CTRL_STATUS_GCMD_BUSY BIT(3)
#define XSPI_CTRL_PAGE_SIZE_256   0x8f
#define XSPI_CTRL_INTR_MASK       (XSPI_CTRL_INTR_EN | XSPI_CTRL_STIG_DONE_EN | \
				   XSPI_CTRL_SDMA_ERR_EN | XSPI_CTRL_SDMA_TRIGG_EN)

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

/* Default PHY init values. SoCs may override through cadence_xspi_soc_phy_config(). */
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
#define XSPI_DIRECT_ACCESS_CS1       BIT(0)
#define XSPI_WAIT_LIMIT              (K_MSEC(1000))
#define XSPI_POLL_TIMEOUT_COUNT      100000U
#define XSPI_POLL_DELAY_NOPS         10U

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

/* WE_SEQ_CFG_0 bit field positions */
#define XSPI_CTRL_WE_SEQ_CFG_0_CMD_VAL_POS     0
#define XSPI_CTRL_WE_SEQ_CFG_0_CMD_IOS_POS     8
#define XSPI_CTRL_WE_SEQ_CFG_0_CMD_EXT_EN_POS  15
#define XSPI_CTRL_WE_SEQ_CFG_0_CMD_EXT_VAL_POS 16
#define XSPI_CTRL_WE_SEQ_CFG_0_EN_POS          24

/* STAT_SEQ_CFG_0 bit field positions */
#define XSPI_CTRL_STAT_SEQ_CFG_0_ADDR_CNT_POS    8
#define XSPI_CTRL_STAT_SEQ_CFG_0_ADDR_IOS_POS    10
#define XSPI_CTRL_STAT_SEQ_CFG_0_DATA_IOS_POS    20
#define XSPI_CTRL_STAT_SEQ_CFG_0_CMD_IOS_POS     0
#define XSPI_CTRL_STAT_SEQ_CFG_0_CMD_EXT_EN_POS  5

/* STAT_SEQ_CFG_1 bit field positions */
#define XSPI_CTRL_STAT_SEQ_CFG_1_PROG_FAIL_ADDR_EN_POS    22
#define XSPI_CTRL_STAT_SEQ_CFG_1_PROG_FAIL_DUMMY_CNT_POS  16

/* STAT_SEQ_CFG_3 bit field positions */
#define XSPI_CTRL_STAT_SEQ_CFG_3_PROG_FAIL_CMD_EXT_VAL_POS 24

/* Read sequence operation codes */
#define XSPI_OP_RD            0x03
#define XSPI_OP_RD_1_2_2      0xBB
#define XSPI_OP_RD_1_4_4      0xEB
#define XSPI_OP_RD_8_8_8_4B   0xEC

/* Program and status operation codes */
#define XSPI_OP_PP            0x02
#define XSPI_OP_PP_8_8_8_4B   0x12
#define XSPI_OP_ERASE_4K_4B   0x21
#define XSPI_OP_RDSR          0x05
#define XSPI_OP_WREN          0x06
#define XSPI_OP_WRCR2         0x72

#define XSPI_OPI_RDID_EXT     0x60
#define XSPI_OPI_READ_EXT     0x13
#define XSPI_OPI_PP_EXT       0xED
#define XSPI_OPI_SE_EXT       0xDE
#define XSPI_OPI_WREN_EXT     0xF9
#define XSPI_OPI_RDSR_EXT     0xFA
#define XSPI_OPI_WRCR2_EXT    0x8D

#define XSPI_OCTA_READ_DUMMY       6
#define XSPI_OCTA_STIG_READ_DUMMY  4
#define XSPI_OCTA_DUMMY_CFG_ADDR   0x300
#define XSPI_OCTA_DUMMY_CFG_VALUE  0x7
#define XSPI_OCTA_ENABLE_ADDR      0x0
#define XSPI_OCTA_ENABLE_VALUE     0x1

#ifndef SPI_NOR_CMD_ULBPR
#define SPI_NOR_CMD_ULBPR     0x98
#endif

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

struct xspic_cmd_cfg {
	uint8_t stig_cmd;
	uint8_t ext_cmd;
	uint8_t ext_cmd_en;
	uint8_t cmd_mode;
	uint8_t addr_bytes;
	uint8_t addr_mode;
	uint8_t data_mode;
	uint8_t dummy_count;
	uint8_t swap;
	uint8_t mode_bytes;
	uint8_t mode[2];
	uint8_t cs;
};


static void xspic_encode_stig_write(const struct xspic_cmd_cfg *cmd,
				   uint32_t *cmd_reg, uint32_t *glue_reg,
				   uint32_t addr, uint32_t data_b,
				   uint8_t data0, uint8_t data1);
static void xspic_trigger_command(struct cad_xspi_params *cad_params, uint32_t *cmd);
static int xspic_stig_wait_for_instr_end(struct cad_xspi_params *cad_params);
static int xspic_wait_for_idle(struct cad_xspi_params *cad_params);
static uint32_t xspic_set_ctrl_work_mode(struct cad_xspi_params *cad_params,
					e_xspic_work_mode new_mode);
static int xspic_stig_cmd(struct cad_xspi_params *cad_params, uint32_t opcode);
static int xspic_stig_cmd_write_status(struct cad_xspi_params *cad_params, uint32_t opcode,
					      uint32_t status_reg_length, uint8_t data0,
					      uint8_t data1);

static void xspic_cmd_cfg_default(struct cad_xspi_params *cad_params, struct xspic_cmd_cfg *cmd,
				       uint32_t opcode)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->stig_cmd = opcode & 0xffU;
	cmd->cmd_mode = IO1;
	cmd->addr_mode = IO1;
	cmd->data_mode = IO1;
	cmd->cs = cad_params->stig_chip_select;
}

static bool xspic_is_octa_mode(const struct cad_xspi_params *cad_params)
{
	return cad_params->current_io == IO8;
}

static void xspic_cmd_cfg_octa(struct cad_xspi_params *cad_params, struct xspic_cmd_cfg *cmd,
				      uint32_t opcode, uint32_t ext_opcode)
{
	xspic_cmd_cfg_default(cad_params, cmd, opcode);
	cmd->ext_cmd = ext_opcode & 0xffU;
	cmd->ext_cmd_en = 1;
	cmd->cmd_mode = IO8;
	cmd->addr_mode = IO8;
	cmd->data_mode = IO8;
}

static void xspic_cmd_cfg_stig(struct cad_xspi_params *cad_params, struct xspic_cmd_cfg *cmd,
			       uint32_t opcode, uint32_t ext_opcode)
{
	if (xspic_is_octa_mode(cad_params)) {
		xspic_cmd_cfg_octa(cad_params, cmd, opcode, ext_opcode);
	} else {
		xspic_cmd_cfg_default(cad_params, cmd, opcode);
	}
}

static uint32_t xspic_octa_ext_for_opcode(uint32_t opcode)
{
	switch (opcode) {
	case SPI_NOR_CMD_WREN:
		return XSPI_OPI_WREN_EXT;
	case SPI_NOR_CMD_RDSR:
		return XSPI_OPI_RDSR_EXT;
	case SPI_NOR_CMD_PP:
		return XSPI_OPI_PP_EXT;
	case SPI_NOR_CMD_SE:
		return XSPI_OPI_SE_EXT;
	case JESD216_CMD_READ_ID:
		return XSPI_OPI_RDID_EXT;
	default:
		return 0;
	}
}

static void xspic_enable_direct_wel(struct cad_xspi_params *cad_params)
{
	uint32_t reg_val;

	if (!xspic_is_octa_mode(cad_params)) {
		return;
	}

	reg_val = (XSPI_OP_WREN << XSPI_CTRL_WE_SEQ_CFG_0_CMD_VAL_POS) |
		  (IO8 << XSPI_CTRL_WE_SEQ_CFG_0_CMD_IOS_POS) |
		  (1U << XSPI_CTRL_WE_SEQ_CFG_0_CMD_EXT_EN_POS) |
		  (XSPI_OPI_WREN_EXT << XSPI_CTRL_WE_SEQ_CFG_0_CMD_EXT_VAL_POS) |
		  (1U << XSPI_CTRL_WE_SEQ_CFG_0_EN_POS);
	sys_write32(reg_val, cad_params->reg_base + XSPI_CTRL_WE_SEQ_CFG_0);
}

static int xspic_stig_write_cr2_1byte(struct cad_xspi_params *cad_params, uint32_t addr,
				      uint8_t value)
{
	uint32_t cmd_reg[5];
	uint32_t glue_reg[5] = {0};
	struct xspic_cmd_cfg wr_cmd;

	xspic_cmd_cfg_default(cad_params, &wr_cmd, XSPI_OP_WRCR2);
	wr_cmd.addr_bytes = 4;
	xspic_encode_stig_write(&wr_cmd, cmd_reg, glue_reg, addr, 1, value, 0);
	xspic_trigger_command(cad_params, cmd_reg);

	return xspic_stig_wait_for_instr_end(cad_params);
}

static int xspic_enable_octa_mode(struct cad_xspi_params *cad_params)
{
	uint32_t mode;
	int ret;

	ret = xspic_wait_for_idle(cad_params);
	if (ret < 0) {
		return ret;
	}

	mode = xspic_set_ctrl_work_mode(cad_params, STIG);

	ret = xspic_stig_cmd(cad_params, SPI_NOR_CMD_WREN);
	if (ret < 0) {
		goto out;
	}

	ret = xspic_stig_write_cr2_1byte(cad_params, XSPI_OCTA_DUMMY_CFG_ADDR,
					   XSPI_OCTA_DUMMY_CFG_VALUE);
	if (ret < 0) {
		goto out;
	}

	ret = xspic_stig_cmd(cad_params, SPI_NOR_CMD_WREN);
	if (ret < 0) {
		goto out;
	}

	ret = xspic_stig_write_cr2_1byte(cad_params, XSPI_OCTA_ENABLE_ADDR,
					   XSPI_OCTA_ENABLE_VALUE);
	if (ret == 0) {
		cad_params->current_io = IO8;
		cad_params->read_dummy = XSPI_OCTA_READ_DUMMY;
	}

out:
	xspic_set_ctrl_work_mode(cad_params, mode);
	return ret;
}

static void xspic_encode_stig_read(const struct xspic_cmd_cfg *cmd,
				  uint32_t *cmd_reg, uint32_t *glue_reg,
				  uint32_t addr, uint32_t data_b)
{
	cmd_reg[0] = 0;
	cmd_reg[1] = 0x1U |
		     ((addr & 0xffU) << 24) |
		     ((uint32_t)cmd->mode[0] << 8) |
		     ((uint32_t)cmd->mode[1] << 16);
	cmd_reg[2] = addr >> 8;
	cmd_reg[3] = ((uint32_t)cmd->stig_cmd << 16) |
		     ((uint32_t)cmd->ext_cmd << 8) |
		     ((uint32_t)cmd->mode_bytes << 24) |
		     (((uint32_t)cmd->addr_bytes & 0x7U) << 28);
	cmd_reg[4] = (((uint32_t)cmd->cs & 0x7U) << 12) |
		     ((uint32_t)cmd->addr_mode & 0xfU) |
		     ((uint32_t)cmd->ext_cmd_en << 4) |
		     (((uint32_t)cmd->cmd_mode & 0xfU) << 8);

	glue_reg[0] = 0;
	glue_reg[1] = 0x7fU | (((data_b > 2U) ? 0U : 1U) << 24);
	glue_reg[2] = (data_b & 0xffffU) << 16;
	glue_reg[3] = (data_b >> 16) | ((uint32_t)cmd->dummy_count << 20);
	glue_reg[4] = (((uint32_t)cmd->cs & 0x7U) << 12) |
		      ((uint32_t)cmd->data_mode << 8) |
		      ((uint32_t)cmd->swap << 16);
}

static void xspic_encode_stig_write(const struct xspic_cmd_cfg *cmd,
				   uint32_t *cmd_reg, uint32_t *glue_reg,
				   uint32_t addr, uint32_t data_b,
				   uint8_t data0, uint8_t data1)
{
	cmd_reg[0] = 0;
	cmd_reg[1] = ((addr & 0xffU) << 24) |
		     ((uint32_t)data0 << 8) |
		     ((uint32_t)data1 << 16);
	cmd_reg[2] = addr >> 8;
	cmd_reg[3] = ((uint32_t)cmd->stig_cmd << 16) |
		     ((uint32_t)cmd->ext_cmd << 8) |
		     (((data_b > 2U) ? 0U : (data_b & 0x3U)) << 24) |
		     (((uint32_t)cmd->addr_bytes & 0x7U) << 28);
	cmd_reg[4] = (((uint32_t)cmd->cs & 0x7U) << 12) |
		     ((uint32_t)cmd->addr_mode & 0xfU) |
		     ((uint32_t)cmd->ext_cmd_en << 4) |
		     (((uint32_t)cmd->cmd_mode & 0xfU) << 8) |
		     (((data_b > 2U) ? 1U : 0U) << 28);

	if ((glue_reg != NULL) && (data_b > 2U)) {
		glue_reg[0] = 0;
		glue_reg[1] = 0x7fU;
		glue_reg[2] = (data_b & 0xffffU) << 16;
		glue_reg[3] = (data_b >> 16) | ((uint32_t)cmd->dummy_count << 20);
		glue_reg[4] = (((uint32_t)cmd->cs & 0x7U) << 12) |
			      (1U << 4) |
			      ((uint32_t)cmd->data_mode << 8) |
			      ((uint32_t)cmd->swap << 16);
	}
}

static void acquire_device(const struct device *dev)
{
	if (IS_ENABLED(CONFIG_MULTITHREADING)) {
		struct flash_cad_xspi_priv *priv = dev->data;

		k_sem_take(&priv->sem, K_FOREVER);
	}
}

static int xspi_set_write_enable(struct cad_xspi_params *cad_params);
static int xspic_poll_for_wip_bit(struct cad_xspi_params *cad_params);
static void xspic_set_interrupts(struct cad_xspi_params *cad_params, bool enable);

static void release_device(const struct device *dev)
{
	if (IS_ENABLED(CONFIG_MULTITHREADING)) {
		struct flash_cad_xspi_priv *priv = dev->data;

		k_sem_give(&priv->sem);
	}
}

static void xspic_enter_reset_dll(uintptr_t base)
{
	uint32_t regval;

	regval = sys_read32(base + XSPI_PHY_DLL_CTRL_REG);
	regval &= ~XSPI_PHY_DLL_RST_N;
	sys_write32(regval, base + XSPI_PHY_DLL_CTRL_REG);
}

static inline void xspic_nop_delay(uint32_t n)
{
	while (n-- > 0U) {
		__asm volatile ("nop");
	}
}

static int xspic_exit_reset_dll(uintptr_t base)
{
	uint32_t regval;

	regval = sys_read32(base + XSPI_PHY_DLL_CTRL_REG);
	regval |= XSPI_PHY_DLL_RST_N;
	sys_write32(regval, base + XSPI_PHY_DLL_CTRL_REG);

	uint32_t timeout_nop = XSPI_POLL_TIMEOUT_COUNT;

	regval = sys_read32(base + XSPI_PHY_DLL_OBS_REG_0);
	while (!(regval & XSPI_PHY_DLL_OBS_LOCKED) && timeout_nop > 0U) {
		xspic_nop_delay(1);
		regval = sys_read32(base + XSPI_PHY_DLL_OBS_REG_0);
		timeout_nop--;
	}

	if (!(regval & XSPI_PHY_DLL_OBS_LOCKED)) {
		LOG_ERR("Timeout while waiting for XSPI PHY DLL to lock");
		return -ETIMEDOUT;
	}

	return 0;
}

static const struct cadence_xspi_phy_config default_phy_config = {
	.dll_ctrl = XSPI_PHY_DLL_CTRL_INIT,
	.dq_timing = XSPI_PHY_DQ_TIMING_INIT,
	.dqs_timing = XSPI_PHY_DQS_TIMING_INIT,
	.gate_lpbk_ctrl = XSPI_PHY_GATE_LPBK_CTRL_INIT,
	.dll_master_ctrl = XSPI_PHY_DLL_MASTER_CTRL_INIT,
	.dll_slave_ctrl = XSPI_PHY_DLL_SLAVE_CTRL_INIT,
};

__weak const struct cadence_xspi_phy_config *cadence_xspi_soc_phy_config(void)
{
	return &default_phy_config;
}

__weak int cadence_xspi_soc_pre_init(uintptr_t reg_base)
{
	ARG_UNUSED(reg_base);

	return 0;
}

static int xspic_phy_init(uintptr_t base)
{
	const struct cadence_xspi_phy_config *phy_config = cadence_xspi_soc_phy_config();
	int ret = 0;
	uint32_t val = 0;

	xspic_enter_reset_dll(base);

	sys_write32(phy_config->dq_timing, base + XSPI_PHY_DQ_TIMING_REG);
	sys_write32(phy_config->dqs_timing, base + XSPI_PHY_DQS_TIMING_REG);
	sys_write32(phy_config->gate_lpbk_ctrl, base + XSPI_PHY_GATE_LPBK_CTRL_REG);
	sys_write32(phy_config->dll_master_ctrl, base + XSPI_PHY_DLL_MASTER_CTRL_REG);
	sys_write32(phy_config->dll_slave_ctrl, base + XSPI_PHY_DLL_SLAVE_CTRL_REG);
	sys_write32(XSPI_PHY_CTRL_INIT, base + XSPI_PHY_CTRL_REG);
	val = sys_read32(base + XSPI_PHY_DLL_CTRL_REG);
	sys_write32(val | phy_config->dll_ctrl, base + XSPI_PHY_DLL_CTRL_REG);

	ret = xspic_exit_reset_dll(base);
	return ret;
}

static void xspic_direct_read_cfg(struct cad_xspi_params *cad_params, e_xspic_io_lines bits,
				  uint32_t dummy_cnt)
{
	uint32_t reg_val;

	switch (bits) {
	case IO8:
		reg_val = (XSPI_OP_RD_8_8_8_4B << XSPI_CTRL_READ_SEQ_CFG_0_CMD_VAL_POS) |
			  (IO8 << XSPI_CTRL_READ_SEQ_CFG_0_CMD_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_CMD_EDGE_POS) |
			  (4 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_CNT_POS) |
			  (IO8 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_ADDR_EDGE_POS) |
			  (IO8 << XSPI_CTRL_READ_SEQ_CFG_0_DATA_IOS_POS) |
			  (0 << XSPI_CTRL_READ_SEQ_CFG_0_DATA_EDGE_POS) |
			  (dummy_cnt << XSPI_CTRL_READ_SEQ_CFG_0_DUMMY_CNT_POS);
		break;

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
	if (bits == IO8) {
		sys_write32((1U << XSPI_CTRL_READ_SEQ_CFG_1_CMD_EXT_EN_POS) |
			    (XSPI_OPI_READ_EXT << XSPI_CTRL_READ_SEQ_CFG_1_CMD_EXT_VAL_POS),
			    cad_params->reg_base + XSPI_CTRL_READ_SEQ_CFG_1);
	} else {
		sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_READ_SEQ_CFG_1);
	}
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_READ_SEQ_CFG_2);
}

static void xspic_direct_stat_cfg(struct cad_xspi_params *cad_params, e_xspic_io_lines bits)
{
	if (bits == IO8) {
		sys_write32((IO8 << XSPI_CTRL_STAT_SEQ_CFG_0_DATA_IOS_POS) |
			    (IO8 << XSPI_CTRL_STAT_SEQ_CFG_0_ADDR_IOS_POS) |
			    (3 << XSPI_CTRL_STAT_SEQ_CFG_0_ADDR_CNT_POS) |
			    (IO8 << XSPI_CTRL_STAT_SEQ_CFG_0_CMD_IOS_POS) |
			    (1U << XSPI_CTRL_STAT_SEQ_CFG_0_CMD_EXT_EN_POS),
			    cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_0);
		sys_write32((4 << XSPI_CTRL_STAT_SEQ_CFG_1_PROG_FAIL_DUMMY_CNT_POS) |
			    (1U << XSPI_CTRL_STAT_SEQ_CFG_1_PROG_FAIL_ADDR_EN_POS),
			    cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_1);
		sys_write32((XSPI_OP_RDSR << XSPI_CTRL_STAT_SEQ_CFG_2_PROG_FAIL_CMD_VAL_POS) |
			    XSPI_OP_RDSR, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_2);
		sys_write32(XSPI_OPI_RDSR_EXT << XSPI_CTRL_STAT_SEQ_CFG_3_PROG_FAIL_CMD_EXT_VAL_POS,
			    cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_3);
	} else {
		sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_0);
		sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_1);
		sys_write32((XSPI_OP_RDSR << XSPI_CTRL_STAT_SEQ_CFG_2_PROG_FAIL_CMD_VAL_POS) |
			    XSPI_OP_RDSR, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_2);
		sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_3);
	}
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_4);
	sys_write32(XSPI_CTRL_STAT_SEQ_CFG_5_DEV_RDY_EN_MASK << 6, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_5);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_7);
	sys_write32(0x0, cad_params->reg_base + XSPI_CTRL_STAT_SEQ_CFG_8);
}

static void xspic_direct_prog_pp_cfg(struct cad_xspi_params *cad_params)
{
	uint32_t reg_val;

	if (cad_params->current_io == IO8) {
		reg_val = (XSPI_OP_PP_8_8_8_4B << XSPI_CTRL_PROG_SEQ_CFG_0_CMD_VAL_POS) |
			  (IO8 << XSPI_CTRL_PROG_SEQ_CFG_0_CMD_IOS_POS) |
			  (0 << XSPI_CTRL_PROG_SEQ_CFG_0_CMD_EDGE_POS) |
			  (4 << XSPI_CTRL_PROG_SEQ_CFG_0_ADDR_CNT_POS) |
			  (IO8 << XSPI_CTRL_PROG_SEQ_CFG_0_ADDR_IOS_POS) |
			  (0 << XSPI_CTRL_PROG_SEQ_CFG_0_ADDR_EDGE_POS) |
			  (IO8 << XSPI_CTRL_PROG_SEQ_CFG_0_DATA_IOS_POS) |
			  (0 << XSPI_CTRL_PROG_SEQ_CFG_0_DATA_EDGE_POS) |
			  (0 << XSPI_CTRL_PROG_SEQ_CFG_0_DUMMY_CNT_POS);
		sys_write32(reg_val, cad_params->reg_base + XSPI_CTRL_PROG_SEQ_CFG_0);
		sys_write32((1U << XSPI_CTRL_READ_SEQ_CFG_1_CMD_EXT_EN_POS) |
			    (XSPI_OPI_PP_EXT << XSPI_CTRL_READ_SEQ_CFG_1_CMD_EXT_VAL_POS),
			    cad_params->reg_base + XSPI_CTRL_PROG_SEQ_CFG_1);
	} else {
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

static void xspic_set_interrupts(struct cad_xspi_params *cad_params, bool enable)
{
	uint32_t intr;
	uint32_t status;

	status = sys_read32(cad_params->reg_base + XSPI_CTRL_INTR_STATUS);
	if (status != 0U) {
		sys_write32(status, cad_params->reg_base + XSPI_CTRL_INTR_STATUS);
	}

	intr = sys_read32(cad_params->reg_base + XSPI_CTRL_INTR_ENABLE);
	if (enable) {
		intr |= XSPI_CTRL_INTR_MASK;
	} else {
		intr &= ~XSPI_CTRL_INTR_MASK;
	}

	sys_write32(intr, cad_params->reg_base + XSPI_CTRL_INTR_ENABLE);
}

static int xspic_stig_wait_for_instr_end(struct cad_xspi_params *cad_params)
{
	uint32_t timeout = XSPI_POLL_TIMEOUT_COUNT;

	while (timeout-- > 0U) {
		if ((sys_read32(cad_params->reg_base + XSPI_CTRL_CMD_STATUS) &
		    XSPI_CTRL_CMD_COMPLETE) != 0U) {
			return 0;
		}
		xspic_nop_delay(XSPI_POLL_DELAY_NOPS);
	}

	return -ETIMEDOUT;
}

static int xspic_stig_cmd(struct cad_xspi_params *cad_params, uint32_t opcode)
{
	uint32_t cmd_reg[5];
	struct xspic_cmd_cfg cmd;

	xspic_cmd_cfg_stig(cad_params, &cmd, opcode, xspic_octa_ext_for_opcode(opcode));
	xspic_encode_stig_write(&cmd, cmd_reg, NULL, 0, 0, 0, 0);
	xspic_trigger_command(cad_params, cmd_reg);

	return xspic_stig_wait_for_instr_end(cad_params);
}

static void xspic_trigger_command(struct cad_xspi_params *cad_params, uint32_t *cmd)
{
	sys_write32(cmd[4], cad_params->reg_base + XSPI_CTRL_CMD_REG4);
	sys_write32(cmd[3], cad_params->reg_base + XSPI_CTRL_CMD_REG3);
	sys_write32(cmd[2], cad_params->reg_base + XSPI_CTRL_CMD_REG2);
	sys_write32(cmd[1], cad_params->reg_base + XSPI_CTRL_CMD_REG1);
	barrier_dsync_fence_full();
	sys_write32(cmd[0], cad_params->reg_base + XSPI_CTRL_CMD_REG0);
}

static int xspic_wait_for_idle(struct cad_xspi_params *cad_params)
{
	uint32_t timeout = XSPI_POLL_TIMEOUT_COUNT;

	while (timeout-- > 0U) {
		if ((sys_read32(cad_params->reg_base + XSPI_CTRL_CTRL_STATUS) &
		     XSPI_CTRL_STATUS_BUSY) == 0U) {
			return 0;
		}
		xspic_nop_delay(XSPI_POLL_DELAY_NOPS);
	}

	return -ETIMEDOUT;
}

static int xspic_wait_for_engine_idle(struct cad_xspi_params *cad_params)
{
	uint32_t status;
	k_timepoint_t end = sys_timepoint_calc(XSPI_WAIT_LIMIT);

	do {
		status = sys_read32(cad_params->reg_base + XSPI_CTRL_CTRL_STATUS);
		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}
	} while (status & XSPI_CTRL_STATUS_GCMD_BUSY);

	return 0;
}

static uint32_t xspic_set_ctrl_work_mode(struct cad_xspi_params *cad_params,
					 e_xspic_work_mode new_mode)
{
	uint32_t mode;

	mode = (sys_read32(cad_params->reg_base + XSPI_CTRL_CTRL_CONFIG) >>
		XSPI_CTRL_WORK_MODE_SHIFT) & 0x1;
	if (mode != (uint32_t)new_mode) {
		sys_write32((uint32_t)new_mode << XSPI_CTRL_WORK_MODE_SHIFT,
			    cad_params->reg_base + XSPI_CTRL_CTRL_CONFIG);
	}

	return mode;
}

static uint32_t xspic_direct_mode_enable(struct cad_xspi_params *cad_params)
{
	uint32_t mode;

	xspic_direct_read_cfg(cad_params, cad_params->current_io, cad_params->read_dummy);
	xspic_direct_prog_pp_cfg(cad_params);
	xspic_enable_direct_wel(cad_params);
	xspic_direct_stat_cfg(cad_params, cad_params->current_io);
	mode = xspic_set_ctrl_work_mode(cad_params, DIRECT);

	return mode;
}

static int xspic_wait_for_data(struct cad_xspi_params *cad_params)
{
	uint32_t status_val = 0;

	uint32_t timeout = XSPI_POLL_TIMEOUT_COUNT;

	while (timeout-- > 0U) {
		status_val = xspic_check_interrupts(cad_params, XSPI_CTRL_SDMA_ERR_EN |
								XSPI_CTRL_SDMA_TRIGG_EN);
		if (status_val & XSPI_CTRL_SDMA_ERR_EN) {
			return -EIO;
		}
		if (status_val & XSPI_CTRL_SDMA_TRIGG_EN) {
			return 0;
		}
		xspic_nop_delay(XSPI_POLL_DELAY_NOPS);
	}

	return -ETIMEDOUT;
}

static void xspic_copy(void *dest, const void *src, size_t len)
{
	uint8_t *b_dest = (uint8_t *)dest;
	const uint8_t *b_src = (const uint8_t *)src;
	uint64_t *l_dest;
	const uint64_t *l_src;
	uintptr_t src_align = (uintptr_t)src & 7U;
	uintptr_t dest_align = (uintptr_t)dest & 7U;

	/*
	 * The 64-bit fast path is only safe when source and destination have the
	 * same alignment offset. Otherwise one side can become aligned while the
	 * other remains unaligned, which faults on Cortex-M55.
	 *
	 * For XSPI/XIP-backed regions, falling back to libc memcpy() here can also
	 * introduce controller/MMIO-unfriendly access patterns. Use an explicit
	 * byte-copy fallback instead.
	 */
	if (src_align != dest_align) {
		while (len-- > 0U) {
			*b_dest++ = *b_src++;
		}
		return;
	}

	while (((uintptr_t)b_src & 7U) && (len > 0U)) {
		*b_dest++ = *b_src++;
		len--;
	}

	l_dest = (uint64_t *)b_dest;
	l_src = (const uint64_t *)b_src;
	while (len >= 8) {
		*l_dest++ = *l_src++;
		len -= 8;
	}

	b_dest = (uint8_t *)l_dest;
	b_src = (const uint8_t *)l_src;
	while (len-- > 0) {
		*b_dest++ = *b_src++;
	}
}

static void xspic_mmio_write(uintptr_t dest, const uint8_t *src, size_t len)
{
	size_t pos = 0U;

	/*
	 * Push data into the XSPI staging window with explicit 32-bit stores for
	 * the bulk path. The SR100 aperture behaves more like a controller RAM/FIFO
	 * than normal system SRAM, so avoiding libc memcpy() access patterns here
	 * gives us tighter control over store width and ordering.
	 */
	while ((len - pos) >= sizeof(uint32_t)) {
		uint32_t word;

		memcpy(&word, src + pos, sizeof(word));
		sys_write32(word, dest + pos);
		pos += sizeof(uint32_t);
	}

	while (pos < len) {
		sys_write8(src[pos], dest + pos);
		pos++;
	}
}

#if defined(CONFIG_FLASH_JESD216_API)
static int xspic_stig_read_data(struct cad_xspi_params *cad_params, uint32_t cmd, off_t offset,
				uint8_t *buf, size_t len, uint32_t addr_cnt, int io4,
				uint32_t dummy)
{
	uint32_t cmd_reg[5];
	uint32_t glue_reg[5] = { 0 };
	uint32_t mode;
	uint32_t sdma_size;
	uint32_t sdma_trd_info;
	int ret;

	ret = xspic_wait_for_idle(cad_params);
	if (ret < 0) {
		return ret;
	}

	/* Switch to STIG mode */
	mode = xspic_set_ctrl_work_mode(cad_params, STIG);

	struct xspic_cmd_cfg rd_cmd;

	if (xspic_is_octa_mode(cad_params) && cmd == JESD216_CMD_READ_ID) {
		xspic_cmd_cfg_octa(cad_params, &rd_cmd, cmd, XSPI_OPI_RDID_EXT);
		rd_cmd.addr_bytes = 4;
		rd_cmd.dummy_count = XSPI_OCTA_STIG_READ_DUMMY;
	} else {
		xspic_cmd_cfg_default(cad_params, &rd_cmd, cmd);
		rd_cmd.addr_bytes = 3;
		rd_cmd.addr_mode = io4 ? IO4 : IO1;
		rd_cmd.data_mode = io4 ? IO4 : IO1;
		rd_cmd.dummy_count = dummy;
	}
	xspic_encode_stig_read(&rd_cmd, cmd_reg, glue_reg, (uint32_t)offset, (uint32_t)len);
	xspic_set_interrupts(cad_params, true);
	xspic_trigger_command(cad_params, cmd_reg);
	xspic_trigger_command(cad_params, glue_reg);

	ret = xspic_wait_for_data(cad_params);
	if (ret < 0) {
		goto out;
	}

	sdma_size = sys_read32(cad_params->reg_base + XSPI_CTRL_SDMA_SIZE);
	sdma_trd_info = sys_read32(cad_params->reg_base + XSPI_CTRL_SDMA_TRD_INFO);
	if (sdma_trd_info & XSPI_SDMA_TRD_INFO_DIR) { /* expect read */
		ret = -EIO;
		goto out;
	}

	xspic_copy(buf, (void *)cad_params->xip_base, sdma_size);
	barrier_dsync_fence_full();

	ret = xspic_stig_wait_for_instr_end(cad_params);

out:
	xspic_set_interrupts(cad_params, false);
	/* Switch back to original mode */
	xspic_set_ctrl_work_mode(cad_params, mode);

	return ret;
}
#endif /* CONFIG_FLASH_JESD216_API */

static int xspic_stig_cmd_read_status(struct cad_xspi_params *cad_params,
				      uint32_t status_reg_length, uint32_t status_cmd,
				      uint32_t *status_reg_value)
{
	uint32_t rdata;
	uint32_t cmd_reg[5];
	uint32_t glue_reg[5] = { 0 };
	int ret;

	struct xspic_cmd_cfg rd_cmd;

	xspic_cmd_cfg_stig(cad_params, &rd_cmd, status_cmd, xspic_octa_ext_for_opcode(status_cmd));
	if (xspic_is_octa_mode(cad_params)) {
		rd_cmd.addr_bytes = 4;
		rd_cmd.dummy_count = XSPI_OCTA_STIG_READ_DUMMY;
	} else {
		rd_cmd.addr_bytes = 0;
		rd_cmd.dummy_count = 0;
	}
	xspic_encode_stig_read(&rd_cmd, cmd_reg, glue_reg, 0, status_reg_length);

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

static int xspic_try_global_unlock(struct cad_xspi_params *cad_params)
{
	uint32_t mode;
	int ret;

	ret = xspic_wait_for_idle(cad_params);
	if (ret < 0) {
		return ret;
	}

	mode = xspic_set_ctrl_work_mode(cad_params, STIG);

	ret = xspi_set_write_enable(cad_params);
	if (ret < 0) {
		goto out;
	}

	ret = xspic_stig_cmd(cad_params, SPI_NOR_CMD_ULBPR);
	if (ret < 0) {
		goto out;
	}

	ret = xspic_poll_for_wip_bit(cad_params);

out:
	xspic_set_ctrl_work_mode(cad_params, mode);

	return ret;
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

static int xspic_stig_cmd_write_status(struct cad_xspi_params *cad_params, uint32_t opcode,
					      uint32_t status_reg_length, uint8_t data0,
					      uint8_t data1)
{
	uint32_t cmd_reg[5];
	uint32_t glue_reg[5] = {0};
	struct xspic_cmd_cfg wr_cmd;

	xspic_cmd_cfg_default(cad_params, &wr_cmd, opcode);
	xspic_encode_stig_write(&wr_cmd, cmd_reg, glue_reg, 0, status_reg_length, data0, data1);
	xspic_trigger_command(cad_params, cmd_reg);

	return xspic_stig_wait_for_instr_end(cad_params);
}

static int xspic_enable_quad_mode(struct cad_xspi_params *cad_params,
				  const struct flash_cad_xspi_config *config)
{
	uint32_t mode;
	uint32_t sr1_value = 0;
	uint32_t sr2_value = 0;
	uint32_t status_reg_length = 0;
	int ret;

	ret = xspic_wait_for_idle(cad_params);
	if (ret < 0) {
		return ret;
	}

	mode = xspic_set_ctrl_work_mode(cad_params, STIG);

	if (config->quad_enable_command != 0U) {
		ret = xspic_stig_cmd(cad_params, config->quad_enable_command);
		if (ret == 0) {
			cad_params->current_io = IO4;
			cad_params->read_dummy = config->quad_read_dummy;
		}
		goto out;
	}

	if (config->quad_enable_bit > 15U) {
		ret = -EINVAL;
		goto out;
	}

	if (config->quad_write_sr_command == 0U) {
		ret = -EINVAL;
		goto out;
	}

	if (config->quad_read_sr1_command != 0U) {
		ret = xspic_stig_cmd_read_status(cad_params, 1,
						 config->quad_read_sr1_command,
						 &sr1_value);
		if (ret < 0) {
			goto out;
		}
		status_reg_length = 1;
	}

	if (config->quad_read_sr2_command != 0U) {
		ret = xspic_stig_cmd_read_status(cad_params, 1,
						 config->quad_read_sr2_command,
						 &sr2_value);
		if (ret < 0) {
			goto out;
		}
		status_reg_length = 2;
	}

	if (status_reg_length == 0U) {
		ret = -EINVAL;
		goto out;
	}

	if (config->quad_enable_bit <= 7U) {
		sr1_value |= BIT(config->quad_enable_bit);
	} else {
		if (status_reg_length < 2U) {
			ret = -EINVAL;
			goto out;
		}

		sr2_value |= BIT(config->quad_enable_bit - 8U);
	}

	ret = xspi_set_write_enable(cad_params);
	if (ret < 0) {
		goto out;
	}

	if (status_reg_length == 2U) {
		ret = xspic_stig_cmd_write_status(cad_params,
						  config->quad_write_sr_command,
						  status_reg_length,
						  sr2_value,
						  sr1_value);
	} else {
		ret = xspic_stig_cmd_write_status(cad_params,
						  config->quad_write_sr_command,
						  status_reg_length,
						  sr1_value,
						  0);
	}

	if (ret < 0) {
		goto out;
	}

	ret = xspic_poll_for_wip_bit(cad_params);
	if (ret == 0) {
		cad_params->current_io = IO4;
		cad_params->read_dummy = config->quad_read_dummy;
	}

out:
	xspic_set_ctrl_work_mode(cad_params, mode);
	return ret;
}

static int xspic_stig_cmd_write_data(struct cad_xspi_params *cad_params, uint32_t xfer_addr,
				     uint8_t *src, uint32_t size)
{
	uint32_t cmd_reg[5];
	uint32_t glue_reg[5];
	uint32_t mode = 0;
	uint32_t sdma_size;
	uint32_t sdma_trd_info;
	int ret;
	unsigned int key;

	if (size > SPI_NOR_PAGE_SIZE) {
		return -EINVAL;
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

	struct xspic_cmd_cfg wr_cmd;

	if (xspic_is_octa_mode(cad_params)) {
		xspic_cmd_cfg_octa(cad_params, &wr_cmd, XSPI_OP_PP_8_8_8_4B, XSPI_OPI_PP_EXT);
		wr_cmd.addr_bytes = 4;
	} else {
		xspic_cmd_cfg_default(cad_params, &wr_cmd, SPI_NOR_CMD_PP);
		wr_cmd.addr_bytes = 3;
	}
	xspic_encode_stig_write(&wr_cmd, cmd_reg, glue_reg, xfer_addr, size, 0, 0);
	xspic_set_interrupts(cad_params, true);
	xspic_trigger_command(cad_params, cmd_reg);
	xspic_trigger_command(cad_params, glue_reg);

	ret = xspic_wait_for_data(cad_params);
	if (ret < 0) {
		goto out;
	}

	sdma_size = sys_read32(cad_params->reg_base + XSPI_CTRL_SDMA_SIZE);
	sdma_trd_info = sys_read32(cad_params->reg_base + XSPI_CTRL_SDMA_TRD_INFO);
	if ((sdma_trd_info & XSPI_SDMA_TRD_INFO_DIR) == 0) { /* expect write */
		ret = -EIO;
		goto out;
	}

	key = irq_lock();
	xspic_mmio_write(cad_params->xip_base, src, sdma_size);
	irq_unlock(key);
	barrier_dsync_fence_full();

	ret = xspic_stig_wait_for_instr_end(cad_params);
	if (ret < 0) {
		goto out;
	}

	ret = xspic_poll_for_wip_bit(cad_params);
	if (ret == 0) {
		ret = xspic_wait_for_engine_idle(cad_params);
	}

out:
	xspic_set_interrupts(cad_params, false);
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

	struct xspic_cmd_cfg erase_cmd;

	if (xspic_is_octa_mode(cad_params)) {
		xspic_cmd_cfg_octa(cad_params, &erase_cmd, XSPI_OP_ERASE_4K_4B, XSPI_OPI_SE_EXT);
		erase_cmd.addr_bytes = 4;
	} else {
		xspic_cmd_cfg_default(cad_params, &erase_cmd, SPI_NOR_CMD_SE);
		erase_cmd.addr_bytes = 3;
	}
	xspic_encode_stig_write(&erase_cmd, cmd_reg, glue_reg, offset, 0, 0, 0);

	xspic_trigger_command(cad_params, cmd_reg);
	ret = xspic_stig_wait_for_instr_end(cad_params);
	if (ret < 0) {
		goto out;
	}

	ret = xspic_poll_for_wip_bit(cad_params);
	if (ret == 0) {
		ret = xspic_wait_for_engine_idle(cad_params);
	}

out:
	/* Switch back to original mode */
	xspic_set_ctrl_work_mode(cad_params, mode);

	return ret;
}

#ifdef CONFIG_DMA
static void flash_cad_xspi_dma_callback(const struct device *dev, void *user_data,
					uint32_t channel, int status)
{
	struct flash_cad_xspi_priv *priv = (struct flash_cad_xspi_priv *)user_data;

	priv->dma_status = status;
	k_sem_give(&priv->dma_sync);
}
#endif

static int flash_cad_xspi_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct cad_xspi_params *cad_params = &priv->params;
	uint32_t dest = (uint32_t)data;
	int ret = 0;

	if ((data == NULL) || (len == 0) || (offset < 0) || (offset + len > cad_params->xip_size)) {
		LOG_ERR("Invalid input parameter for XSPI Read!");
		return -EINVAL;
	}

	acquire_device(dev);

#ifdef CONFIG_DMA
	if (priv->dma_dev && (len > DMA_THRESHOLD) && !(len % 4) && !(offset % 4) && !(dest % 4)) {
		priv->dma_cfg.head_block = &priv->dma_block;
		priv->dma_block.dest_address = (uint32_t)dest;
		priv->dma_block.source_address = cad_params->xip_base + offset;
		priv->dma_block.block_size = len;
		priv->dma_block.next_block = NULL;

		ret = dma_config(priv->dma_dev, priv->dma_channel, &priv->dma_cfg);
		if (ret != 0) {
			LOG_ERR("dma_config failed");
			release_device(dev);
			return ret;
		}

		priv->dma_status = 0;
		ret = dma_start(priv->dma_dev, priv->dma_channel);
		if (ret != 0) {
			LOG_ERR("dma_start failed");
			release_device(dev);
			return ret;
		}

		k_sem_take(&priv->dma_sync, K_FOREVER);

		sys_cache_data_invd_range((void *)dest, len);

		dma_stop(priv->dma_dev, priv->dma_channel);

		release_device(dev);

		return priv->dma_status;
	}
#endif /* CONFIG_DMA */

	/* Use direct mode with XIP access */
	xspic_copy((void *)dest, (void *)(cad_params->xip_base + offset), len);
	barrier_dsync_fence_full();

	release_device(dev);

	return ret;
}

static int flash_cad_xspi_erase(const struct device *dev, off_t offset, size_t len)
{
	const struct flash_cad_xspi_config *config = DEV_CFG(dev);
	struct flash_cad_xspi_priv *priv = dev->data;
	struct cad_xspi_params *cad_params = &priv->params;
	uint32_t erase_size = SPI_NOR_SECTOR_SIZE;
	int ret = 0;

	erase_size = config->pages_layout.pages_size;
	if ((offset % erase_size) || (offset < 0) || offset + len > cad_params->xip_size) {
		LOG_ERR("Invalid input parameter for XSPI erase!");
		return -EINVAL;
	}

	acquire_device(dev);

	while (len > 0) {
		ret = xspic_stig_cmd_erase(cad_params, offset);
		if (ret < 0) {
			if (xspic_try_global_unlock(cad_params) == 0) {
				ret = xspic_stig_cmd_erase(cad_params, offset);
			}
		}

		if (ret < 0) {
			LOG_ERR("Cadence XSPI Flash Erase Failed!");
			break;
		}

		sys_cache_data_invd_range((void *)(cad_params->xip_base + offset),
					  erase_size);

		offset += erase_size;
		if (len < erase_size) {
			break;
		}
		len -= erase_size;
	}

	release_device(dev);

	return ret;
}

static int flash_cad_xspi_write(const struct device *dev, off_t offset, const void *data,
				size_t len)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	const struct flash_cad_xspi_config *config = DEV_CFG(dev);
	struct cad_xspi_params *cad_params = &priv->params;
	uint32_t write_size = MIN(len, SPI_NOR_PAGE_SIZE - (offset % SPI_NOR_PAGE_SIZE));
	uint8_t *src = (uint8_t *)data;
	int ret = 0;

	if ((data == NULL) || (len == 0) || (offset < 0) || (offset + len > cad_params->xip_size)) {
		LOG_ERR("Invalid input parameter for XSPI Write!");
		return -EINVAL;
	}

	acquire_device(dev);

#ifdef CONFIG_DMA
	if (priv->dma_dev && (len > DMA_THRESHOLD) && !(len % 4) && !(offset % 4)) {
		uint32_t mode;

		priv->dma_cfg.head_block = &priv->dma_block;
		priv->dma_block.source_address = (uint32_t)src;
		priv->dma_block.dest_address = cad_params->xip_base + offset;
		priv->dma_block.block_size = len;
		priv->dma_block.next_block = NULL;

		sys_set_bit(cad_params->reg_base + XSPI_CTRL_WE_SEQ_CFG_0,
			    XSPI_CTRL_WE_SEQ_CFG_0_EN_POS);

		sys_cache_data_flush_range((void *)src, len);

		ret = dma_config(priv->dma_dev, priv->dma_channel, &priv->dma_cfg);
		if (ret != 0) {
			LOG_ERR("dma_config failed");
			return ret;
		}

		priv->dma_status = 0;
		ret = dma_start(priv->dma_dev, priv->dma_channel);
		if (ret != 0) {
			LOG_ERR("dma_start failed");
			return ret;
		}

		k_sem_take(&priv->dma_sync, K_FOREVER);

		dma_stop(priv->dma_dev, priv->dma_channel);

		mode = xspic_set_ctrl_work_mode(cad_params, STIG);

		ret = xspic_poll_for_wip_bit(cad_params);
		if (ret < 0) {
			LOG_ERR("Cadence XSPI DMA write polling failed!");
			xspic_set_ctrl_work_mode(cad_params, mode);
			release_device(dev);
			return ret;
		}

		ret = xspic_wait_for_engine_idle(cad_params);
		if (ret < 0) {
			LOG_ERR("Cadence XSPI DMA write engine idle wait failed!");
			xspic_set_ctrl_work_mode(cad_params, mode);
			release_device(dev);
			return ret;
		}

		(void)xspic_set_ctrl_work_mode(cad_params, mode);

		sys_cache_data_invd_range((void *)(cad_params->xip_base + offset), len);

		sys_clear_bit(cad_params->reg_base + XSPI_CTRL_WE_SEQ_CFG_0,
			      XSPI_CTRL_WE_SEQ_CFG_0_EN_POS);

		release_device(dev);

		return priv->dma_status;
	}
#endif /* CONFIG_DMA */

	while (len) {
		if (config->use_direct_mode) {
			uint32_t mode = xspic_set_ctrl_work_mode(cad_params, STIG);
			unsigned int key;

			ret = xspi_set_write_enable(cad_params);
			if (ret < 0) {
				LOG_ERR("Failed to set write enable!");
				break;
			}

			(void)xspic_set_ctrl_work_mode(cad_params, mode);

			key = irq_lock();
			xspic_mmio_write(cad_params->xip_base + offset, src, write_size);
			irq_unlock(key);
			barrier_dsync_fence_full();

			mode = xspic_set_ctrl_work_mode(cad_params, STIG);
			ret = xspic_poll_for_wip_bit(cad_params);
			if (ret < 0) {
				LOG_ERR("Cadence XSPI Flash Write polling failed!");
				break;
			}

			ret = xspic_wait_for_engine_idle(cad_params);
			if (ret < 0) {
				LOG_ERR("Cadence XSPI Flash engine idle wait failed!");
				break;
			}

			/* Direct-mode writes update flash behind the XIP mapping, so invalidate
			 * the destination window before any immediate read-back/verify.
			 */
			sys_cache_data_invd_range((void *)(cad_params->xip_base + offset),
						  write_size);

			(void)xspic_set_ctrl_work_mode(cad_params, mode);
		} else {
			/* Use STIG mode */
			ret = xspic_stig_cmd_write_data(cad_params, offset, src, write_size);
			if (ret < 0) {
				if (xspic_try_global_unlock(cad_params) == 0) {
					ret = xspic_stig_cmd_write_data(cad_params, offset, src,
								 write_size);
				}
			}

			if (ret < 0) {
				LOG_ERR("Cadence XSPI Flash Write Failed!");
				break;
			}

			sys_cache_data_invd_range((void *)(cad_params->xip_base + offset),
						  write_size);
		}

		len -= write_size;
		offset += write_size;
		src += write_size;
		write_size = MIN(len, SPI_NOR_PAGE_SIZE);
	}

	release_device(dev);

	return ret;
}

#if defined(CONFIG_FLASH_JESD216_API)
static int flash_cad_xspi_sfdp_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct cad_xspi_params *cad_params = &priv->params;
	int ret;

	acquire_device(dev);
	ret = xspic_stig_read_data(cad_params, JESD216_CMD_READ_SFDP, offset, (uint8_t *)data, len,
				   3U, 0, 8);
	release_device(dev);

	return ret;
}

static int flash_cad_xspi_read_jedec_id(const struct device *dev, uint8_t *id)
{
	struct flash_cad_xspi_priv *priv = dev->data;
	struct cad_xspi_params *cad_params = &priv->params;
	int ret;

	acquire_device(dev);
	ret = xspic_stig_read_data(cad_params, JESD216_CMD_READ_ID, 0, id, JESD216_READ_ID_LEN,
				   0U, 0, 0);
	release_device(dev);

	return ret;
}
#endif /* CONFIG_FLASH_JESD216_API */

static const struct flash_parameters *flash_cad_xspi_get_parameters(const struct device *dev)
{
	const struct flash_cad_xspi_config *config = DEV_CFG(dev);

	return config->parameters;
}

#if CONFIG_FLASH_PAGE_LAYOUT
static void flash_cad_xspi_page_layout(const struct device *dev,
				       const struct flash_pages_layout **layout,
				       size_t *layout_size)
{
	const struct flash_cad_xspi_config *config = DEV_CFG(dev);

	*layout = &config->pages_layout;
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
	struct cad_xspi_params *cad_params = &priv->params;
	const struct flash_cad_xspi_config *config = DEV_CFG(dev);
	int ret;

	DEVICE_MMIO_NAMED_MAP(dev, xspi_reg, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, xspi_xip, K_MEM_CACHE_NONE);

	cad_params->reg_base = DEVICE_MMIO_NAMED_GET(dev, xspi_reg);
	cad_params->xip_base = DEVICE_MMIO_NAMED_GET(dev, xspi_xip);
	cad_params->current_io = IO1;
	cad_params->read_dummy = 0;
	cad_params->stig_chip_select = config->stig_chip_select;

#if defined(CONFIG_PINCTRL)
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("XSPI pinctrl setup failed (%d)", ret);
		return ret;
	}
#endif

#ifdef CONFIG_DMA
	if (priv->dma_dev && !device_is_ready(priv->dma_dev)) {
		LOG_ERR("DMA device not ready");
		return -ENODEV;
	}

	k_sem_init(&priv->dma_sync, 0, 1);
	priv->dma_cfg.user_data = (void *)priv;
#endif

#if defined(CONFIG_CLOCK_CONTROL)
	if (config->clk_dev) {
		ret = clock_control_on(config->clk_dev, config->clk_id);
		if (ret < 0) {
			LOG_ERR("Failed to enable clock (%d)", ret);
			return ret;
		}

		if (config->assigned_clock_rate > 0) {
			ret = clock_control_set_rate(config->clk_dev, config->clk_id,
					(clock_control_subsys_rate_t)config->assigned_clock_rate);
			if (ret < 0) {
				LOG_ERR("Failed to set clock rate (%d)", ret);
				return ret;
			}
		}
	}
#endif

#if defined(CONFIG_RESET)
	if (config->reset.dev != NULL) {
		ret = reset_line_deassert_dt(&config->reset);
		if (ret < 0) {
			LOG_ERR("Failed to de-assert reset");
			return ret;
		}
	}
#endif

	ret = cadence_xspi_soc_pre_init(cad_params->reg_base);
	if (ret < 0) {
		return ret;
	}

	ret = xspic_phy_init(cad_params->reg_base);
	if (ret < 0) {
		LOG_ERR("Failed to initialize PHY (%d)", ret);
		return ret;
	}

	if (config->octa_mode) {
		ret = xspic_enable_octa_mode(cad_params);
		if (ret < 0) {
			LOG_ERR("Failed to enable XSPI octa mode (%d)", ret);
			return ret;
		}
	} else if (config->quad_mode) {
		ret = xspic_enable_quad_mode(cad_params, config);
		if (ret < 0) {
			LOG_ERR("Failed to enable XSPI quad mode (%d)", ret);
			return ret;
		}
	}

	sys_write32(cad_params->xip_base, cad_params->reg_base + XSPI_CTRL_DIRECT_ACCESS_RMP);
	sys_write32(XSPI_CTRL_RMP_ADDR_EN |
		    (cad_params->stig_chip_select == 1U ? XSPI_DIRECT_ACCESS_CS1 : 0U),
		    cad_params->reg_base + XSPI_CTRL_DIRECT_ACCESS_CFG);
	/* Limit program page size to 256 bytes */
	sys_write32(XSPI_CTRL_PAGE_SIZE_256, cad_params->reg_base + XSPI_CTRL_GLOBAL_SEQ_CFG);

	xspic_direct_mode_enable(cad_params);

	/* Switch to DIRECT mode */
	(void)xspic_set_ctrl_work_mode(cad_params, DIRECT);
	if (!xspic_is_octa_mode(cad_params)) {
		/* Disable Write-Enable command in direct mode */
		sys_write32(0x1f90006, cad_params->reg_base + XSPI_CTRL_WE_SEQ_CFG_0);
	}

	if (IS_ENABLED(CONFIG_MULTITHREADING)) {
		k_sem_init(&priv->sem, 1, K_SEM_MAX_LIMIT);
	}

	return 0;
}

#if defined(CONFIG_CLOCK_CONTROL)
#define CLOCK_XSPI_CONFIG(n)                                                                       \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, clocks),                                               \
		   (.clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                              \
		    .clk_id = (clock_control_subsys_t)                                             \
			      DT_INST_CLOCKS_CELL(n, clkid),                                       \
		    .assigned_clock_rate = DT_INST_PROP_OR(n, assigned_clock_rates, 0),))
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

#if CONFIG_FLASH_PAGE_LAYOUT
#define PAGE_LAYOUT(n)                                                                             \
	.pages_layout = {                                                                          \
		.pages_count = DT_INST_REG_SIZE_BY_IDX(n, 1) / DT_INST_PROP(n, erase_block_size),  \
		.pages_size = DT_INST_PROP(n, erase_block_size),                                   \
	},
#else
#define PAGE_LAYOUT(n)
#endif

#ifdef CONFIG_DMA
#define DMA_XSPI_CONFIG(inst) \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, dmas), \
		(.dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR(inst)),                                \
		 .dma_channel = DT_INST_DMAS_CELL_BY_IDX(inst, 0, channel),                        \
		 .dma_cfg = {                                                                      \
			.source_burst_length = 16,                                                 \
			.dest_burst_length = 16,                                                   \
			.source_data_size = 4,                                                     \
			.dest_data_size = 4,                                                       \
			.complete_callback_en = 1,                                                 \
			.error_callback_dis = 0,                                                   \
			.block_count = 1,                                                          \
			.channel_direction = MEMORY_TO_MEMORY,                                     \
			.dma_slot = 0,                                                             \
			.dma_callback = flash_cad_xspi_dma_callback,                               \
			.user_data = (void *)DEVICE_DT_INST_GET(inst)                              \
		 },),                                                                              \
		(.dma_dev = NULL,))
#else
#define DMA_XSPI_CONFIG(inst)
#endif

#if defined(CONFIG_PINCTRL)
#define PINCTRL_XSPI_DEFINE(inst) PINCTRL_DT_INST_DEFINE(inst)
#define PINCTRL_XSPI_CONFIG(inst) .pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),
#else
#define PINCTRL_XSPI_DEFINE(inst)
#define PINCTRL_XSPI_CONFIG(inst)
#endif

#define CREATE_FLASH_CADENCE_XSPI_DEVICE(inst)                                                     \
	PINCTRL_XSPI_DEFINE(inst);                                                              \
	static struct flash_cad_xspi_priv flash_cad_xspi_priv_##inst = {                           \
		.params = {                                                                        \
			.xip_size = DT_INST_REG_SIZE_BY_IDX(inst, 1),                              \
		},                                                                                 \
		DMA_XSPI_CONFIG(inst)                                                              \
	};                                                                                         \
                                                                                                   \
	static const struct flash_parameters flash_cad_xspi_parameters_##inst = {                  \
		.write_block_size = DT_INST_PROP(inst, write_block_size),                          \
		.erase_value = 0xff,                                                               \
	};                                                                                         \
                                                                                                   \
	static const struct flash_cad_xspi_config flash_cad_xspi_config_##inst = {                 \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(xspi_reg, DT_DRV_INST(inst)),                   \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(xspi_xip, DT_DRV_INST(inst)),                   \
		PINCTRL_XSPI_CONFIG(inst)                                      \
		.use_direct_mode = DT_INST_PROP_OR(inst, use_direct_mode, false),                  \
		.quad_mode = DT_INST_ENUM_IDX_OR(inst, io_mode, 0) == 1,                       \
		.octa_mode = DT_INST_ENUM_IDX_OR(inst, io_mode, 0) == 2,                       \
		.stig_chip_select = DT_INST_PROP_OR(inst, stig_chip_select, 0),                    \
		.quad_read_dummy = DT_INST_PROP_OR(inst, quad_read_dummy, 6),                       \
		.quad_enable_command = DT_INST_PROP_OR(inst, quad_enable_command, 0),               \
		.quad_enable_bit = DT_INST_PROP_OR(inst, quad_enable_bit, 6),                       \
		.quad_read_sr1_command = DT_INST_PROP_OR(inst, quad_read_sr1_command, 5),           \
		.quad_read_sr2_command = DT_INST_PROP_OR(inst, quad_read_sr2_command, 0),           \
		.quad_write_sr_command = DT_INST_PROP_OR(inst, quad_write_sr_command, 1),           \
		.parameters = &flash_cad_xspi_parameters_##inst,                                   \
		PAGE_LAYOUT(inst) CLOCK_XSPI_CONFIG(inst) RESET_XSPI_CONFIG(inst)};                \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, flash_cad_xspi_init, NULL, &flash_cad_xspi_priv_##inst,        \
			      &flash_cad_xspi_config_##inst, POST_KERNEL,                          \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &flash_cad_xspi_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_FLASH_CADENCE_XSPI_DEVICE)
