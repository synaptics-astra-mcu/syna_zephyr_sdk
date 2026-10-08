/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#ifndef SUPPLICANT_MHD_TRANSPORT_ZEPHYR_H
#define SUPPLICANT_MHD_TRANSPORT_ZEPHYR_H

#ifdef ZEPHYR_OS

#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

#include "syna_wifi.h"

void sup_adap_transport_bind_zephyr_drv(driver_syna_data_t *drv);
void sup_adap_transport_unbind_zephyr_drv(driver_syna_data_t *drv);
bool sup_adap_transport_handle_zephyr_scan_event(void *data, uint32_t status);
int syna_adapt_zephyr_scan(struct net_if *iface, struct wifi_scan_params *scan,
			   scan_result_cb_t cb);

#endif

#endif