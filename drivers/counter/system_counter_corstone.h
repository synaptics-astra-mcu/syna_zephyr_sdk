
/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SYSTEM_COUNTER_CORSTONE_H_
#define ZEPHYR_DRIVERS_SYSTEM_COUNTER_CORSTONE_H_

#include <zephyr/drivers/counter.h>

#ifdef __cplusplus
extern "C" {
#endif

struct corstone_syscounter_regs {
	/* Offset: 0x000 (R/W) control register */
	volatile uint32_t cntcr;
	/* Offset: 0x004 (R/ ) status register */
	volatile uint32_t cntsr;
	/* Offset: 0x008 (R/W) count value low register */
	volatile uint32_t cntcv_low;
    /* Offset: 0x00C (R/W) count value high register */
	volatile uint32_t cntcv_high;
    /* Offset: 0x010 (R/W) scale register */
	volatile uint32_t cntscr;
    volatile uint32_t reserved0[2]; /* 0x014-0x18 */
    /* Offset: 0x01C (R/ ) id register */
	volatile uint32_t cntid;
    volatile uint32_t reserved1[44];    /* 0x020–0x0CC */
    /* Offset: 0x0D0 (R/W) scale register 0 */
    volatile uint32_t cntsr0;
    /* Offset: 0x0D4 (R/W) scale register 1 */
    volatile uint32_t cntsr1;
};

#define SYSCOUNT_CNTCR_ENABLE           (1 << 0)
#define SYSCOUNT_CNTCR_INTRCLR          (1 << 3)
#define SYSCOUNT_CNTCR_SCEN             (1 << 2)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SYSTEM_COUNTER_CORSTONE_H_ */
