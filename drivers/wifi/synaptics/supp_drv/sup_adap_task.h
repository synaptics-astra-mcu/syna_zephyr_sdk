/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#ifndef SUP_ADAP_TASK_H
#define SUP_ADAP_TASK_H

#include "sup_adap_msg.h"
#include "mhd_wifi_api.h"
#if 0
#include "host_rtos.h"
#include "mhd_debug.h"

/*Don't change the order, it should be same as supp_cmd_handlers[] in mhd_mapper.c*/

typedef enum {
    SUPP_CMD_TRIGGER_SCAN = 0,
    SUPP_CMD_ABORT_SCAN,
    SUPP_CMD_JOIN_IBSS,
    SUPP_CMD_LEAVE_IBSS,
    SUPP_CMD_CONNECT,
    SUPP_CMD_DISCONNECT,
    SUPP_CMD_GET_STATE_REQ,
    SUPP_CMD_GET_VERSION_REQ,
    SUPP_CMD_SBREG_REQ,
    SUPP_CMD_MEMBYTES_REQ,
    SUPP_CMD_INTCONFIG_REQ,
    SUPP_CMD_INTBUS_REQ,
    SUPP_CMD_MAX,
} supp_cmd_t;


/* MHD Flag masks list */
#define SUP_ADAP_SUPP_MASK (1U << 0)
#define SUP_ADAP_EVT2_MASK (1U << 1)

/*

typedef struct mhd_driver_context_s
{
    host_event_type_t mhd_evt_id;
    host_queue_type_t mhd_queue;
} mhd_driver_context_t;

typedef enum mhd_message_type {
    MHD_CMD_GET_WIPHY, // struct wpa_driver_capa
    MHD_CMD_SET_KEY, //struct wpa_driver_set_key_params
    MHD_CMD_FLUSH_PMKSA, // No data
    MHD_CMD_SET_STATION, // Set attribute
    MHD_CMD_TRIGGER_SCAN, // Scan struct wpa_driver_scan_params

} mhd_message_type_t;
*/

typedef struct sup_adap_msg
{
    supp_cmd_t type;
    void *msg;
} sup_adap_msg_t;

#endif

int send_msg_to_sup_adap(sup_adap_msg_t *msg);
int sup_adap_register_mhd_event_handler(mhd_interface_t interface);
void init_sup_adap_task(void);
void deinit_sup_adap_task(void);

#endif
