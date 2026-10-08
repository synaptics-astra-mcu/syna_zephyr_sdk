/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
#ifndef CON_BOOT_TRACE_H
#define CON_BOOT_TRACE_H

#include <stdio.h>

#if defined(CON_BOOT_TRACE_LOG)
	#define CON_BOOT_TRACE(fmt, ...) printf("CON_BOOT: " fmt "\n", ##__VA_ARGS__)
#else
	#define CON_BOOT_TRACE(...) do { } while (0)
#endif

#endif /* CON_BOOT_TRACE_H */
