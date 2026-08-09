/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_flight_control

/**
 * @brief Driver for Synaptics flight control.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/fc.h>

struct fc_registers {
    uint32_t fault_grp0_input;
    uint32_t fault_grp0_flmask;
    uint32_t fault_grp1_input;
    uint32_t fault_grp1_flmask;
    uint32_t intr_grp_clear;
    uint32_t intr_grp_toclear;
    uint32_t intr_grp_mask;
    uint32_t intr_grp_edge;
    uint32_t intr_grp_status;
    uint32_t flctrl;
    uint32_t intrctrl;
    uint32_t trigctrl;
    /* Offset: 0x030 (R/W) reset-n control register */
    uint32_t rstnctrl;
    uint32_t interrupt0_faultasso0;
    uint32_t interrupt0_faultasso1;
    uint32_t interrupt1_faultasso0;
    uint32_t interrupt1_faultasso1;
    uint32_t interrupt2_faultasso0;
    uint32_t interrupt2_faultasso1;
    uint32_t interrupt3_faultasso0;
    uint32_t interrupt3_faultasso1;
    uint32_t interrupt4_faultasso0;
    uint32_t interrupt4_faultasso1;
    uint32_t interrupt5_faultasso0;
    uint32_t interrupt5_faultasso1;
    uint32_t interrupt6_faultasso0;
    uint32_t interrupt6_faultasso1;
    uint32_t interrupt7_faultasso0;
    uint32_t interrupt7_faultasso1;
    uint32_t interrupt8_faultasso0;
    uint32_t interrupt8_faultasso1;
    uint32_t interrupt9_faultasso0;
    uint32_t interrupt9_faultasso1;
    uint32_t interrupt10_faultasso0;
    uint32_t interrupt10_faultasso1;
    uint32_t interrupt11_faultasso0;
    uint32_t interrupt11_faultasso1;
    uint32_t interrupt12_faultasso0;
    uint32_t interrupt12_faultasso1;
    uint32_t interrupt13_faultasso0;
    uint32_t interrupt13_faultasso1;
    uint32_t interrupt14_faultasso0;
    uint32_t interrupt14_faultasso1;
    uint32_t interrupt15_faultasso0;
    uint32_t interrupt15_faultasso1;
    uint32_t trigger0_faultasso0;
    uint32_t trigger0_faultasso1;
    uint32_t trigger1_faultasso0;
    uint32_t trigger1_faultasso1;
    uint32_t trigger2_faultasso0;
    uint32_t trigger2_faultasso1;
    uint32_t trigger3_faultasso0;
    uint32_t trigger3_faultasso1;
    uint32_t trigger4_faultasso0;
    uint32_t trigger4_faultasso1;
    uint32_t trigger5_faultasso0;
    uint32_t trigger5_faultasso1;
    uint32_t trigger6_faultasso0;
    uint32_t trigger6_faultasso1;
    uint32_t trigger7_faultasso0;
    uint32_t trigger7_faultasso1;
    /* Offset: 0x0F4 (R/W) reset-0 fault associate register */
    uint32_t reset0_faultasso0;
    /* Offset: 0x0F8 (R/W) reset-0 fault associate register */
    uint32_t reset0_faultasso1;
    uint32_t reset1_faultasso0;
    uint32_t reset1_faultasso1;
    uint32_t reset2_faultasso0;
    uint32_t reset2_faultasso1;
    uint32_t reset3_faultasso0;
    uint32_t reset3_faultasso1;
    uint32_t reset4_faultasso0;
    uint32_t reset4_faultasso1;
    uint32_t reset5_faultasso0;
    uint32_t reset5_faultasso1;
    uint32_t reset6_faultasso0;
    uint32_t reset6_faultasso1;
    uint32_t reset7_faultasso0;
    uint32_t reset7_faultasso1;
};

struct fc_config {
    struct fc_registers *base;
};

static const struct fc_config fc_cfg = {
    .base = (struct fc_registers *)DT_INST_REG_ADDR(0),
};

void fc_configure_num_faults_for_reset(const struct device *dev, uint32_t num_faults)
{
    const struct fc_config *cfg = dev->config;
    cfg->base->rstnctrl = num_faults;
}

void fc_enable_reset0_on_fault(const struct device *dev, en_fault_id fault_id)
{
    const struct fc_config *cfg = dev->config;

    if(fault_id >= 32)
    {
        cfg->base->reset0_faultasso1 |= (1 << (fault_id % 32));
        return;
    }

    cfg->base->reset0_faultasso0 |= (1 << (fault_id % 32));
    return;
}

void fc_disable_reset0_on_fault(const struct device *dev, en_fault_id fault_id)
{
    const struct fc_config *cfg = dev->config;

    if(fault_id >= 32)
    {
        cfg->base->reset0_faultasso1 &= ~(1 << (fault_id % 32));
        return;
    }

    cfg->base->reset0_faultasso0 &= ~(1 << (fault_id % 32));
    return;
}

static int fc_init(const struct device *dev)
{
    return 0;
}


DEVICE_DT_INST_DEFINE(0,
                 fc_init,
                 NULL,
                 NULL,
                 &fc_cfg,
                 POST_KERNEL,
                 CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
                 NULL);
