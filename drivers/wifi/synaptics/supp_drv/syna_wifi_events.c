/**
 * Copyright (c) 2026 Synaptics Incorporated
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file syna_wifi_events.c
 * Events handling and processing.
 */

#include "includes.h"

#include "syna_wifi.h"
#include "syna_wifi_msg.h"
#include "supp_drv_platform_headers.h"
#include "common/ieee802_11_common.h"

__attribute__((weak)) void start_eapol(void) { /* no-op stub for QEMU build */ }

#ifndef SYNA_DRV_LOG
#define SYNA_DRV_LOG(level, fmt, ...) \
    wpa_printf(level, "DRV_SYNA [EVT]: " fmt, ##__VA_ARGS__)
#endif

int32_t handle_event_scan_result(struct driver_syna_data *drv, drv_syna_msg_t *drv_msg)
{
    SYNA_DRV_LOG(MSG_DEBUG, "scan_result evt entry");
    drv_scan_result_t *scan_res = drv_msg->data;
    struct scan_info *info;
    union wpa_event_data event;
    int freqs[16];

    if (!scan_res) {
        SYNA_DRV_LOG(MSG_ERROR, "SCAN null scan_res pointer");
        return -1;
    }
    SYNA_DRV_LOG(MSG_DEBUG, "SCAN msg completed=%u freq=%d ssid_len=%u ie_len=%zu",\
                 scan_res->scan_completed, scan_res->freq,\
                 (unsigned)(scan_res->ssids.ssid_len), scan_res->ie_len);

    /* Terminal marker: notify supplicant that scan results are ready.
     * Supplicant will call drv_syna_get_scan_results() to retrieve the list.
     */
    if (scan_res->scan_completed) {
        unsigned count = 0; drv_scan_result_t *c;
        dl_list_for_each(c, &drv->scan_result_list, drv_scan_result_t, list) count++;
        SYNA_DRV_LOG(MSG_INFO, "SCAN terminal -> EVENT_SCAN_RESULTS accumulated=%u", count);
    #ifdef ZEPHYR_OS
        if (drv->cbs.scan_done)
            drv->cbs.scan_done(drv->ctx, NULL);
    #else
        wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS, NULL);
    #endif
        if (scan_res->ssids.ssid)
            os_free((void *)scan_res->ssids.ssid);
        os_free(scan_res);
        return 0;
    }

    /* Partial result: enqueue into driver's result list */
    /* Create a minimal scan_info to notify supplicant about this AP.
     * We only populate SSID and frequency when available. Full results
     * are retrieved by supplicant via drv_syna_get_scan_results().
     */
    os_memset(&event, 0, sizeof(event));

    info = &event.scan_info;

    /* Populate frequency if present in the partial result (freq == 0 means unknown) */
    if (scan_res->freq) {
        /* single-entry freqs array on stack is fine for minimal notification */
        freqs[0] = scan_res->freq;
        info->freqs = freqs;
        info->num_freqs = 1;
    }

    /* Populate SSID if present */
    if (scan_res->ssids.ssid && scan_res->ssids.ssid_len > 0) {
        /* Copy into the first ssid slot (wpa_driver_scan_ssid expects pointer/len)
         * Note: we don't allocate here; we reference the buffer owned by scan_res
         * because supplicant will only look at this transient notification. The
         * authoritative copy remains in drv->scan_result_list.
         */
        info->ssids[0].ssid = scan_res->ssids.ssid;
        info->ssids[0].ssid_len = scan_res->ssids.ssid_len;
        info->num_ssids = 1;
    }

    /* Notify supplicant of this partial scan result (minimal). Full list
     * will be fetched on EVENT_SCAN_RESULTS terminal event.
     */
    /* Enqueue the result into driver's aggregated list for later retrieval */
    dl_list_add(&drv->scan_result_list, &scan_res->list);
    /* Count current list size for debug */
    {
        unsigned count = 0; drv_scan_result_t *c;
        dl_list_for_each(c, &drv->scan_result_list, drv_scan_result_t, list) count++;
        SYNA_DRV_LOG(MSG_DEBUG, "SCAN enqueued partial list_size=%u", count);
    }
    return 0;
}

extern void start_eapol(void);
int32_t handle_event_assoc(struct driver_syna_data *drv, drv_syna_msg_t *drv_msg)
{
     SYNA_DRV_LOG(MSG_DEBUG, "ASSOC evt entry");
    union wpa_event_data event;
    struct drv_assoc_info *drv_assoc_info = NULL;
    struct assoc_info *assoc_info = NULL;

    if (!drv || !drv_msg) {
         SYNA_DRV_LOG(MSG_ERROR, "ASSOC invalid drv or msg");
       return -1;
    }
     SYNA_DRV_LOG(MSG_INFO, "ASSOC received DRV_SYNA_EVENT_ASSOC");
    os_memset(&event, 0, sizeof(event));

    /* Update the drv fields immediately after receiving
     * assoc event from driver
     */
    drv->connect_reassoc = 0;
    drv->associated = 1;

    drv_assoc_info = drv_msg->data;
    assoc_info     = &event.assoc_info;

    assoc_info->reassoc = drv_assoc_info->reassoc;

    if (drv_assoc_info->addr) {
            /* For AP mode: addr is the STA MAC, copy to assoc_info for hostapd */
            assoc_info->addr = drv_assoc_info->addr;
            /* For STA mode: addr would be the AP BSSID */
            os_memcpy(drv->bssid, drv_assoc_info->addr, ETH_ALEN);
            os_memcpy(drv->prev_bssid, drv->bssid, ETH_ALEN);
    }

    if (drv_assoc_info->req_ies) {
            assoc_info->req_ies_len = drv_assoc_info->req_ies_len;
            assoc_info->req_ies = drv_assoc_info->req_ies;
    }

    if (drv_assoc_info->resp_ies) {
            assoc_info->resp_ies_len = drv_assoc_info->resp_ies_len;
            assoc_info->resp_ies = drv_assoc_info->resp_ies;
    }

    assoc_info->freq = drv_assoc_info->freq;

#ifdef ZEPHYR_OS
    if (drv->cbs.assoc_resp)
        drv->cbs.assoc_resp(drv->ctx, &event, WLAN_STATUS_SUCCESS);
#else
    wpa_supplicant_event(drv->ctx, EVENT_ASSOC, &event);
#endif

    os_free((void *)drv_assoc_info->addr);
    os_free((void *)drv_assoc_info->req_ies);
    os_free((void *)drv_assoc_info->resp_ies);
    os_free(drv_assoc_info);

    start_eapol();
    
    return 0;
}

#define MAC2STRDBG(ea)  ((const uint8*)(ea))[0], \
                        ((const uint8*)(ea))[1], \
                        ((const uint8*)(ea))[2], \
                        ((const uint8*)(ea))[3], \
                        ((const uint8*)(ea))[4], \
                        ((const uint8*)(ea))[5]
                        
int32_t handle_event_external_auth(struct driver_syna_data *drv, drv_syna_msg_t *drv_msg)
{
    SYNA_DRV_LOG(MSG_DEBUG, "JOIN_START evt entry");

    union wpa_event_data event;
    struct drv_external_auth *drv_external_auth = NULL;
    struct external_auth *external_auth = NULL;

    if (!drv || !drv_msg) {
         SYNA_DRV_LOG(MSG_ERROR, "JOIN_START invalid drv or msg");
       return -1;
    }
    SYNA_DRV_LOG(MSG_INFO, "JOIN_START received DRV_SYNA_EVENT_JOIN_START");
    os_memset(&event, 0, sizeof(event));

    /* Update the drv fields immediately after receiving
     * external_auth event from driver
     */
    drv_external_auth = drv_msg->data;
    external_auth     = &event.external_auth;
    switch (drv_external_auth->action) {
    case DRV_EXT_AUTH_START:
        external_auth->action = EXT_AUTH_START;
        break;
    case DRV_EXT_AUTH_ABORT:
        external_auth->action = EXT_AUTH_ABORT;
        break;
    default:
        return -1;
    }

    external_auth->key_mgmt_suite = drv_external_auth->key_mgmt_suite;
    external_auth->ssid_len = drv_external_auth->ssid_len;
    if (drv_external_auth->ssid)
        external_auth->ssid = drv_external_auth->ssid;
    if (drv_external_auth->bssid)
        external_auth->bssid = drv_external_auth->bssid;
    if (drv_external_auth->mld_addr)
        external_auth->mld_addr = drv_external_auth->mld_addr; /* MLO SAE: AP MLD address */

    SYNA_DRV_LOG(MSG_INFO, "External auth action: %u, AKM: 0x%x",
            event.external_auth.action,
            event.external_auth.key_mgmt_suite);
#ifdef ZEPHYR_OS
    if (drv->cbs.external_auth)
        drv->cbs.external_auth(drv->ctx, &event);
#else
    wpa_supplicant_event(drv->ctx, EVENT_EXTERNAL_AUTH, &event);
#endif

    os_free((void *)drv_external_auth->ssid);
    os_free((void *)drv_external_auth->bssid);
    os_free((void *)drv_external_auth->mld_addr);
    os_free(drv_external_auth);

    return 0;
}

int32_t handle_mlme_event_mgmt(struct driver_syna_data *drv, drv_syna_msg_t *drv_msg)
{
    SYNA_DRV_LOG(MSG_DEBUG, "AUTH evt entry");

    union wpa_event_data event;
    struct drv_rx_mgmt *drv_rx_mgmt = NULL;
    struct rx_mgmt *rx_mgmt = NULL;
    const struct ieee80211_mgmt *mgmt;
    u16 fc, stype;
    int rx_freq = 0, ssi_signal = 0;

    if (!drv || !drv_msg) {
         SYNA_DRV_LOG(MSG_ERROR, "AUTH invalid drv or msg");
       return -1;
    }
    SYNA_DRV_LOG(MSG_INFO, "AUTH received DRV_SYNA_EVENT_AUTH");
    os_memset(&event, 0, sizeof(event));

    drv_rx_mgmt = drv_msg->data;
    rx_mgmt     = &event.rx_mgmt;

    mgmt = (const struct ieee80211_mgmt *) drv_rx_mgmt->frame;
    if (drv_rx_mgmt->frame_len < 24) {
        SYNA_DRV_LOG(MSG_ERROR, "Too short management frame");
        return -1;
    }
    fc = le_to_host16(mgmt->frame_control);
    stype = WLAN_FC_GET_STYPE(fc);
    ssi_signal = drv_rx_mgmt->ssi_signal;
    if (drv_rx_mgmt->freq) {
        event.rx_mgmt.freq = drv_rx_mgmt->freq;
        rx_freq = drv->last_mgmt_freq = event.rx_mgmt.freq;
    }
    SYNA_DRV_LOG(MSG_INFO,
            "RX frame da=" MACSTR " sa=" MACSTR " bssid=" MACSTR
            " freq=%d ssi_signal=%d fc=0x%x seq_ctrl=0x%x stype=%u (%s) len=%u",
            MAC2STR(mgmt->da), MAC2STR(mgmt->sa), MAC2STR(mgmt->bssid),
            rx_freq, ssi_signal, fc,
            le_to_host16(mgmt->seq_ctrl), stype, fc2str(fc),
            (unsigned int) drv_rx_mgmt->frame_len);

    event.rx_mgmt.frame = drv_rx_mgmt->frame;
    event.rx_mgmt.frame_len = drv_rx_mgmt->frame_len;
    event.rx_mgmt.ssi_signal = drv_rx_mgmt->ssi_signal;

#ifdef ZEPHYR_OS
    if (drv->cbs.mgmt_rx)
        drv->cbs.mgmt_rx(drv->ctx, (char *)drv_rx_mgmt->frame,
			  drv_rx_mgmt->frame_len, rx_freq, ssi_signal);
#else
    wpa_supplicant_event(drv->ctx, EVENT_RX_MGMT, &event);
#endif

    os_free((void *)drv_rx_mgmt->frame);
    os_free(drv_rx_mgmt);

    return 0;
}

int32_t handle_event_ch_switch(struct driver_syna_data *drv,
                drv_syna_msg_t *drv_msg)
{
    union wpa_event_data event;
    drv_ch_switch_info_t *csa_info = NULL;

    if (!drv || !drv_msg) {
        SYNA_DRV_LOG(MSG_ERROR, "handle_event_ch_switch: invalid drv or msg");
        return -1;
    }

    os_memset(&event, 0, sizeof(event));
    csa_info = drv_msg->data;

    if (csa_info) {
        event.ch_switch.freq         = csa_info->freq;
        event.ch_switch.ht_enabled   = csa_info->ht_enabled;
        event.ch_switch.ch_width     = csa_info->ch_width;
        event.ch_switch.cf1          = csa_info->cf1;
        event.ch_switch.cf2          = csa_info->cf2;
        event.ch_switch.ch_offset    = csa_info->ch_offset;
        event.ch_switch.link_id      = csa_info->link_id;
        event.ch_switch.punct_bitmap = csa_info->punct_bitmap;

        SYNA_DRV_LOG(MSG_DEBUG, "handle_event_ch_switch: freq=%d ht=%d width=%d link_id=%d punct=0x%x",
                     csa_info->freq, csa_info->ht_enabled, csa_info->ch_width,
                     csa_info->link_id, csa_info->punct_bitmap);
    }

#ifdef ZEPHYR_OS
    if (drv->cbs.ecsa_complete)
        drv->cbs.ecsa_complete(drv->ctx, &event);
#else
    wpa_supplicant_event(drv->ctx, EVENT_CH_SWITCH, &event);
#endif

    os_free(csa_info);
    return 0;
}

int32_t handle_event_disassoc(struct driver_syna_data *drv,
                drv_syna_msg_t *drv_msg)
{
    SYNA_DRV_LOG(MSG_DEBUG, "DISASSOC evt entry");
    union wpa_event_data event;
    drv_disassoc_info_t *drv_disassoc_info = NULL;
    struct disassoc_info *disassoc_info = NULL;

    if (!drv || !drv_msg) {
        SYNA_DRV_LOG(MSG_ERROR, "DISASSOC invalid drv or msg");
        return -1;
    }

    os_memset(&event, 0, sizeof(event));
    drv_disassoc_info = drv_msg->data;
    disassoc_info = &event.disassoc_info;

    if (drv_disassoc_info->addr) {
        u8 *addr_buf = os_zalloc(ETH_ALEN);
        if (addr_buf)
            os_memcpy(addr_buf, drv_disassoc_info->addr, ETH_ALEN);
        disassoc_info->addr = addr_buf;
    }

    if (drv_disassoc_info->ie) {
        disassoc_info->ie_len = drv_disassoc_info->ie_len;
        u8 *ie_buf = os_zalloc(disassoc_info->ie_len);
        if (ie_buf)
            os_memcpy(ie_buf, drv_disassoc_info->ie, disassoc_info->ie_len);
        disassoc_info->ie = ie_buf;
    }

    disassoc_info->reason_code = drv_disassoc_info->reason_code;
    disassoc_info->locally_generated = drv_disassoc_info->locally_generated;

    drv->associated = 0;

#ifdef ZEPHYR_OS
    if (drv->cbs.disassoc)
        drv->cbs.disassoc(drv->ctx, &event);
#else
    wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, &event);
#endif

    os_free((void *)drv_disassoc_info->addr);
    os_free((void *)drv_disassoc_info->ie);
    os_free(drv_disassoc_info);

    return 0;
}

int32_t handle_event_deauth(struct driver_syna_data *drv,
                drv_syna_msg_t *drv_msg)
{
    SYNA_DRV_LOG(MSG_DEBUG, "DEAUTH evt entry");
    union wpa_event_data event;
    drv_deauth_info_t *drv_deauth_info = NULL;
    struct deauth_info *deauth_info = NULL;

    if (!drv || !drv_msg) {
        SYNA_DRV_LOG(MSG_ERROR, "DEAUTH invalid drv or msg");
            return -1;
    }

    if (drv->ignore_next_local_deauth) {
        SYNA_DRV_LOG(MSG_INFO, "DEAUTH ignoring (local deauth triggered)");
            drv->ignore_next_local_deauth = 0;
            return 0;
    }

    os_memset(&event, 0, sizeof(event));
    drv_deauth_info = drv_msg->data;
    deauth_info = &event.deauth_info;

    if (drv_deauth_info->addr) {
        u8 *addr_buf = os_zalloc(ETH_ALEN);
        if (addr_buf)
            os_memcpy(addr_buf, drv_deauth_info->addr, ETH_ALEN);
        deauth_info->addr = addr_buf; /* assign (may be NULL) */
    }

    if (drv_deauth_info->ie) {
        deauth_info->ie_len = drv_deauth_info->ie_len;
        u8 *ie_buf = os_zalloc(deauth_info->ie_len);
        if (ie_buf)
            os_memcpy(ie_buf, drv_deauth_info->ie, deauth_info->ie_len);
        deauth_info->ie = ie_buf;
    }

    deauth_info->reason_code = drv_deauth_info->reason_code;
    deauth_info->locally_generated = drv_deauth_info->locally_generated;

#ifdef ZEPHYR_OS
    if (drv->cbs.deauth)
        drv->cbs.deauth(drv->ctx, &event, NULL);
#else
    wpa_supplicant_event(drv->ctx, EVENT_DEAUTH, &event);
#endif

    os_free((void *)drv_deauth_info->addr);
    os_free((void *)drv_deauth_info->ie);
    os_free(drv_deauth_info);

    return 0;
}

int drv_syna_process_msg(struct driver_syna_data *drv, drv_syna_msg_t *drv_msg)
{
    switch (drv_msg->type) {
    case DRV_SYNA_EVENT_SCAN_RESULT:
        return handle_event_scan_result(drv, drv_msg);
    case DRV_SYNA_EVENT_ASSOC:
        return handle_event_assoc(drv, drv_msg);
    case DRV_SYNA_EVENT_DISASSOC:
        return handle_event_disassoc(drv, drv_msg);
    case DRV_SYNA_EVENT_DEAUTH:
        return handle_event_deauth(drv, drv_msg);
    case DRV_SYNA_EVENT_JOIN_START:
        return handle_event_external_auth(drv, drv_msg);
    case DRV_SYNA_EVENT_AUTH:
        return handle_mlme_event_mgmt(drv, drv_msg);
    case DRV_SYNA_EVENT_CH_SWITCH:
        return handle_event_ch_switch(drv, drv_msg);
    default:
        return -1;
    }
}

#ifndef ZEPHYR_OS
void drv_sup_adap_message_receiver(void *eloop_ctx, void *user_ctx)
{
    struct driver_syna_data *drv = eloop_ctx;
    drv_syna_msg_t drv_msg;
    osStatus_t status;

    SYNA_DRV_LOG(MSG_INFO, "RX thread loop start");

   while (1) {
        status = osMessageQueueGet(supp_queue, &drv_msg, NULL, 0);
        if (status == osOK)
            SYNA_DRV_LOG(MSG_DEBUG, "RX message type=%d", drv_msg.type);
        else
            break;

        switch(drv_msg.type)
        {
        case DRV_SYNA_EVENT_SCAN_RESULT:
            {
                handle_event_scan_result(drv, &drv_msg);
                break;
            }
        case DRV_SYNA_EVENT_ASSOC:
            {
                handle_event_assoc(drv, &drv_msg);
                break;
            }
        case DRV_SYNA_EVENT_DISASSOC:
            {
                handle_event_disassoc(drv, &drv_msg);
                break;
            }
        case DRV_SYNA_EVENT_DEAUTH:
            {
                handle_event_deauth(drv, &drv_msg);
                break;
            }
        case DRV_SYNA_EVENT_JOIN_START:
            {
                handle_event_external_auth(drv, &drv_msg);
                break;
            }
        case DRV_SYNA_EVENT_AUTH:
            {
                handle_mlme_event_mgmt(drv, &drv_msg);
                break;
            }
        case DRV_SYNA_EVENT_CH_SWITCH:
            {
                handle_event_ch_switch(drv, &drv_msg);
                break;
            }

        default:
            SYNA_DRV_LOG(MSG_DEBUG, "RX unhandled type=%d", drv_msg.type);
            break;
        }
   }

   return;
}
#endif