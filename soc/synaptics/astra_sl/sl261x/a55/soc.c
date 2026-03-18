/*
 * Copyright (c) 2026 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

int __weak pm_cpu_on(unsigned long cpuid, uintptr_t entry_point)
{
	return 0;
}
