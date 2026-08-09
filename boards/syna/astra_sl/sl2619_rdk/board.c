/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(board_sl2619_rdk, LOG_LEVEL_DBG);

#define GLOBAL_SEC_BASE_ADDRESS 0x58024000
#define GLOBAL_BASE_ADDRESS 0x58025000

#define INIT_INHIBIT                    0
#define INIT_WP_ENABLE                  1
#define INIT_SW_CTRLED_HW_RST_OPTION    0
#define INIT_RST_DQ3_ENABLE             0
#define INIT_SEQ_TYPE                   0
#define INIT_SEQ_DATA_SWAP              0
#define INIT_SEQ_CRC_EN                 0
#define INIT_SEQ_CRC_OE                 0
#define INIT_SEQ_CRC_CHUNK_SIZE         0
#define INIT_SEQ_CRC_UAL_CHUNK_EN       0
#define INIT_SEQ_CRC_UAL_CHUNK_CHK      0

#define INIT_READ_SEQ_P1_MB_DUMMY_CNT   7
#define INIT_READ_SEQ_P1_MB_EN          0
#define INIT_READ_SEQ_P1_DUMMY_CNT      6
#define INIT_READ_SEQ_P1_CMD_EXT_EN     0
#define INIT_READ_SEQ_P1_CMD_EXT_VAL    0

#define INIT_READ_SEQ_P1_CMD_VAL        0xEB
#define INIT_READ_SEQ_P1_CMD_IOS        0   /*0-1bit, 1-2bit, 2-4bit, 3-8bit */
#define INIT_READ_SEQ_P1_CMD_EDGE       0   /*0-SDR, 1-DDR */
#define INIT_READ_SEQ_P1_ADDR_CNT       3   /*3-3B address, 4-4B address, 0-no address */
#define INIT_READ_SEQ_P1_ADDR_IOS       2   /*0-1bit, 1-2bit, 2-4bit, 3-8bit */
#define INIT_READ_SEQ_P1_ADDR_EDGE      0   /*0-SDR, 1-DDR */
#define INIT_READ_SEQ_P1_DATA_IOS       2   /*0-1bit, 1-2bit, 2-4bit, 3-8bit */
#define INIT_READ_SEQ_P1_DATA_EDGE      0   /*0-SDR, 1-DDR */

#define INIT_RB_VALID_TIME              0x0000000a

#define INIT_PHY_DQ_TIMING_REG        0x80000101
#define INIT_PHY_DQS_TIMING_REG       0x00300404
#define INIT_PHY_GATE_LPBK_CTRL_REG   0x00180030
#define INIT_PHY_DLL_MASTER_CTRL_REG    0x00000013
#define INIT_PHY_DLL_SLAVE_CTRL_REG   0x00000f3f

#define GLOBAL_PERIF_XSPI_RB_VALID_TIME_OFFSET  0x834
#define GLOBAL_PERIF_XSPI_CTRL0_OFFSET          0x824
#define GLOBAL_PERIF_XSPI_STATUS_OFFSET         0x84C

#define GLOBAL_PERIF_XSPI_STATUS__INIT_COMP_MASK   0x1

#define GLOBAL_PERIF_XSPI_CTRL0__INHIBIT_POS                           0
#define GLOBAL_PERIF_XSPI_CTRL0__WP_EN_POS                             1
#define GLOBAL_PERIF_XSPI_CTRL0__SW_CTRL_HW_RST_POS                    2
#define GLOBAL_PERIF_XSPI_CTRL0__RST_DQ3_ENABLE_POS                    3
#define GLOBAL_PERIF_XSPI_CTRL0__SEQ_TYPE_POS                          4
#define GLOBAL_PERIF_XSPI_CTRL0__SEQ_DATA_SWAP_POS                     5
#define GLOBAL_PERIF_XSPI_CTRL0__SEQ_CRC_EN_POS                        6
#define GLOBAL_PERIF_XSPI_CTRL0__SEQ_CRC_OE_POS                        7
#define GLOBAL_PERIF_XSPI_CTRL0__SEQ_CRC_CHUNK_SIZE_POS                8
#define GLOBAL_PERIF_XSPI_CTRL0__SEQ_CRC_UAL_CHUNK_EN_POS              11
#define GLOBAL_PERIF_XSPI_CTRL0__SEQ_CRC_UAL_CHUNK_CHK_POS             12
#define GLOBAL_PERIF_XSPI_CTRL1__IDDQ_EN_POS                           0
#define GLOBAL_PERIF_XSPI_CTRL1__DQS_REMOD_EN_POS                      1
#define GLOBAL_PERIF_XSPI_CTRL1__SDR_EDGE_ACTIVE_POS                   2
#define GLOBAL_PERIF_XSPI_CTRL1__LAST_DATA_DROP_EN_POS                 3
#define GLOBAL_PERIF_XSPI_CTRL2__READ_SEQ_PL_MB_EN_POS                 0
#define GLOBAL_PERIF_XSPI_CTRL2__READ_SEQ_P1_MB_DUMMY_CNT_POS          1
#define GLOBAL_PERIF_XSPI_CTRL2__READ_SEQ_P1_DUMMY_CNT_POS             7
#define GLOBAL_PERIF_XSPI_CTRL2__READ_SEQ_P1_CMD_EXT_EN_POS            13
#define GLOBAL_PERIF_XSPI_CTRL2__READ_SEQ_P1_CMD_EXT_VAL_POS           14
#define GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_CMD_VAL_POS               0
#define GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_CMD_IOS_POS               8
#define GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_CMD_EDGE_POS              10
#define GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_ADDR_CNT_POS              11
#define GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_ADDR_IOS_POS              14
#define GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_ADDR_EDGE_POS             16
#define GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_DATA_IOS_POS              17
#define GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_DATA_EDGE_POS             19


#define GLOBAL_PERIF_XSPI_CTRL0_OFFSET                                 0x824
#define GLOBAL_PERIF_XSPI_CTRL1_OFFSET                                 0x828
#define GLOBAL_PERIF_XSPI_CTRL2_OFFSET                                 0x82C
#define GLOBAL_PERIF_XSPI_CTRL3_OFFSET                                 0x830

#define GLOBAL_PERIF_XSPI_PHY_DQ_TIMING_OFFSET                         0x838
#define GLOBAL_PERIF_XSPI_PHY_DQS_TIMING_OFFSET                        0x83C
#define GLOBAL_PERIF_XSPI_PHY_GATE_LPBK_CTRL_OFFSET                    0x840
#define GLOBAL_PERIF_XSPI_PHY_DLL_MASTER_CTRL_OFFSET                   0x844
#define GLOBAL_PERIF_XSPI_PHY_DLL_SLAVE_CTRL_OFFSET                    0x848

#define MCU_GBL_CFG_SEC_RST_SYNC_STICKY_RST0_OFFSET       0x418
#define GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1_OFFSET        0x404
#define GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST0_OFFSET        0x400
#define MCU_GBL_CFG_SEC_SYS_ROM_PWR_CTRL_OFFSET           0xA34
#define GLOBAL_MCU_GBL_XSPI_SRAM_PWR_CTRL_OFFSET          0xA00

#define MCU_GBL_CFG_SEC_RST_SYNC_STICKY_RST0__OTF_CRYPTO_STICKY_PRSTN_MASK          (0x1 <<5)
#define GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1__XSPI_STICKY_ARSTN_MASK                 (0x1 << 0)
#define GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1__XSPI_STICKY_PRSTN_MASK                 (0x1 << 1)
#define GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1__XSPI_STICKY_SRSTN_MASK                 (0x1 << 2)

#define XSPI_RESET_BITS             (GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1__XSPI_STICKY_ARSTN_MASK | \
                                     GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1__XSPI_STICKY_PRSTN_MASK | \
                                     GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1__XSPI_STICKY_SRSTN_MASK)

#define hw_reg_write(addr, value) (*(volatile uint32_t *)(addr) = (value))
#define hw_reg_read(addr) (*(volatile uint32_t *)(addr))

static void xspi_init_bootstrap(void)
{
    uint32_t gbl_base = GLOBAL_BASE_ADDRESS;
    uint32_t reg_val;

    hw_reg_write(gbl_base + GLOBAL_PERIF_XSPI_RB_VALID_TIME_OFFSET, INIT_RB_VALID_TIME);
    hw_reg_write(gbl_base + GLOBAL_PERIF_XSPI_CTRL0_OFFSET,
            (INIT_INHIBIT << (GLOBAL_PERIF_XSPI_CTRL0__INHIBIT_POS)) |
            (INIT_WP_ENABLE<<(GLOBAL_PERIF_XSPI_CTRL0__WP_EN_POS)) |
            (INIT_SW_CTRLED_HW_RST_OPTION<<(GLOBAL_PERIF_XSPI_CTRL0__SW_CTRL_HW_RST_POS)) |
            (INIT_RST_DQ3_ENABLE<<(GLOBAL_PERIF_XSPI_CTRL0__RST_DQ3_ENABLE_POS)) |
            (INIT_SEQ_TYPE<<(GLOBAL_PERIF_XSPI_CTRL0__SEQ_TYPE_POS)) |
            (INIT_SEQ_DATA_SWAP<<(GLOBAL_PERIF_XSPI_CTRL0__SEQ_DATA_SWAP_POS)) |
            (INIT_SEQ_CRC_EN<<(GLOBAL_PERIF_XSPI_CTRL0__SEQ_CRC_EN_POS)) |
            (INIT_SEQ_CRC_OE<<(GLOBAL_PERIF_XSPI_CTRL0__SEQ_CRC_OE_POS)) |
            (INIT_SEQ_CRC_CHUNK_SIZE<<(GLOBAL_PERIF_XSPI_CTRL0__SEQ_CRC_CHUNK_SIZE_POS)) |
            (INIT_SEQ_CRC_UAL_CHUNK_EN<<(GLOBAL_PERIF_XSPI_CTRL0__SEQ_CRC_UAL_CHUNK_EN_POS)) |
            (INIT_SEQ_CRC_UAL_CHUNK_CHK<<(GLOBAL_PERIF_XSPI_CTRL0__SEQ_CRC_UAL_CHUNK_CHK_POS))
            );
    reg_val = hw_reg_read(gbl_base + GLOBAL_PERIF_XSPI_CTRL1_OFFSET);
    reg_val |= 2;  //DQS_REMOD_EN set to 0 to enable CLK_N
    hw_reg_write(gbl_base + GLOBAL_PERIF_XSPI_CTRL1_OFFSET, reg_val);

    hw_reg_write(gbl_base + GLOBAL_PERIF_XSPI_CTRL3_OFFSET,
            (INIT_READ_SEQ_P1_CMD_VAL<<(GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_CMD_VAL_POS)) |
            (INIT_READ_SEQ_P1_CMD_IOS<<(GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_CMD_IOS_POS)) |
            (INIT_READ_SEQ_P1_CMD_EDGE<<(GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_CMD_EDGE_POS)) |
            (INIT_READ_SEQ_P1_ADDR_CNT<<(GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_ADDR_CNT_POS)) |
            (INIT_READ_SEQ_P1_ADDR_IOS<<(GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_ADDR_IOS_POS)) |
            (INIT_READ_SEQ_P1_ADDR_EDGE<<(GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_ADDR_EDGE_POS)) |
            (INIT_READ_SEQ_P1_DATA_IOS<<(GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_DATA_IOS_POS)) |
            (INIT_READ_SEQ_P1_DATA_EDGE<<(GLOBAL_PERIF_XSPI_CTRL3__READ_SEQ_P1_DATA_EDGE_POS))
            );
    hw_reg_write(gbl_base + GLOBAL_PERIF_XSPI_CTRL2_OFFSET,
            (INIT_READ_SEQ_P1_MB_EN<<(GLOBAL_PERIF_XSPI_CTRL2__READ_SEQ_PL_MB_EN_POS)) |
            (INIT_READ_SEQ_P1_MB_DUMMY_CNT<<(GLOBAL_PERIF_XSPI_CTRL2__READ_SEQ_P1_MB_DUMMY_CNT_POS)) |
            (INIT_READ_SEQ_P1_DUMMY_CNT<<(GLOBAL_PERIF_XSPI_CTRL2__READ_SEQ_P1_DUMMY_CNT_POS)) |
            (INIT_READ_SEQ_P1_CMD_EXT_EN<<(GLOBAL_PERIF_XSPI_CTRL2__READ_SEQ_P1_CMD_EXT_EN_POS)) |
            (INIT_READ_SEQ_P1_CMD_EXT_VAL<<(GLOBAL_PERIF_XSPI_CTRL2__READ_SEQ_P1_CMD_EXT_VAL_POS))
            );

    hw_reg_write(gbl_base + GLOBAL_PERIF_XSPI_PHY_DQ_TIMING_OFFSET, INIT_PHY_DQ_TIMING_REG);
    hw_reg_write(gbl_base + GLOBAL_PERIF_XSPI_PHY_DQS_TIMING_OFFSET, INIT_PHY_DQS_TIMING_REG);
    hw_reg_write(gbl_base + GLOBAL_PERIF_XSPI_PHY_GATE_LPBK_CTRL_OFFSET, INIT_PHY_GATE_LPBK_CTRL_REG);
    hw_reg_write(gbl_base + GLOBAL_PERIF_XSPI_PHY_DLL_MASTER_CTRL_OFFSET, INIT_PHY_DLL_MASTER_CTRL_REG);
    hw_reg_write(gbl_base + GLOBAL_PERIF_XSPI_PHY_DLL_SLAVE_CTRL_OFFSET, INIT_PHY_DLL_SLAVE_CTRL_REG);

}

/**
 * @brief reset xSPI controller
 *
 * @return
 */
static void xspi_reset(void)
{
    uint32_t rdata;
    uint32_t gbl_base = GLOBAL_BASE_ADDRESS;

    /* update XSPI clock to 50MHz */
    uint32_t *addr = (uint32_t *)(gbl_base + 0x628);
    sys_write32(0x9, (uintptr_t)addr); /* div = 3 (2+1) 50MHz */
    sys_write32(0xB, (uintptr_t)addr);
    sys_write32(0x9, (uintptr_t)addr);

    rdata = hw_reg_read(gbl_base + GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1_OFFSET); /* sticky reset */
    rdata &= ~(XSPI_RESET_BITS);
    hw_reg_write(gbl_base + GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1_OFFSET, rdata);
    rdata = hw_reg_read(gbl_base + GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1_OFFSET);

    /* wait for 200 cycles */
    for (volatile uint32_t i = 0; i < (200); i++) {
        __NOP();
    }

    rdata |= XSPI_RESET_BITS;
    hw_reg_write(gbl_base + GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1_OFFSET, rdata);
    rdata = hw_reg_read(gbl_base + GLOBAL_MCU_GBL_RST_SYNC_STICKY_RST1_OFFSET);

    /* release otf_crypto reset */
    rdata = hw_reg_read(GLOBAL_SEC_BASE_ADDRESS + MCU_GBL_CFG_SEC_RST_SYNC_STICKY_RST0_OFFSET);
    rdata |= MCU_GBL_CFG_SEC_RST_SYNC_STICKY_RST0__OTF_CRYPTO_STICKY_PRSTN_MASK;
    hw_reg_write(GLOBAL_SEC_BASE_ADDRESS + MCU_GBL_CFG_SEC_RST_SYNC_STICKY_RST0_OFFSET, rdata);

    /* sram power on */
    hw_reg_write(gbl_base + GLOBAL_MCU_GBL_XSPI_SRAM_PWR_CTRL_OFFSET, 0);

}

/**
 * @brief xspi controller wait_init_comp
 *
 * @return
 */
static int32_t xspi_wait_init_comp(void )
{
    uint32_t init_comp;
    uint32_t gbl_base = GLOBAL_BASE_ADDRESS;

    do {
        init_comp = hw_reg_read(gbl_base + GLOBAL_PERIF_XSPI_STATUS_OFFSET);
    } while ((init_comp != GLOBAL_PERIF_XSPI_STATUS__INIT_COMP_MASK));

    if (init_comp != GLOBAL_PERIF_XSPI_STATUS__INIT_COMP_MASK)
        return -1;

    return 0;
}

int board_init(void)
{
    xspi_init_bootstrap();
    xspi_reset();
    if(xspi_wait_init_comp() != 0) {
        printk("Error: xSPI initialization failed\n");
    }
    return 0;
}

/* Initialize before I2C driver (priority 50) */
SYS_INIT(board_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
