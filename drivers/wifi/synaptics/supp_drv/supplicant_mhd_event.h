/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#ifndef SUPPLICANT_MHD_EVENT_H
#define SUPPLICANT_MHD_EVENT_H

#include "syna_wifi_msg.h"
#include "mhd_wifi_api.h"

extern struct dl_list drv_scan_results_list;
extern struct drv_assoc_info assoc_info;

void mhd_wifi_common_events_handler(const mhd_event_header_t *event_header,
                                    const uint8_t *event_data,
                                    void *user_data);

#endif