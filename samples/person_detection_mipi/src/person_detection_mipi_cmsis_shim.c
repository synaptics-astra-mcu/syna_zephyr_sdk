/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <stdint.h>

uint32_t osKernelGetTickCount(void)
{
	return k_uptime_ticks();
}

int32_t osDelay(uint32_t ticks)
{
	k_sleep(K_TICKS(ticks));
	return 0;
}
