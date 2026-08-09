/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_WDT_CMSDK_SYNA_H_
#define ZEPHYR_DRIVERS_WDT_CMSDK_SYNA_H_

#include <zephyr/drivers/counter.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wdog_cmsdk {
	/* offset: 0x000 (r/w) watchdog load register */
	volatile uint32_t  load;
	/* offset: 0x004 (r/ ) watchdog value register */
	volatile uint32_t  value;
	/* offset: 0x008 (r/w) watchdog control register */
	volatile uint32_t  ctrl;
	/* offset: 0x00c ( /w) watchdog clear interrupt register */
	volatile uint32_t  intclr;
	/* offset: 0x010 (r/ ) watchdog raw interrupt status register */
	volatile uint32_t  rawintstat;
	/* offset: 0x014 (r/ ) watchdog interrupt status register */
	volatile uint32_t  maskintstat;
	volatile uint32_t  reserved0[762];
	/* offset: 0xc00 (r/w) watchdog lock register */
	volatile uint32_t  lock;
	volatile uint32_t  reserved1[191];
	/* offset: 0xf00 (r/w) watchdog integration test control register */
	volatile uint32_t  itcr;
	/* offset: 0xf04 ( /w) watchdog integration test output set register */
	volatile uint32_t  itop;
};

#define CMSDK_WDOG_LOAD		(0xFFFFFFFF << 0)
#define CMSDK_WDOG_RELOAD		(0xE4E1C00 << 0)
#define CMSDK_WDOG_VALUE		(0xFFFFFFFF << 0)
#define CMSDK_WDOG_CTRL_RESEN	(0x1 << 1)
#define CMSDK_WDOG_CTRL_INTEN	(0x1 << 0)
#define CMSDK_WDOG_INTCLR		(0x1 << 0)
#define CMSDK_WDOG_RAWINTSTAT	(0x1 << 0)
#define CMSDK_WDOG_MASKINTSTAT	(0x1 << 0)
#define CMSDK_WDOG_LOCK		(0x1 << 0)
#define CMSDK_WDOG_INTEGTESTEN	(0x1 << 0)
#define CMSDK_WDOG_INTEGTESTOUTSET	(0x1 << 1)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_WDT_CMSDK_SYNA_H_ */
