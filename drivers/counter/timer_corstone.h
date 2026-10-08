/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_COUNTER_TIMER_CORSTONE_H_
#define ZEPHYR_DRIVERS_COUNTER_TIMER_CORSTONE_H_

#include <zephyr/drivers/counter.h>

#ifdef __cplusplus
extern "C" {
#endif

struct timer_corstone {
	/* Offset: 0x000 (R/ ) physical count low register */
	volatile uint32_t pct_low;
	/* Offset: 0x004 (R/ ) physical count high register */
	volatile uint32_t pct_high;
	volatile uint32_t reserved0[2];	/* 0x008-0x00C */
	/* Offset: 0x010 (R/W) counter freq register */
	volatile uint32_t cntfrq;
	volatile uint32_t reserved1[3];	/* 0x014-0x01C */
	/* Offset: 0x020 (R/W) compare value low register */
	volatile uint32_t cval_low;
	/* Offset: 0x024 (R/W) compare value high register */
	volatile uint32_t cval_high;
	/* Offset: 0x028 (R/W) timer value register */
	volatile uint32_t tval;
	/* Offset: 0x02C (R/W) control register */
	volatile uint32_t ctl;
	volatile uint32_t reserved2[4];	/* 0x030-0x03C */
	/* Offset: 0x040 (R/ ) auto increment value low register */
	volatile uint32_t aival_low;
	/* Offset: 0x044 (R/ ) auto increment value high register */
	volatile uint32_t aival_high;
	/* Offset: 0x048 (R/W) auto increment reload register */
	volatile uint32_t aival_reload;
	/* Offset: 0x04C (R/W) auto increment control register */
	volatile uint32_t aival_ctl;
	/* Offset: 0x050 (R/ ) configuration register */
	volatile uint32_t cfg;
};

#define TIMER_SYSTEM_TIMER_ENABLE_IMASK         0x3 // CNTP_CTL.IMASK = 1 & CNTP_CTL.ENABLE = 1
#define TIMER_SYSTEM_TIMER_DISABLE              0x2 // CNTP_CTL.IMASK = 1 & CNTP_CTL.ENABLE = 0
#define TIMER_SYSTEM_TIMER_DISABLE_IMASK        0x1 // CNTP_CTL.IMASK = 0 & CNTP_CTL.ENABLE = 1
#define TIMER_CMSDK_TIMER_CLEAR_TIMERINTEN      0x1 // TimerIntEn = 0 & Enable = 1

#define TIMER_CTL_ENABLE						(1 << 0)
#define TIMER_CTL_IMASK							(1 << 1)
#define TIMER_CTL_ISTATUS						(1 << 2)

#define TIMER_AIVAL_CTL_EN						(1 << 0)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_COUNTER_TIMER_CORSTONE_H_ */
