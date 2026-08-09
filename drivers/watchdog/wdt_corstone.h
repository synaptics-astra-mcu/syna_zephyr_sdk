/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_WDT_CORSTONE_H_
#define ZEPHYR_DRIVERS_WDT_CORSTONE_H_

#include <zephyr/drivers/counter.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wdog_corstone_ctrl {
	/* Offset: 0x000 (R/W) control and status register */
	volatile uint32_t wcs;
	volatile uint32_t reserved0;	/* 0x004 */
	/* Offset: 0x008 (R/W) offset register */
	volatile uint32_t wor;
	volatile uint32_t reserved1;	/* 0x00C */
	/* Offset: 0x010 (R/W) compare value low register */
	volatile uint32_t wcv_low;
	/* Offset: 0x014 (R/W) compare value high register */
	volatile uint32_t wcv_high;
};

struct wdog_corstone_ref {
	/* Offset: 0x000 (R/W) refresh register */
	volatile uint32_t wrr;
};

#define WDT_WCS_ENABLE				(1 << 0)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_WDT_CORSTONE_H_ */
