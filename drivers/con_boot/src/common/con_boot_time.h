/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
#ifndef CON_BOOT_TIME_H
#define CON_BOOT_TIME_H

#include <stdint.h>

uint32_t con_boot_time_ms(void);
void con_boot_delay_ms(uint32_t delay_ms);

#endif /* CON_BOOT_TIME_H */
