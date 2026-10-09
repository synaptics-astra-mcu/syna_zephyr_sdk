/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
#include "con_boot_time.h"
#include <cmsis_os2.h>

uint32_t con_boot_time_ms(void)
{
	uint32_t frequency = osKernelGetTickFreq();

	return frequency == 0U ? 0U :
		(uint32_t)(((uint64_t)osKernelGetTickCount() * 1000U) / frequency);
}

void con_boot_delay_ms(uint32_t delay_ms)
{
	osDelay(delay_ms);
}
