/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT arm_corstone_syscounter

#include <zephyr/drivers/counter.h>
#include <zephyr/device.h>
#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <soc.h>

#include "system_counter_corstone.h"

struct corstone_syscounter_config {
    volatile struct corstone_syscounter_regs * base;
    uint32_t freq;
};

static int corstone_syscounter_init(const struct device *dev)
{
    const struct corstone_syscounter_config * const cfg = dev->config;

    if(cfg->base->cntcr & SYSCOUNT_CNTCR_ENABLE) {
        return 0;
    }

    cfg->base->cntcr &= (~SYSCOUNT_CNTCR_ENABLE);       /* Disable the system counter by clearing CNTCR.EN */
    cfg->base->cntcv_low = 0;                           /* Set counter value to zero by clearing CNTCV_LOW & CNTCV_HIGH */
    cfg->base->cntcv_high = 0;
    cfg->base->cntcr &= (~SYSCOUNT_CNTCR_INTRCLR);      /* Disable interrupt by clearing CNTCR.INTRCLR */
    cfg->base->cntcr &= (~SYSCOUNT_CNTCR_SCEN);         /* Disable scale by clearing CNTCR.SCEN */
    cfg->base->cntcr |= (1 << 0);                       /* Enable the counter by setting CNTCR.EN = 1 */

    return 0;
}

static int corstone_syscounter_get_value(const struct device *dev, uint32_t *ticks)
{
    const struct corstone_syscounter_config * const cfg = dev->config;
    uint32_t hi1, hi2, lo;

    do {
        hi1 = cfg->base->cntcv_high;
        lo  = cfg->base->cntcv_low;
        hi2 = cfg->base->cntcv_high;
    } while (hi1 != hi2);

    // ignoring the hi value.
    *ticks = lo;

    return 0;
}

static const struct counter_driver_api corstone_syscounter_api = {
    .start = NULL,            /* Always running */
    .stop = NULL,             /* Always running */
    .get_value = corstone_syscounter_get_value,
};

#define SYSTEM_COUNTER_CORSTONE_INIT(inst)						\
    static const struct corstone_syscounter_config corstone_syscounter_cfg_##inst = {                  \
        .base = ((volatile struct corstone_syscounter_regs *)DT_INST_REG_ADDR(inst)),                  \
        .freq = DT_INST_PROP_BY_PHANDLE(inst, clocks, clock_frequency),     \
    };                                                                      \
    DEVICE_DT_INST_DEFINE(inst,                                             \
                          corstone_syscounter_init,                         \
                          NULL,                                             \
                          NULL,                                             \
                          &corstone_syscounter_cfg_##inst,                  \
                          POST_KERNEL,                                      \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE,               \
                          &corstone_syscounter_api);

DT_INST_FOREACH_STATUS_OKAY(SYSTEM_COUNTER_CORSTONE_INIT);