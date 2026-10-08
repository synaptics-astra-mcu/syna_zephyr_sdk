/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#ifndef SUPP_DRV_PLATFORM_HEADERS_H_
#define SUPP_DRV_PLATFORM_HEADERS_H_

#include "cmsis_os2_port.h"

#ifdef ZEPHYR_OS
#include "supplicant_utils_zephyr.h"
#else
#include <supplicant_utils.h>
#include "eloop_cmsis.h"
#include "l2_packet_cmsis.h"
#endif

#endif