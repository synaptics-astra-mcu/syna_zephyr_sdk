/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#ifndef SUPPLICANT_MHD_TRANSPORT_H
#define SUPPLICANT_MHD_TRANSPORT_H

#include "sup_adap_msg.h"
#include "syna_wifi.h"

struct sup_adap_state {
	int assoc_indicated;
	int assoc_control_stage;
};

int32_t sup_adap_process_msg(sup_adap_msg_t *msg);
struct sup_adap_state *sup_adap_transport_state(void);
const uint32_t *sup_adap_transport_sta_events(size_t *count);
const uint32_t *sup_adap_transport_ap_events(size_t *count);
int sup_adap_register_sta_event_handler(mhd_interface_t interface);
int sup_adap_register_ap_event_handler(void);

#endif
