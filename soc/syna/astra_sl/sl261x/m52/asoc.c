/*
 * Copyright (c) 2026 Synaptics Incorporated.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file asoc.c
 * @brief ACPU initialization and setup, including clock, reset, power, and memory mapping.
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <soc.h>
#include <asoc.h>

LOG_MODULE_REGISTER(asoc, LOG_LEVEL_INF);

#define START_SOC_REG                 0xF7E20000
#define START_CHIP_CTRL_REG           0xF7E10000

#define START_MCU_GBL_SEC_S           0x58024000

#define RA_SOC_PERIF_PPC0_CTRL0          0x1700
#define RA_SOC_PERIF_PPC0_CTRL2          0x1708

#define RA_Gbl_clkEnable                 0x0420
#define RA_Gbl_atbClk                    0x045C

#define RA_mcu_gbl_cfg_sec_asoc_pwr_ctrl 0x0058
#define RA_mcu_gbl_cfg_sec_asoc_pwr_strs 0x005C

#define ASOC_PWR_ctrl_asoc2mcu_iso_en        0x00000002
#define ASOC_PWR_strs_asoc_clkrst_rdy        0x00000002
#define ASOC_PWR_ctrl_asoc1_cpu_sticky_rst   0x00000020
#define ASOC_PWR_ctrl_asoc_sticky_rst        0x00000004
#define ASOC_PWR_ctrl_mcu2asoc_iso_en        0x00000001
#define ASOC_PWR_ctrl_asoc1_sticky_rst       0x00000010
#define ASOC_PWR_ctrl_asoc_cpu_sticky_rst    0x00000008

#define INTF_REGION_VALID                0x0000
#define INTF_REGION_START                0x0004
#define INTF_REGION_END                  0x0008
#define INTF_REGION_OFFSET               0x000C

struct sysmgr_mapping {
    bool is_mcu2soc;
    uint32_t slot;
    uint32_t base;
    uint32_t src_start;
    uint32_t src_end;
    uint32_t dst_start;
    const char *description;
};

struct sysmgr_mapping sysmgr_map_1[] = {
    /* At first map ASOC reg region before mapping SOC to MCU */
    {true, 0, 0x5802004C, 0xD0000000, 0xE0000000-1, 0xF0000000, "ASOC REG"},
    {true, 1, 0x5802004C, 0xB0000000, 0xC0000000-1, 0x00000000, "ASOC DDR"},
};

struct sysmgr_mapping sysmgr_map_2[] = {
    {false, 0, 0xD7E2012C, 0xE0000000, 0xE0100000-1, 0x10000000, "ITCM"},
    {false, 1, 0xD7E2012C, 0xE1000000, 0xE1100000-1, 0x30000000, "DTCM"},
    {false, 2, 0xD7E2012C, 0xE2000000, 0xE3000000-1, 0x38000000, "XSPI"},
    {false, 3, 0xD7E2012C, 0xE3000000, 0xE3100000-1, 0x50000000, "MCU Peripheral0"},
    {false, 4, 0xD7E2012C, 0xE4000000, 0xE4100000-1, 0x58000000, "MCU Peripheral1"},
};

// minimum 1MB
static int map_region(uint32_t base, uint32_t slot, uint32_t src_start, uint32_t src_end, uint32_t dst_start)
{
    if (slot >= 8) {
        LOG_ERR("Invalid slot number %d\n", slot);
        return -1;
    }

    if (src_end  <= src_start) {
        LOG_ERR("Invalid, start:0x%08x >= end:0x%08x\n", src_start, src_end);
        return -1;
    }

    src_start >>= 20;
    src_end   >>= 20;
    dst_start >>= 20;

    base += slot * 0x10;

    sys_write32(src_start, base + INTF_REGION_START);
    sys_write32(src_end, base + INTF_REGION_END);
    sys_write32((0x1000 + dst_start - src_start) & 0xfff, base + INTF_REGION_OFFSET);
    sys_write32(1, base + INTF_REGION_VALID);
    return 0;
}

static void setup_soc(void)
{
    uint32_t reg_val;
    int timeout_us = 100000;    //100ms, Empirical

    // release ASOC ISO and sticky reset
    reg_val = sys_read32(START_MCU_GBL_SEC_S + RA_mcu_gbl_cfg_sec_asoc_pwr_ctrl);
    reg_val &= ~ASOC_PWR_ctrl_asoc_sticky_rst;
    reg_val &= ~ASOC_PWR_ctrl_asoc1_sticky_rst;
    reg_val &= ~ASOC_PWR_ctrl_mcu2asoc_iso_en;
    reg_val &= ~ASOC_PWR_ctrl_asoc2mcu_iso_en;
    sys_write32(reg_val, START_MCU_GBL_SEC_S + RA_mcu_gbl_cfg_sec_asoc_pwr_ctrl);

    // Wait for mcu_gbl_cfg_asoc_pwr_strs[asoc_clkrst_rdy]
    while((sys_read32(START_MCU_GBL_SEC_S + RA_mcu_gbl_cfg_sec_asoc_pwr_strs) & ASOC_PWR_strs_asoc_clkrst_rdy) == 0) {
        timeout_us -= 10;
    }

    // release ASOC  ACPU sticky reset
    reg_val &= ~ASOC_PWR_ctrl_asoc_cpu_sticky_rst;
    reg_val &= ~ASOC_PWR_ctrl_asoc1_cpu_sticky_rst;
    sys_write32(reg_val, START_MCU_GBL_SEC_S + RA_mcu_gbl_cfg_sec_asoc_pwr_ctrl);

    for (int i = 0; i < ARRAY_SIZE(sysmgr_map_1); i++) {
        const struct sysmgr_mapping *p = &sysmgr_map_1[i];
        map_region(p->base, p->slot, p->src_start, p->src_end, p->dst_start);
    }

    // set PPC for SoC APB as secure access
    sys_write32(0x1, SOCREG_REGION(START_SOC_REG + RA_SOC_PERIF_PPC0_CTRL0)); // bus fault
    sys_write32(0, SOCREG_REGION(START_SOC_REG + RA_SOC_PERIF_PPC0_CTRL2)); // secure only

    // ATB clk 600/3 = 200Mhz
    sys_write32(0x69, SOCREG_REGION(START_CHIP_CTRL_REG + RA_Gbl_atbClk));
    for (int i = 0; i < ARRAY_SIZE(sysmgr_map_2); i++) {
        const struct sysmgr_mapping *p = &sysmgr_map_2[i];
        map_region(p->base, p->slot, p->src_start, p->src_end, p->dst_start);
    }

    // Enable all clocks
    sys_write32(0xFFFFFFFF, SOCREG_REGION(START_CHIP_CTRL_REG + RA_Gbl_clkEnable));
    sys_write32(2, 0xd7e10378);
}

void release_asoc(uint32_t initvtor)
{
    LOG_DBG("Release ASOC...");
    sys_write32(0x1, 0xd7e31000);
    sys_write32(0x7fe, 0xd7e3000c);
    sys_write32(0x01800001, 0xd7e30018);
    sys_write32(initvtor, 0xd7e30800);
    sys_write32(initvtor, 0xd7e30808);
    sys_write32(0x7ff, 0xd7e3000c);
    sys_write32(0x12, 0x5803001c); /*change uart owner */
}

int asoc_setup(void)
{
    setup_soc();
    return 0;
}
