/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#include "includes.h"
#include "supp_main.h"
#include "sup_adap_msg.h"
#include "syna_wifi.h"
#include "syna_wifi_msg.h"
#include "mhd_wifi_api.h"
#include "supplicant_mhd_transport.h"
#include "supplicant_mhd_event.h"
#include "syna_wifi_sc_filter.h"
#include "common/wpa_common.h"
#include "supp_drv_platform_headers.h"

#ifndef WL_CHANNEL_BAND
#define WL_CHANNEL_BAND(ch) \
    ((uint)(ch) <= CH_MAX_2G_CHANNEL ? MHD_CHANSPEC_BAND_2G : \
     ((uint)(ch) <= CH_MAX_5G_CHANNEL ? MHD_CHANSPEC_BAND_5G : \
                           MHD_CHANSPEC_BAND_6G))
#endif

#define MACDBG "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STRDBG(ea)  ((const uint8*)(ea))[0], \
                        ((const uint8*)(ea))[1], \
                        ((const uint8*)(ea))[2], \
                        ((const uint8*)(ea))[3], \
                        ((const uint8*)(ea))[4], \
                        ((const uint8*)(ea))[5]

static void adap_upsert_scan_result_from_fw_bss(const mhd_wifi_bss_info_update_t *bss_upd)
{
    drv_scan_result_t *scan_res;
    drv_scan_result_t *existing = NULL;
    uint32_t channel = 0;
    mhd_result_t rc;

    if (!bss_upd || is_zero_ether_addr(bss_upd->bssid.octet) || bss_upd->ies_len == 0)
        return;

    scan_res = os_zalloc(sizeof(*scan_res) + bss_upd->ies_len);
    if (!scan_res)
        return;

    scan_res->scan_completed = 0;
    os_memcpy(scan_res->bssid, bss_upd->bssid.octet, ETH_ALEN);
    scan_res->ie_len = bss_upd->ies_len;
    scan_res->beacon_ie_len = 0;
    os_memcpy((uint8_t *)(scan_res + 1), bss_upd->ies, bss_upd->ies_len);
    rc = mhd_wifi_api_get_channel(MHD_STA_INTERFACE, &channel);
    if (rc == MHD_SUCCESS && channel != 0U)
        scan_res->freq = mhd_channel_to_frequency(channel, WL_CHANNEL_BAND(channel));
    else
        scan_res->freq = assoc_info.freq;

    dl_list_for_each(existing, &drv_scan_results_list, drv_scan_result_t, list) {
        if (os_memcmp(existing->bssid, bss_upd->bssid.octet, ETH_ALEN) == 0) {
            dl_list_del(&existing->list);
            sc_filter_free_entry(existing);
            break;
        }
    }

    dl_list_add_tail(&drv_scan_results_list, &scan_res->list);
    wpa_printf(MSG_DEBUG,
               "Adaptor: cached FW bss into scan_results bssid=" MACDBG " ie_len=%u freq=%d",
               MAC2STRDBG(&bss_upd->bssid),
               (unsigned)bss_upd->ies_len,
               scan_res->freq);
}

static void adap_refresh_bss_info_from_fw(const char *event_tag)
{
    uint8_t ie_buf[1536];
    mhd_wifi_bss_info_update_t bss_upd;
    mhd_result_t rc;

    os_memset(&bss_upd, 0, sizeof(bss_upd));
    bss_upd.ies = ie_buf;
    bss_upd.ies_buf_len = sizeof(ie_buf);
    rc = mhd_wifi_api_update_bss_info(MHD_STA_INTERFACE, &bss_upd);
    if (rc != MHD_SUCCESS) {
        wpa_printf(MSG_DEBUG, "Adaptor: %s fw bss update failed rc=%ld",
                   event_tag, (long)rc);
        return;
    }

    wpa_printf(MSG_DEBUG,
               "Adaptor: %s fw bss_info ver=%u ie_len=%u has_rsn=%u bssid=" MACDBG,
               event_tag,
               (unsigned)bss_upd.bss_info_version,
               (unsigned)bss_upd.ie_length,
               (unsigned)bss_upd.has_rsn_ie,
               MAC2STRDBG(&bss_upd.bssid));

    if (!is_zero_ether_addr(bss_upd.bssid.octet)) {
        if (assoc_info.addr)
            os_free((void *)assoc_info.addr);
        assoc_info.addr = os_zalloc(ETH_ALEN);
        if (assoc_info.addr)
            os_memcpy((void *)assoc_info.addr, bss_upd.bssid.octet, ETH_ALEN);
    }
    if (bss_upd.ies_len) {
        uint8_t *copy = os_zalloc(bss_upd.ies_len);
        if (copy) {
            if (assoc_info.beacon_ies)
                os_free((void *)assoc_info.beacon_ies);
            os_memcpy(copy, bss_upd.ies, bss_upd.ies_len);
            assoc_info.beacon_ies = copy;
            assoc_info.beacon_ies_len = bss_upd.ies_len;
        }
    }

    adap_upsert_scan_result_from_fw_bss(&bss_upd);
}

static unsigned int translate_auth_key_mgmt_suite(unsigned int akm_suite)
{
    switch ((mhd_akm_suite_t)akm_suite) {
    case MHD_AKM_SAE_SHA256:
        return RSN_AUTH_KEY_MGMT_SAE;
    case MHD_AKM_FT_SAE_SHA256:
        return RSN_AUTH_KEY_MGMT_FT_SAE;
    case MHD_AKM_SAE_EXT_KEY:
        return RSN_AUTH_KEY_MGMT_SAE_EXT_KEY;
    default:
        return 0;
    }
}

static int wl_frame_get_mgmt(uint16_t fc, mhd_mac_t *da,
			     mhd_mac_t *sa, mhd_mac_t *bssid,
			     uint8_t **pheader, uint32_t *body_len,
			     const uint8_t *pbody)
{
    struct dot11_management_header *hdr;
    uint32_t totlen = 0;
    int32_t err = MHD_SUCCESS;
    uint8_t *offset;
    uint32_t prebody_len = *body_len;

    totlen += DOT11_MGMT_HDR_LEN + prebody_len;
    *pheader = os_zalloc(totlen);
    if (*pheader == NULL) {
        wpa_printf(MSG_DEBUG, "memory alloc failed totlen=%d", totlen);
        return MHD_ERROR;
    }
    hdr = (struct dot11_management_header *)(*pheader);
    hdr->fc = fc;
    hdr->durid = 0;
    hdr->seq = 0;
    offset = (uint8_t *)(hdr + 1) + (totlen - DOT11_MGMT_HDR_LEN - prebody_len);
    memcpy((uint8_t *)&hdr->da, (const char *)da, ETHER_ADDR_LEN);
    memcpy((uint8_t *)&hdr->sa, (const char *)sa, ETHER_ADDR_LEN);
    memcpy((uint8_t *)&hdr->bssid, (const char *)bssid, ETHER_ADDR_LEN);
    if (pbody != NULL && prebody_len)
        memcpy(offset, (const char *)pbody, prebody_len);
    *body_len = totlen;

    return err;
}

static void supplicant_mhd_event_cb(uint32_t event,
                                    const mhd_event_header_t *hdr,
                                    const uint8_t *payload,
                                    uint32_t payload_len,
                                    void *ctx)
{
    struct sup_adap_state *st = (struct sup_adap_state *)ctx;

    if (!st)
        st = sup_adap_transport_state();

    switch (event) {
    case WLC_E_SET_SSID:
    {
        uint16_t status = hdr ? (uint16_t)hdr->status : 0xffff;

        wpa_printf(MSG_DEBUG, "Adaptor: WLC_E_SET_SSID (status=%u)", (unsigned)status);
        if (status == WLC_E_STATUS_SUCCESS) {
            st->assoc_control_stage = 1;
            if (!st->assoc_indicated) {
                sup_adap_msg_t msg;
                os_memset(&msg, 0, sizeof(msg));
                msg.type = SUP_ADAP_EVT_SET_SSID;
                send_msg_to_sup_adap(&msg);
            }
        } else if (status == WLC_E_STATUS_NO_NETWORKS) {
            sup_adap_msg_t msg;
            os_memset(&msg, 0, sizeof(msg));
            msg.type = SUP_ADAP_EVT_DISASSOC;
            send_msg_to_sup_adap(&msg);
        } else if (status != 0xffff) {
            sup_adap_msg_t msg;
            os_memset(&msg, 0, sizeof(msg));
            msg.type = SUP_ADAP_EVT_DISASSOC;
            send_msg_to_sup_adap(&msg);
            wpa_printf(MSG_DEBUG,
                       "Adaptor: WLC_E_SET_SSID non-success status=%u -> DISASSOC",
                       (unsigned)status);
        }
        break;
    }
    case WLC_E_DISASSOC:
    case WLC_E_DISASSOC_IND:
        wpa_printf(MSG_DEBUG,
                   "Adaptor: %s reason=%u addr=%02x:%02x:%02x:%02x:%02x:%02x payload_len=%u",
                   (event == WLC_E_DISASSOC) ? "WLC_E_DISASSOC" : "WLC_E_DISASSOC_IND",
                   hdr ? (u16)hdr->reason : 0,
                   hdr ? hdr->addr.octet[0] : 0, hdr ? hdr->addr.octet[1] : 0,
                   hdr ? hdr->addr.octet[2] : 0, hdr ? hdr->addr.octet[3] : 0,
                   hdr ? hdr->addr.octet[4] : 0, hdr ? hdr->addr.octet[5] : 0,
                   (unsigned)payload_len);
        {
            drv_deauth_info_t *dinfo = os_zalloc(sizeof(*dinfo));
            sup_adap_msg_t msg;

            if (!dinfo) {
                wpa_printf(MSG_WARNING,
                           "Adaptor: alloc drv_deauth_info_t failed (WLC_E_DISASSOC_IND)");
                break;
            }

            dinfo->reason_code = hdr ? (u16)hdr->reason : 0;
            dinfo->locally_generated = 0;

            if (hdr &&
                (hdr->addr.octet[0] || hdr->addr.octet[1] || hdr->addr.octet[2] ||
                 hdr->addr.octet[3] || hdr->addr.octet[4] || hdr->addr.octet[5])) {
                dinfo->addr = os_zalloc(ETH_ALEN);
                if (dinfo->addr)
                    os_memcpy((u8 *)dinfo->addr, hdr->addr.octet, ETH_ALEN);
            }

            if (payload && payload_len) {
                dinfo->ie = os_zalloc(payload_len);
                if (dinfo->ie) {
                    os_memcpy((u8 *)dinfo->ie, payload, payload_len);
                    dinfo->ie_len = payload_len;
                }
            }

            os_memset(&msg, 0, sizeof(msg));
            msg.type = SUP_ADAP_EVT_DISASSOC;
            msg.msg = dinfo;
            msg.len = sizeof(*dinfo);
            if (send_msg_to_sup_adap(&msg) != 0) {
                if (dinfo->ie)
                    os_free((void *)dinfo->ie);
                if (dinfo->addr)
                    os_free((void *)dinfo->addr);
                os_free(dinfo);
            }
        }
        break;
    case WLC_E_ASSOC_IND:
        wpa_printf(MSG_DEBUG,
                   "Adaptor: WLC_E_ASSOC_IND (SoftAP client assoc) addr=%02x:%02x:%02x:%02x:%02x:%02x",
                   hdr ? hdr->addr.octet[0] : 0, hdr ? hdr->addr.octet[1] : 0,
                   hdr ? hdr->addr.octet[2] : 0, hdr ? hdr->addr.octet[3] : 0,
                   hdr ? hdr->addr.octet[4] : 0, hdr ? hdr->addr.octet[5] : 0);
        {
            struct drv_assoc_info *assoc_req = &assoc_info;
            sup_adap_msg_t msg;

            os_memset(&msg, 0, sizeof(msg));
            msg.type = SUP_ADAP_EVT_ASSOC_IND;
            if (payload && payload_len) {
                assoc_req->req_ies = os_zalloc(payload_len);
                if (assoc_req->req_ies) {
                    memcpy((u8 *)assoc_req->req_ies, payload, payload_len);
                    assoc_req->req_ies_len = payload_len;
                } else {
                    assoc_req->req_ies_len = 0;
                    wpa_printf(MSG_ERROR,
                               "Adaptor: WLC_E_ASSOC_IND: Failed to allocate req_ies");
                }
            } else {
                assoc_req->req_ies_len = 0;
            }
            if (hdr) {
                uint8_t *sta_addr = os_zalloc(ETH_ALEN);
                if (sta_addr) {
                    memcpy(sta_addr, hdr->addr.octet, ETH_ALEN);
                    msg.msg = sta_addr;
                    msg.len = ETH_ALEN;
                }
            }
            if (send_msg_to_sup_adap(&msg) != 0 && msg.msg)
                os_free(msg.msg);
        }
        break;
    case WLC_E_AUTH:
        {
            struct drv_assoc_info *assoc = &assoc_info;
            uint16_t status = 0xffff;

            if (!hdr) {
                wpa_printf(MSG_DEBUG, "Adaptor: WLC_E_AUTH: NULL event header, ignoring");
                break;
            }
            status = (uint16_t)hdr->status;
            wpa_printf(MSG_DEBUG, "Adaptor: WLC_E_AUTH(%d) (status=%u) (interface=%d)",
                       event, status, hdr->interface);
            if (hdr->auth_type != WLAN_AUTH_SAE) {
                if (assoc->addr)
                    os_free((void *)assoc->addr);
                assoc->addr = os_zalloc(ETH_ALEN);
                if (assoc->addr)
                    memcpy((void *)assoc->addr, &hdr->addr.octet[0], ETH_ALEN);
            }

            if (hdr->auth_type == WLAN_AUTH_SAE) {
                mhd_result_t rc = MHD_SUCCESS;
                drv_syna_msg_t msg;
                struct drv_rx_mgmt *drv_rx_mgmt = NULL;

                if (!payload_len) {
                    wpa_printf(MSG_DEBUG,
                               "Adaptor: WLC_E_AUTH: event has no payload. status %d reason %d \n",
                               hdr->status, hdr->reason);
                    break;
                }
                if (hdr->interface != MHD_STA_INTERFACE && hdr->interface != MHD_AP_INTERFACE) {
                    wpa_printf(MSG_ERROR, "Adaptor: WLC_E_AUTH: invalid interface %d",
                               hdr->interface);
                    break;
                }

                mhd_mac_t dst_addr;
                rc = mhd_wifi_api_get_mac_address(hdr->interface, &dst_addr);
                if (rc != MHD_SUCCESS) {
                    wpa_printf(MSG_ERROR,
                               "Adaptor: WLC_E_AUTH: mhd_wifi_api_get_mac_address failed rc=%ld",
                               (long)rc);
                    break;
                }

                mhd_mac_t bssid;
                if (hdr->interface == MHD_STA_INTERFACE)
                    memcpy(&bssid, &hdr->addr, ETHER_ADDR_LEN);
                else
                    memcpy(&bssid, &dst_addr, ETHER_ADDR_LEN);

                uint32_t channel;
                rc = mhd_wifi_api_get_channel(hdr->interface, &channel);

                u8 *mgmt_frame = NULL;
                u32 body_len = payload_len;
                rc = wl_frame_get_mgmt(FC_AUTH, &dst_addr, (mhd_mac_t *)&hdr->addr, &bssid,
                                       &mgmt_frame, &body_len, payload);
                if (rc) {
                    if (mgmt_frame)
                        os_free(mgmt_frame);
                    break;
                }

                msg.type = DRV_SYNA_EVENT_AUTH;
                msg.data = os_zalloc(sizeof(struct drv_rx_mgmt));
                drv_rx_mgmt = msg.data;
                if (!drv_rx_mgmt) {
                    wpa_printf(MSG_ERROR,
                               "Adaptor: WLC_E_AUTH: Failed to allocate drv_rx_mgmt");
                    os_free(mgmt_frame);
                    break;
                }
                if (body_len) {
                    drv_rx_mgmt->frame = os_zalloc(body_len);
                    if (drv_rx_mgmt->frame)
                        memcpy(drv_rx_mgmt->frame, mgmt_frame, body_len);
                    else {
                        wpa_printf(MSG_ERROR,
                                   "Adaptor: WLC_E_AUTH: Failed to allocate frame buffer");
                        body_len = 0;
                    }
                }
                drv_rx_mgmt->frame_len = body_len;
                drv_rx_mgmt->datarate = 0;
                drv_rx_mgmt->freq = mhd_channel_to_frequency(channel, WL_CHANNEL_BAND(channel));
                drv_rx_mgmt->ssi_signal = 0;

                os_free(mgmt_frame);

                wpa_printf(MSG_DEBUG, "Adaptor: WLC_E_AUTH(%d) da:" MACDBG " sa:" MACDBG " "
                           "BSSID:" MACDBG " ch %d(freq:%d) len %d",
                           event, MAC2STRDBG(&dst_addr), MAC2STRDBG(&hdr->addr),
                           MAC2STRDBG(&bssid), channel, drv_rx_mgmt->freq,
                           drv_rx_mgmt->frame_len);
                if (sup_adap_send_supp_msg(&msg) != 0) {
                    if (drv_rx_mgmt->frame)
                        os_free(drv_rx_mgmt->frame);
                    os_free(drv_rx_mgmt);
                }
            }
        }
        break;
    case WLC_E_DEAUTH_IND:
        {
            drv_deauth_info_t *dinfo = os_zalloc(sizeof(*dinfo));
            sup_adap_msg_t msg;

            if (!dinfo) {
                wpa_printf(MSG_WARNING,
                           "Adaptor: alloc drv_deauth_info_t failed (WLC_E_DEAUTH_IND)");
                break;
            }

            wpa_printf(MSG_DEBUG,
                       "Adaptor: WLC_E_DEAUTH_IND reason=%u addr=%02x:%02x:%02x:%02x:%02x:%02x",
                       hdr ? (u16)hdr->reason : 0,
                       hdr ? hdr->addr.octet[0] : 0, hdr ? hdr->addr.octet[1] : 0,
                       hdr ? hdr->addr.octet[2] : 0, hdr ? hdr->addr.octet[3] : 0,
                       hdr ? hdr->addr.octet[4] : 0, hdr ? hdr->addr.octet[5] : 0);

            dinfo->reason_code = hdr ? (u16)hdr->reason : 0;
            dinfo->locally_generated = 0;

            if (hdr &&
                (hdr->addr.octet[0] || hdr->addr.octet[1] || hdr->addr.octet[2] ||
                 hdr->addr.octet[3] || hdr->addr.octet[4] || hdr->addr.octet[5])) {
                dinfo->addr = os_zalloc(ETH_ALEN);
                if (dinfo->addr)
                    os_memcpy((u8 *)dinfo->addr, hdr->addr.octet, ETH_ALEN);
            }

            if (payload && payload_len) {
                dinfo->ie = os_zalloc(payload_len);
                if (dinfo->ie) {
                    os_memcpy((u8 *)dinfo->ie, payload, payload_len);
                    dinfo->ie_len = payload_len;
                }
            }

            os_memset(&msg, 0, sizeof(msg));
            msg.type = SUP_ADAP_EVT_DEAUTH;
            msg.msg = dinfo;
            msg.len = sizeof(*dinfo);
            if (send_msg_to_sup_adap(&msg) != 0) {
                if (dinfo->ie)
                    os_free((void *)dinfo->ie);
                if (dinfo->addr)
                    os_free((void *)dinfo->addr);
                os_free(dinfo);
            }
        }
        break;
    case WLC_E_REASSOC:
        assoc_info.reassoc = 1;
        /* fallthrough */
    case WLC_E_ASSOC:
        {
            drv_syna_msg_t msg;
            struct drv_assoc_info *drv_assoc = NULL;
            uint16_t status = hdr ? (uint16_t)hdr->status : 0xffff;

            wpa_printf(MSG_DEBUG, "Adaptor: %s(%d) (status=%u)",
                       (event == WLC_E_REASSOC) ? "WLC_E_REASSOC" : "WLC_E_ASSOC",
                       event, status);

            if (status != WLC_E_STATUS_SUCCESS) {
                wpa_printf(MSG_DEBUG,
                           "Adaptor: %s non-success status=%u -> skip ASSOC forward",
                           (event == WLC_E_REASSOC) ? "WLC_E_REASSOC" : "WLC_E_ASSOC",
                           status);
                assoc_info.reassoc = 0;
                break;
            }

            if (hdr && (!assoc_info.addr || os_memcmp(assoc_info.addr, hdr->addr.octet, ETH_ALEN))) {
                mhd_mac_t bssid;

                if (!assoc_info.addr)
                    assoc_info.addr = os_zalloc(ETH_ALEN);
                if (!assoc_info.addr) {
                    wpa_printf(MSG_ERROR,
                               "Adaptor: WLC_E_ASSOC: Failed to allocate assoc_info.addr");
                    break;
                }
                if (mhd_wifi_api_get_bssid(MHD_STA_INTERFACE, &bssid) == MHD_SUCCESS)
                    memcpy((void *)assoc_info.addr, bssid.octet, ETH_ALEN);
                else
                    memcpy((void *)assoc_info.addr, &hdr->addr.octet[0], ETH_ALEN);
            }

            msg.type = DRV_SYNA_EVENT_ASSOC;
            msg.data = os_zalloc(sizeof(struct drv_assoc_info));
            drv_assoc = msg.data;
            if (!drv_assoc) {
                wpa_printf(MSG_ERROR,
                           "Adaptor: WLC_E_ASSOC: Failed to allocate drv_assoc_info");
                break;
            }
            drv_assoc->reassoc = assoc_info.reassoc;
            drv_assoc->addr = assoc_info.addr;
            drv_assoc->freq = assoc_info.freq;
            drv_assoc->req_ies = assoc_info.req_ies;
            drv_assoc->req_ies_len = assoc_info.req_ies_len;
            drv_assoc->resp_ies = assoc_info.resp_ies;
            drv_assoc->resp_ies_len = assoc_info.resp_ies_len;
            st->assoc_indicated = 1;

            assoc_info.addr = NULL;
            assoc_info.req_ies = NULL;
            assoc_info.req_ies_len = 0;
            assoc_info.resp_ies = NULL;
            assoc_info.resp_ies_len = 0;
            assoc_info.reassoc = 0;
            if (sup_adap_send_supp_msg(&msg) != 0) {
                os_free((void *)drv_assoc->addr);
                os_free((void *)drv_assoc->req_ies);
                os_free((void *)drv_assoc->resp_ies);
                os_free(drv_assoc);
                st->assoc_indicated = 0;
            }
        }
        break;
    case WLC_E_ASSOC_REQ_IE:
        {
            uint16_t status = hdr ? (uint16_t)hdr->status : 0xffff;
            struct drv_assoc_info *assoc_req = &assoc_info;

            wpa_printf(MSG_DEBUG, "Adaptor: WLC_E_ASSOC_REQ_IE(%d) (status=%u)",
                       event, status);
            if (payload && payload_len) {
                assoc_req->req_ies = os_zalloc(payload_len);
                if (assoc_req->req_ies) {
                    memcpy((void *)assoc_req->req_ies, payload, payload_len);
                    assoc_req->req_ies_len = payload_len;
                } else {
                    assoc_req->req_ies_len = 0;
                    wpa_printf(MSG_ERROR,
                               "Adaptor: WLC_E_ASSOC_REQ_IE: Failed to allocate req_ies");
                }
            } else {
                assoc_req->req_ies_len = 0;
            }
        }
        break;
    case WLC_E_ASSOC_RESP_IE:
        {
            uint16_t status = hdr ? (uint16_t)hdr->status : 0xffff;
            struct drv_assoc_info *assoc_resp = &assoc_info;

            wpa_printf(MSG_DEBUG, "Adaptor: WLC_E_ASSOC_RESP_IE(%d) (status=%u)",
                       event, status);
            if (payload && payload_len) {
                assoc_resp->resp_ies = os_zalloc(payload_len);
                if (assoc_resp->resp_ies) {
                    memcpy((void *)assoc_resp->resp_ies, payload, payload_len);
                    assoc_resp->resp_ies_len = payload_len;
                } else {
                    assoc_resp->resp_ies_len = 0;
                    wpa_printf(MSG_ERROR,
                               "Adaptor: WLC_E_ASSOC_RESP_IE: Failed to allocate resp_ies");
                }
            } else {
                assoc_resp->resp_ies_len = 0;
            }
        }
        break;
    case WLC_E_CSA_COMPLETE_IND:
        {
            drv_ch_switch_info_t *csa_info = os_zalloc(sizeof(drv_ch_switch_info_t));
            if (!csa_info) {
                wpa_printf(MSG_ERROR,
                           "Adaptor: WLC_E_CSA_COMPLETE_IND: alloc failed");
                break;
            }

            if (!payload) {
                wpa_printf(MSG_WARNING,
                           "Adaptor: WLC_E_CSA_COMPLETE_IND: no payload, dropping");
                os_free(csa_info);
                break;
            }

            const csa_event_data_t *csa = (const csa_event_data_t *)payload;
            const mhd_chandef_t *cd = &csa->chandef;

            csa_info->freq = mhd_channel_to_frequency(cd->primary_chan, cd->band);
            csa_info->ht_enabled = (cd->chan_type == MHD_CHAN_NO_HT) ? 0 : 1;
            csa_info->ch_width = map_chanspec_bw_to_chan_width(cd->bw);
            csa_info->cf1 = (int)cd->center_freq1;
            csa_info->cf2 = (int)cd->center_freq2;
            csa_info->ch_offset = (cd->chan_type == MHD_CHAN_HT40PLUS) ? 1 :
                                  (cd->chan_type == MHD_CHAN_HT40MINUS) ? -1 : 0;
            csa_info->link_id = (int)csa->link_id;
            csa_info->punct_bitmap = (u16)csa->punct_bitmap;

            wpa_printf(MSG_DEBUG,
                       "Adaptor: WLC_E_CSA_COMPLETE_IND: freq=%d cf1=%d bw=%d ch_offset=%d link_id=%d punct=0x%x",
                       csa_info->freq, csa_info->cf1, (int)cd->bw,
                       csa_info->ch_offset, csa_info->link_id,
                       csa_info->punct_bitmap);

            drv_syna_msg_t drv_msg;
            os_memset(&drv_msg, 0, sizeof(drv_msg));
            drv_msg.type = DRV_SYNA_EVENT_CH_SWITCH;
            drv_msg.data = csa_info;
            sup_adap_send_supp_msg(&drv_msg);
        }
        break;
    case WLC_E_LINK:
        {
            mhd_event_status_t status = hdr->status;
            uint16_t flags = hdr->flags;

            wpa_printf(MSG_DEBUG, "Adaptor: WLC_E_LINK(%d) (status=%u) (flags=%u)",
                       event, status, flags);

            if (status == WLC_E_STATUS_SUCCESS && (flags & WLC_EVENT_MSG_LINK) &&
                st->assoc_indicated) {
                wpa_printf(MSG_DEBUG,
                           "Adaptor: LINK Up while connected -> refresh FW BSS info");
                adap_refresh_bss_info_from_fw("WLC_E_LINK");
            }

            if (status == WLC_E_STATUS_SUCCESS && !(flags & WLC_EVENT_MSG_LINK)) {
                wpa_printf(MSG_DEBUG, "Adaptor: LINK Down Event Observed");
                st->assoc_indicated = 0;
                st->assoc_control_stage = 0;
            }
        }
        break;
    case WLC_E_JOIN_START:
        {
            mhd_ext_auth_evt_t *evt_data = (mhd_ext_auth_evt_t *)payload;
            drv_syna_msg_t msg;
            struct drv_external_auth *drv_external_auth = NULL;
            uint16_t status = hdr ? (uint16_t)hdr->status : 0xffff;
            uint32_t host_wpa_auth;
            mhd_result_t rc = MHD_SUCCESS;

            wpa_printf(MSG_DEBUG, "Adaptor: WLC_E_JOIN_START(%d) (status=%u)",
                       event, status);
            msg.type = DRV_SYNA_EVENT_JOIN_START;
            msg.data = os_zalloc(sizeof(struct drv_external_auth));
            drv_external_auth = msg.data;
            if (!drv_external_auth) {
                wpa_printf(MSG_ERROR,
                           "Adaptor: WLC_E_JOIN_START: Failed to allocate drv_external_auth");
                break;
            }
            drv_external_auth->ssid_len = MIN(evt_data->wl_ext_auth->ssid.SSID_len, SSID_MAX_LEN);
            if (drv_external_auth->ssid_len) {
                drv_external_auth->ssid = os_zalloc(drv_external_auth->ssid_len);
                if (drv_external_auth->ssid)
                    memcpy(drv_external_auth->ssid,
                           evt_data->wl_ext_auth->ssid.SSID,
                           drv_external_auth->ssid_len);
                else {
                    wpa_printf(MSG_ERROR,
                               "Adaptor: WLC_E_JOIN_START: Failed to allocate ssid buffer");
                    drv_external_auth->ssid_len = 0;
                }
            }
            drv_external_auth->bssid = os_zalloc(ETH_ALEN);
            if (drv_external_auth->bssid)
                memcpy(drv_external_auth->bssid,
                       evt_data->wl_ext_auth->bssid.octet, ETH_ALEN);
            else
                wpa_printf(MSG_ERROR,
                           "Adaptor: WLC_E_JOIN_START: Failed to allocate bssid buffer");
            host_wpa_auth = evt_data->wl_ext_auth->key_mgmt_suite;
            drv_external_auth->key_mgmt_suite = translate_auth_key_mgmt_suite(host_wpa_auth);
            if (!drv_external_auth->key_mgmt_suite)
                drv_external_auth->key_mgmt_suite = RSN_AUTH_KEY_MGMT_SAE;
            drv_external_auth->status = 0;

            wpa_printf(MSG_INFO, "Adaptor: WLC_E_JOIN_START: key_mgmt_suite=0x%x",
                       drv_external_auth->key_mgmt_suite);

            if (!is_zero_ether_addr(evt_data->mld_addr.octet)) {
                drv_external_auth->mld_addr = os_zalloc(ETH_ALEN);
                if (drv_external_auth->mld_addr) {
                    memcpy((u8 *)drv_external_auth->mld_addr,
                           evt_data->mld_addr.octet, ETH_ALEN);
                    wpa_printf(MSG_INFO,
                               "Adaptor: WLC_E_JOIN_START - MLD Address:" MACDBG,
                               MAC2STRDBG(&evt_data->mld_addr));
                } else {
                    wpa_printf(MSG_ERROR,
                               "Adaptor: WLC_E_JOIN_START: Failed to allocate mld_addr buffer");
                }
            } else {
                wpa_printf(MSG_INFO, "Adaptor: WLC_E_JOIN_START - No MLD Address");
            }

            wpa_printf(MSG_DEBUG,
                       "Adaptor: WLC_E_JOIN_START(%d) SSID: %.*s(%d), BSSID:" MACDBG,
                       event, drv_external_auth->ssid_len,
                       drv_external_auth->ssid ? (const char *)drv_external_auth->ssid : "",
                       evt_data->wl_ext_auth->ssid.SSID_len,
                       MAC2STRDBG(&evt_data->wl_ext_auth->bssid));

            rc = mhd_wifi_api_assoc_mgr_cmd(WL_ASSOC_MGR_CMD_PAUSE_ON_EVT,
                                            WL_ASSOC_MGR_PARAMS_PAUSE_EVENT_AUTH_RESP);
            if (rc != 0) {
                wpa_printf(MSG_ERROR,
                           "Adaptor: mhd_wifi_api_assoc_mgr_cmd failed rc=%ld",
                           (long)rc);
            } else {
                wpa_printf(MSG_DEBUG,
                           "Adaptor: mhd_wifi_api_assoc_mgr_cmd dispatched");
            }
            if (sup_adap_send_supp_msg(&msg) != 0) {
                if (drv_external_auth->ssid)
                    os_free((void *)drv_external_auth->ssid);
                if (drv_external_auth->bssid)
                    os_free((void *)drv_external_auth->bssid);
                if (drv_external_auth->mld_addr)
                    os_free((void *)drv_external_auth->mld_addr);
                os_free(drv_external_auth);
            }
        }
        break;
    default:
        wpa_printf(MSG_DEBUG, "Adaptor: Unhandled raw WLC_E event %lu",
                   (unsigned long)event);
        break;
    }
}

void mhd_wifi_common_events_handler(const mhd_event_header_t *event_header,
                                    const uint8_t *event_data,
                                    void *user_data)
{
    uint32_t event = event_header->event_type;
    uint32_t data_len = event_header->datalen;

    supplicant_mhd_event_cb(event, event_header, event_data, data_len, user_data);
}