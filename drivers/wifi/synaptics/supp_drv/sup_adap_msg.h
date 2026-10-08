/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#ifndef SUP_ADAP_MSG_H
#define SUP_ADAP_MSG_H

#include "syna_wifi_msg.h"

/* MHD Flag masks list */
#define SUP_ADAP_SUPP_MASK (1U << 0)
#define SUP_ADAP_EVT2_MASK (1U << 1)

typedef enum sup_adap_message_type {
    SUP_ADAP_CMD_REQ_WIPHY,    // struct wpa_driver_capa
    
    SUP_ADAP_CMD_DEL_KEY,      // Key Deletion
    SUP_ADAP_CMD_NEW_KEY,      // New Key installation

    SUP_ADAP_CMD_FLUSH_PMKSA,  // No data

    SUP_ADAP_CMD_SET_STATION,  // Set attribute
    SUP_ADAP_CMD_SCAN_ABORT,   // Abort an ongoing scan
    SUP_ADAP_CMD_ASSOC_REQ,    // Assoc wpa_driver_nl80211_associate
    SUP_ADAP_DATA_EAPOL_PKT,   // EAPOL packet
    SUP_ADAP_CMD_DEAUTHENTICATE, // Deauth
    /* Event indications flowing from MHD adaptor ISR/handler layer into the
     * adaptor task (shim) before being mapped into driver_syna events.
     */
    SUP_ADAP_EVT_SET_SSID,     // Indication of successful set SSID / assoc progression
    SUP_ADAP_EVT_DISASSOC,     // Indication of disassociation (link lost)
    SUP_ADAP_EVT_DEAUTH,       // Indication of deauthentication (common for AP and STA modes)
    SUP_ADAP_EVT_JOIN_START,   // Indication to trigger SAE exteranl auth
    SUP_ADAP_EVT_ASSOC_IND,    // SoftAP: External STA association indication
    SUP_ADAP_CMD_SET_AP,       // Set up AP mode
    SUP_ADAP_CMD_SET_STA_FLAG, // Set STA Flags
    SUP_ADAP_CMD_SET_SUPP_PORT, // Set Supplicant port status

} sup_adap_message_type_t;

typedef struct sup_adap_msg {
	sup_adap_message_type_t type;
	void *msg;
	uint32_t len;
	void *cb_ctx;
	void *drv_priv;
} sup_adap_msg_t;

#ifndef ZEPHYR_OS
void *sup_adap_send_and_recv(sup_adap_msg_t *req,
                     drv_syna_message_type_t ret_msg_type);
#endif
int sup_adap_send_supp_msg(void *msg);
/* Forward declaration so driver layer can enqueue messages without
 * including adaptor C file directly.
 * Returns 0 on success, -1 on failure; same caller-cleanup contract
 * as sup_adap_send_supp_msg().
 */
int send_msg_to_sup_adap(sup_adap_msg_t *msg);

#endif /* MHD_MSG_H_ */