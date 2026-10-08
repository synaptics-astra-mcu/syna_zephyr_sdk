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
#include "supplicant_mhd_event.h"
#include "supplicant_mhd_transport.h"
#ifdef ZEPHYR_OS
#include "supplicant_mhd_transport_zephyr.h"
#endif
#include "common/wpa_common.h" /* For WPA_PROTO_*, WPA_CIPHER_*, RSN_AUTH_KEY_MGMT_* */
#include "syna_wifi_sc_filter.h"
#include "supp_drv_platform_headers.h"

/* Minimal WL chanspec Macros for chanspec parsing
 * TODO: Replace with MHD macros once we have a common header that exposes these
 */
#ifndef WL_CHANSPEC_BAND_MASK
#define WL_CHANSPEC_BAND_MASK      0xc000
#endif
#ifndef WL_CHANSPEC_BAND_SHIFT
#define WL_CHANSPEC_BAND_SHIFT         14
#endif
#ifndef WL_CHANSPEC_BW_MASK
#define WL_CHANSPEC_BW_MASK        0x3800
#endif
#ifndef WL_CHANSPEC_BW_SHIFT
#define WL_CHANSPEC_BW_SHIFT           11
#endif
/* Fallback definitions in case mhd_events.h is not exposing these due to
 * compile-time condition differences between supplicant and MHD cores.
 */
#ifndef WLC_E_SET_SSID
#define WLC_E_SET_SSID 0u
#endif
#ifndef WLC_E_DISASSOC_IND
#define WLC_E_DISASSOC_IND 12u
#endif

#define MACDBG "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STRDBG(ea)  ((const uint8*)(ea))[0], \
                        ((const uint8*)(ea))[1], \
                        ((const uint8*)(ea))[2], \
                        ((const uint8*)(ea))[3], \
                        ((const uint8*)(ea))[4], \
                        ((const uint8*)(ea))[5]

/* Local forward declarations */
uint8_t convert_freq_to_channel(struct hostapd_freq_params *freq,  drv_syna_channel_info_t *params);

/* Global scan results list shared across the adaptor/driver path. */
struct dl_list drv_scan_results_list;

/* Global assoc_info */
struct drv_assoc_info assoc_info;

/*
 * Lightweight adaptor focused on scan control and minimal scan-result
 * integration between wpa_supplicant and the MHD task.
 *
 */

/* Map security parameters from supplicant association request to MHD join params */
static void map_sta_security(const struct wpa_driver_associate_params *ap,
                         mhd_wifi_join_params_t *join)
{
    mhd_password_t *pwd_obj;
    if (!ap || !join) return;
    join->security = HOST_SECURITY_OPEN;

    if (!ap->wpa_proto && !ap->key_mgmt_suite) return; /* Open network */

        pwd_obj = os_zalloc(sizeof(mhd_password_t));
        if (!pwd_obj) {
            wpa_printf(MSG_ERROR, "Adaptor: Failed to allocate password object");
            return;
        }
    /* WEP first */
    if (ap->wep_key[ap->wep_tx_keyidx] && ap->wep_key_len[ap->wep_tx_keyidx]) {
        join->security = (ap->auth_alg & WPA_AUTH_ALG_SHARED) ? HOST_SECURITY_WEP_SHARED : HOST_SECURITY_WEP_PSK;
        os_memcpy(pwd_obj->value, ap->wep_key[ap->wep_tx_keyidx], ap->wep_key_len[ap->wep_tx_keyidx]);
        pwd_obj->length = (uint8_t) ap->wep_key_len[ap->wep_tx_keyidx];
        join->pwd = pwd_obj;
        return;
    }

    if (!ap->wpa_proto &&
        (!ap->key_mgmt_suite || ap->key_mgmt_suite == WPA_KEY_MGMT_NONE))
        return;

    int is_wpa  = (ap->wpa_proto & WPA_PROTO_WPA)  != 0;
    int is_rsn  = (ap->wpa_proto & WPA_PROTO_RSN)  != 0;
#ifdef WPA_PROTO_WPA3
    int is_wpa3 = (ap->wpa_proto & WPA_PROTO_WPA3) != 0;
#else
    int is_wpa3 = 0;
#endif
    unsigned int akm = ap->key_mgmt_suite;
    int has_sae = (akm & (WPA_KEY_MGMT_SAE | WPA_KEY_MGMT_FT_SAE | WPA_KEY_MGMT_SAE_EXT_KEY | WPA_KEY_MGMT_FT_SAE_EXT_KEY)) != 0;
    int has_psk = (akm & (WPA_KEY_MGMT_PSK | WPA_KEY_MGMT_PSK_SHA256 | WPA_KEY_MGMT_FT_PSK)) != 0;
    int has_eap = (akm & (WPA_AUTH_KEY_MGMT_UNSPEC_802_1X | RSN_AUTH_KEY_MGMT_UNSPEC_802_1X | RSN_AUTH_KEY_MGMT_802_1X_SHA256 | RSN_AUTH_KEY_MGMT_802_1X_SUITE_B | RSN_AUTH_KEY_MGMT_802_1X_SUITE_B_192 | RSN_AUTH_KEY_MGMT_802_1X_SHA384 | RSN_AUTH_KEY_MGMT_FT_802_1X | RSN_AUTH_KEY_MGMT_FT_802_1X_SHA384)) != 0;
    int has_owe = (akm & (WPA_KEY_MGMT_OWE)) != 0;
    if (has_sae || has_psk) {
        if (ap->psk) {
            os_memcpy(pwd_obj->value, ap->psk, 32);
            pwd_obj->length = 32;
            join->pwd = pwd_obj;
        }
        else if (ap->passphrase)
        {
            size_t plen = strnlen((const char*)ap->passphrase, 64);
            if (plen >= 8 && plen <= 63)
            {
                os_memcpy(pwd_obj->value, (const uint8_t*)ap->passphrase, (uint8_t)plen);
                pwd_obj->length = (uint8_t)plen;
                join->pwd = pwd_obj;
            }
        }
        else if (ap->sae_password)
        {
            size_t plen = strnlen((const char*)ap->sae_password, 64);
            if (plen >= 8 && plen <= 63)
            {
                os_memcpy(pwd_obj->value, (const uint8_t*)ap->sae_password, (uint8_t)plen);
                pwd_obj->length = (uint8_t)plen;
                join->pwd = pwd_obj;
            }
        }
    }
    if (has_sae) {
        if (is_rsn && is_wpa3) {
            join->security = HOST_SECURITY_WPA2_WPA3_MIXED;
        }
        else if (is_wpa3 || is_rsn) {
            if ((ap->pairwise_suite & WPA_CIPHER_GCMP_256) && (ap->group_suite & WPA_CIPHER_GCMP_256))
                join->security = HOST_SECURITY_WPA3_SAE_GCMP_256_PSK;
            else
                join->security = HOST_SECURITY_WPA3_SAE_PSK;
        }
        else {
            join->security = HOST_SECURITY_WPA3_SAE_PSK; 
        }
        if (akm & WPA_KEY_MGMT_SAE_EXT_KEY) {
                if ((ap->pairwise_suite & WPA_CIPHER_GCMP_256) && (ap->group_suite & WPA_CIPHER_GCMP_256)) {
                        join->security = HOST_SECURITY_WPA3_SAE_EXT_KEY_GCMP_256_PSK;
                }
                else {
                        join->security = HOST_SECURITY_WPA3_SAE_EXT_KEY_PSK;
                }
        }
        join->pwd = pwd_obj;
        join->sae_pwe = ap->sae_pwe;
        return;
    }
    
    if (has_psk) {
        if (is_wpa && is_rsn) join->security = HOST_SECURITY_WPA_WPA2_MIXED;
        else if (is_rsn) {
            if ((ap->pairwise_suite & WPA_CIPHER_GCMP_256) &&
                (ap->group_suite & WPA_CIPHER_GCMP_256))
                join->security = HOST_SECURITY_WPA2_GCMP_256_PSK;
            else if ((ap->pairwise_suite & WPA_CIPHER_CCMP) &&
                     (ap->group_suite & WPA_CIPHER_CCMP))
                join->security = HOST_SECURITY_WPA2_AES_PSK;
            else if ((ap->pairwise_suite & WPA_CIPHER_CCMP) &&
                     (ap->group_suite & WPA_CIPHER_TKIP))
                join->security = HOST_SECURITY_WPA2_MIXED_PSK;
            else {
                join->security = HOST_SECURITY_WPA2_MIXED_PSK;
            }
        } else if (is_wpa) {
            if ((ap->pairwise_suite & WPA_CIPHER_CCMP) && (ap->pairwise_suite & WPA_CIPHER_TKIP)) 
                join->security = HOST_SECURITY_WPA_MIXED_PSK;
            else if (ap->pairwise_suite & WPA_CIPHER_CCMP) 
                join->security = HOST_SECURITY_WPA_AES_PSK;
            else if (ap->pairwise_suite & WPA_CIPHER_TKIP) 
                join->security = HOST_SECURITY_WPA_TKIP_PSK;
            else 
                join->security = HOST_SECURITY_WPA_MIXED_PSK;
        }
        join->pwd = pwd_obj;
        return;
    }
    
    if (has_eap) {
        if (is_wpa && is_rsn) join->security = HOST_SECURITY_WPA2_MIXED_ENT;
        else if (is_rsn) {
            if ((ap->pairwise_suite & WPA_CIPHER_CCMP) && (ap->pairwise_suite & WPA_CIPHER_TKIP)) 
                join->security = HOST_SECURITY_WPA2_MIXED_ENT;
            else if (ap->pairwise_suite & WPA_CIPHER_CCMP) 
                join->security = HOST_SECURITY_WPA2_AES_ENT;
            else if (ap->pairwise_suite & WPA_CIPHER_TKIP) 
                join->security = HOST_SECURITY_WPA2_TKIP_ENT;
            else 
                join->security = HOST_SECURITY_WPA2_MIXED_ENT;
        } else if (is_wpa) {
            if ((ap->pairwise_suite & WPA_CIPHER_CCMP) && (ap->pairwise_suite & WPA_CIPHER_TKIP)) 
                join->security = HOST_SECURITY_WPA_MIXED_ENT;
            else if (ap->pairwise_suite & WPA_CIPHER_CCMP) 
                join->security = HOST_SECURITY_WPA_AES_ENT;
            else if (ap->pairwise_suite & WPA_CIPHER_TKIP) 
                join->security = HOST_SECURITY_WPA_TKIP_ENT;
            else 
                join->security = HOST_SECURITY_WPA_MIXED_ENT;
        }
    }
    if (has_owe) {
        join->security = HOST_SECURITY_WPA3_OWE;
    }
    join->pwd = pwd_obj;
    return;
}

#ifdef CONFIG_AP
static void map_ap_security(const struct wpa_driver_ap_params *ap,
                         mhd_wifi_ap_params_t *ap_params)
{
    mhd_password_t *pwd_obj;
    if (!ap || !ap_params) return;
    ap_params->security = HOST_SECURITY_OPEN;

    /* Check for open network before allocating password object */
    if (!ap->wpa_version && !ap->key_mgmt_suites) return;

    pwd_obj = os_zalloc(sizeof(mhd_password_t));
    if (!pwd_obj) {
        wpa_printf(MSG_ERROR, "Adaptor: Failed to allocate password object");
        return;
    }
    /* hostapd not support WEP by default */
#ifdef CONFIG_WEP
    /* WEP first */
    if (ap->wep_key[ap->wep_tx_keyidx] && ap->wep_key_len[ap->wep_tx_keyidx]) {
        ap_params->security = (ap->auth_alg & WPA_AUTH_ALG_SHARED) ? HOST_SECURITY_WEP_SHARED : HOST_SECURITY_WEP_PSK;
        os_memcpy(pwd_obj->value, ap->wep_key[ap->wep_tx_keyidx], ap->wep_key_len[ap->wep_tx_keyidx]);
        pwd_obj->length = (uint8_t) ap->wep_key_len[ap->wep_tx_keyidx];
        ap_params->pwd = pwd_obj;
        return;
    }
#endif /* CONFIG_WEP */
    int is_wpa  = (ap->wpa_version & WPA_PROTO_WPA)  != 0;
    int is_rsn  = (ap->wpa_version & WPA_PROTO_RSN)  != 0;
#ifdef WPA_PROTO_WPA3
    int is_wpa3 = (ap->wpa_version & WPA_PROTO_WPA3) != 0;
#else
    int is_wpa3 = 0;
#endif
    unsigned int akm = ap->key_mgmt_suites;
    int has_sae = (akm & (WPA_KEY_MGMT_SAE | WPA_KEY_MGMT_FT_SAE | WPA_KEY_MGMT_SAE_EXT_KEY | WPA_KEY_MGMT_FT_SAE_EXT_KEY)) != 0;
    int has_psk = (akm & (WPA_KEY_MGMT_PSK | WPA_KEY_MGMT_PSK_SHA256 | WPA_KEY_MGMT_FT_PSK)) != 0;
    int has_eap = (akm & (WPA_AUTH_KEY_MGMT_UNSPEC_802_1X | RSN_AUTH_KEY_MGMT_UNSPEC_802_1X | RSN_AUTH_KEY_MGMT_802_1X_SHA256 | RSN_AUTH_KEY_MGMT_802_1X_SUITE_B | RSN_AUTH_KEY_MGMT_802_1X_SUITE_B_192 | RSN_AUTH_KEY_MGMT_802_1X_SHA384 | RSN_AUTH_KEY_MGMT_FT_802_1X | RSN_AUTH_KEY_MGMT_FT_802_1X_SHA384)) != 0;

    if (has_sae || has_psk) {
        if (ap->psk_len) {
            size_t psk_len = ap->psk_len > sizeof(pwd_obj->value) ? sizeof(pwd_obj->value) : ap->psk_len;
            os_memcpy(pwd_obj->value, ap->psk, psk_len);
            pwd_obj->length = (uint8_t)psk_len;
            ap_params->security_key = pwd_obj;
        }
        else if (ap->sae_password)
        {
            size_t plen = strnlen((const char*)ap->sae_password, 64);
            if (plen >= 8 && plen <= 63)
            {
                os_memcpy(pwd_obj->value, (const uint8_t*)ap->sae_password, (uint8_t)plen);
                pwd_obj->length = (uint8_t)plen;
                ap_params->security_key = pwd_obj;
            }
        }
    }
    if (has_sae) {
        if (is_rsn && is_wpa3) {
            ap_params->security = HOST_SECURITY_WPA2_WPA3_MIXED;
        }
        else if (is_wpa3 || is_rsn) {
            if ((ap->pairwise_ciphers & WPA_CIPHER_GCMP_256) && (ap->group_cipher & WPA_CIPHER_GCMP_256))
                ap_params->security = HOST_SECURITY_WPA3_SAE_GCMP_256_PSK;
            else
                ap_params->security = HOST_SECURITY_WPA3_SAE_PSK;
        }
        else {
            ap_params->security = HOST_SECURITY_WPA3_SAE_PSK;
        }
        ap_params->sae_pwe = ap->sae_pwe;
        if (ap_params->security_key != pwd_obj)
            os_free(pwd_obj);
        return;
    }

    if (has_psk) {
        if (is_wpa && is_rsn) ap_params->security = HOST_SECURITY_WPA_WPA2_MIXED;
        else if (is_rsn) {
            if ((ap->pairwise_ciphers & WPA_CIPHER_CCMP) && (ap->pairwise_ciphers & WPA_CIPHER_TKIP)) {
                ap_params->security = HOST_SECURITY_WPA2_MIXED_PSK;
	    } else if (ap->pairwise_ciphers & WPA_CIPHER_CCMP) {
                ap_params->security = HOST_SECURITY_WPA2_AES_PSK;
	    } else if (ap->pairwise_ciphers & WPA_CIPHER_TKIP)
                ap_params->security = HOST_SECURITY_WPA2_TKIP_PSK;
            else
                ap_params->security = HOST_SECURITY_WPA2_MIXED_PSK;
        } else if (is_wpa) {
            if ((ap->pairwise_ciphers & WPA_CIPHER_CCMP) && (ap->pairwise_ciphers & WPA_CIPHER_TKIP))
                ap_params->security = HOST_SECURITY_WPA_MIXED_PSK;
            else if (ap->pairwise_ciphers & WPA_CIPHER_CCMP)
                ap_params->security = HOST_SECURITY_WPA_AES_PSK;
            else if (ap->pairwise_ciphers & WPA_CIPHER_TKIP)
                ap_params->security = HOST_SECURITY_WPA_TKIP_PSK;
            else
                ap_params->security = HOST_SECURITY_WPA_MIXED_PSK;
        }
        if (ap_params->security_key != pwd_obj)
            os_free(pwd_obj);
        return;
    }

    if (has_eap) {
        if (is_wpa && is_rsn) ap_params->security = HOST_SECURITY_WPA2_MIXED_ENT;
        else if (is_rsn) {
            if ((ap->pairwise_ciphers & WPA_CIPHER_CCMP) && (ap->pairwise_ciphers & WPA_CIPHER_TKIP))
                ap_params->security = HOST_SECURITY_WPA2_MIXED_ENT;
            else if (ap->pairwise_ciphers & WPA_CIPHER_CCMP)
                ap_params->security = HOST_SECURITY_WPA2_AES_ENT;
            else if (ap->pairwise_ciphers & WPA_CIPHER_TKIP)
                ap_params->security = HOST_SECURITY_WPA2_TKIP_ENT;
            else
                ap_params->security = HOST_SECURITY_WPA2_MIXED_ENT;
        } else if (is_wpa) {
            if ((ap->pairwise_ciphers & WPA_CIPHER_CCMP) && (ap->pairwise_ciphers & WPA_CIPHER_TKIP))
                ap_params->security = HOST_SECURITY_WPA_MIXED_ENT;
            else if (ap->pairwise_ciphers & WPA_CIPHER_CCMP)
                ap_params->security = HOST_SECURITY_WPA_AES_ENT;
            else if (ap->pairwise_ciphers & WPA_CIPHER_TKIP)
                ap_params->security = HOST_SECURITY_WPA_TKIP_ENT;
            else
                ap_params->security = HOST_SECURITY_WPA_MIXED_ENT;
        }
    }
    if (ap_params->security_key != pwd_obj)
        os_free(pwd_obj);
}
#endif

/* Events we are interested in from MHD for supplicant integration. We use
 * raw firmware event codes (WLC_E_*). Association success is inferred from
 * WLC_E_SET_SSID (control plane complete). Disassociation is inferred from
 * WLC_E_DISASSOC_IND. LINK events are handled by lower layers (TCP/IP stack)
 * and are no longer routed through the supplicant adaptor.
 */
static uint32_t supp_mhd_sta_events[] = { WLC_E_AUTH, WLC_E_ASSOC, WLC_E_REASSOC, WLC_E_ASSOC_REQ_IE,
                                          WLC_E_ASSOC_RESP_IE, WLC_E_LINK, WLC_E_SET_SSID,
                                          WLC_E_DEAUTH_IND, WLC_E_DISASSOC_IND, WLC_E_DISASSOC, 
                                          WLC_E_JOIN_START, WLC_E_CSA_COMPLETE_IND,
                                        };
/* Events we are interested in from MHD for supplicant integration in AP mode.
 * These events indicate external STAs connecting to our SoftAP.
 */
static uint32_t supp_mhd_ap_events[] = { WLC_E_AUTH, WLC_E_AUTH_IND, WLC_E_DEAUTH_IND, WLC_E_ASSOC_IND,
                                         WLC_E_DISASSOC_IND, WLC_E_LINK, WLC_E_IF,
                                       };

static struct sup_adap_state g_sup_state = { 0, 0 };

struct sup_adap_state *sup_adap_transport_state(void)
{
    return &g_sup_state;
}

const uint32_t *sup_adap_transport_sta_events(size_t *count)
{
    if (count)
        *count = ARRAY_SIZE(supp_mhd_sta_events);
    return supp_mhd_sta_events;
}

const uint32_t *sup_adap_transport_ap_events(size_t *count)
{
    if (count)
        *count = ARRAY_SIZE(supp_mhd_ap_events);
    return supp_mhd_ap_events;
}

int sup_adap_register_sta_event_handler(mhd_interface_t interface)
{
    size_t event_count = 0;
    const uint32_t *event_list;
    mhd_wifi_event_handler_params_t eh_sta;

    if (interface != MHD_STA_INTERFACE) {
        /* TODO: Currently, the AP event handler registration is done
         * only in .set_ap driver API and is not supported
         * as a part of driver's interface initialization itself
         * Registering AP event handler should be supported here once
         * actual AP interface creation is done once supplicant supports
         * multi-interface management architecture.
         */
        return -1;
    }

    event_list = sup_adap_transport_sta_events(&event_count);
    eh_sta.event_list = event_list;
    eh_sta.event_count = event_count;
    eh_sta.event_cb = mhd_wifi_common_events_handler;
    eh_sta.user_ctx = sup_adap_transport_state();
    eh_sta.interface = interface;

    if (mhd_wifi_api_set_event_handler(&eh_sta) != 0) {
        wpa_printf(MSG_ERROR,
                   "Adaptor: failed to register MHD event handler (if=%d)",
                   interface);
        return -1;
    }

    wpa_printf(MSG_DEBUG, "Adaptor: registered MHD event handler (if=%d)",
               interface);
    return 0;
}

/* Dispatch a single drv_scan_result_t to the supplicant task */
static void syna_dispatch_partial_scan_result(drv_scan_result_t *scan_res)
{
    if (!scan_res) return;
    drv_syna_msg_t drv_msg; 
    drv_msg.type = DRV_SYNA_EVENT_SCAN_RESULT;
    drv_msg.data = scan_res; /* ownership transfers; freed in handle_event_scan_result */
    sup_adap_send_supp_msg(&drv_msg);
}

/* syna_scan_cb
 * - status indicates partial/completed/aborted
 * - data is wl_bss_info_t* for partial results when status==HOST_SCAN_INCOMPLETE
 *
 * New behaviour (default):
 *   For each partial result allocate drv_scan_result_t, split it into a 2G/5G/6G
 *   list, and keep each list sorted by RSSI from strongest to weakest.
 *   On completion/abort, drop the weakest tail percentage per band, flush the
 *     - Missing scan completion notification
 *     - Empty scan result list forcing fallback issues
 *     - Incorrect frequency derived from chanspec (now uses control channel)
 */
uint32_t sc_result_cnt = 0;
static void sup_adap_scan_cb(mhd_ifidx_t ifidx, void *data, uint32_t status, void *context)
{
    UNUSED_PARAMETER(context);
    UNUSED_PARAMETER(ifidx);
    wl_bss_info_t *bi = (wl_bss_info_t *) data;

#ifdef ZEPHYR_OS
    if (sup_adap_transport_handle_zephyr_scan_event(data, status))
        return;
#endif

    if (status == HOST_SCAN_INCOMPLETE) {
        if (sc_result_cnt >= MAX_SCAN_RESULTS)
            return;
        if (!bi) return;

        chanspec_band_t band = (chanspec_band_t)(CHSPEC_BAND(bi->chanspec) >> WL_CHANSPEC_BAND_SHIFT);
        size_t ssid_len = (size_t)MIN(bi->SSID_len, sizeof(bi->SSID));

        if (syna_sc_filter_entry(bi, band) == SC_FILTER_DISCARD)
            return;

        size_t ie_len = (size_t) bi->ie_length;
        size_t extra = 2 + ssid_len + 2 + (size_t)MIN(bi->rateset.count, 15) + ie_len;
        drv_scan_result_t *scan_res = os_zalloc(sizeof(drv_scan_result_t) + extra);
        if (!scan_res) return;
        scan_res->scan_completed = 0;
        scan_res->ssids.ssid_len = ssid_len;
        if (ssid_len) {
            scan_res->ssids.ssid = os_zalloc(ssid_len);
            if (scan_res->ssids.ssid)
                os_memcpy((u8*)scan_res->ssids.ssid, bi->SSID, ssid_len);
        }
        os_memcpy(scan_res->bssid, &bi->BSSID, ETH_ALEN);

        uint16_t primary_chan = mhd_chspec_primary20_chan(bi->chanspec);
        chanspec_bw_t bw = (chanspec_bw_t)(CHSPEC_BW(bi->chanspec) >> WL_CHANSPEC_BW_SHIFT);
        scan_res->freq = mhd_channel_to_frequency(primary_chan, band);
        scan_res->max_cw = map_chanspec_bw_to_chan_width(bw);
        scan_res->beacon_int = (u16)bi->beacon_period;
        scan_res->caps = (u16)bi->capability;
        scan_res->level = bi->RSSI;

        u8 *pos = (u8 *)(scan_res + 1);
        *pos++ = WLAN_EID_SSID;
        *pos++ = (u8) ssid_len;
        if (ssid_len) {
            os_memcpy(pos, bi->SSID, ssid_len); pos += ssid_len; 
        }
        u32 nrate_len = MIN(bi->rateset.count, (uint32_t)8);
        *pos++ = WLAN_EID_SUPP_RATES;
        *pos++ = (u8) nrate_len;
        os_memcpy(pos, (uint8_t *)(bi->rateset.rates), nrate_len);
        pos += nrate_len;
        if (ie_len) {
            os_memcpy(pos, (uint8_t *)(((uint8_t *)bi) + bi->ie_offset), ie_len);
            pos += ie_len;
        }
        scan_res->ie_len = pos - (u8 *)(scan_res + 1);

        if (syna_sc_queue_result(scan_res, band) == SC_QUEUE_ADDED)
            sc_result_cnt++;
        return;
    }

    syna_sc_drop_tails();
    syna_sc_log_completion(status, sc_result_cnt);
    syna_sc_flush_to_global();
    /* Completion / abort */
    /* Allocate terminal marker so handle_event_scan_result can emit EVENT_SCAN_RESULTS */
    drv_scan_result_t *terminal = os_zalloc(sizeof(drv_scan_result_t));
    if (terminal) {
        terminal->scan_completed = 1;
        if (status == HOST_SCAN_COMPLETED)
            wpa_printf(MSG_INFO, "sup_adap_scan_cb: scan completed (per-driver)");
        else if (status == HOST_SCAN_ABORTED)
            wpa_printf(MSG_INFO, "sup_adap_scan_cb: scan aborted (per-driver)");
        syna_dispatch_partial_scan_result(terminal);
    }
}

uint32_t syna_adapt_scan(struct wpa_driver_scan_params *scan)
{
    if (scan == NULL) return (uint32_t) -1;

    /* Reset scan result counter for new scan */
    sc_result_cnt = 0;
    syna_sc_filter_scan_init(scan);

    mhd_wifi_scan_params_t params;
    memset(&params, 0, sizeof(params));

    /*
     * In wpa_supplicant's driver interface, num_ssids == 0 signals a passive
     * scan (no probe requests sent, listen for beacons only). See driver.h.
     */
    params.scan_type = (scan->num_ssids == 0) ? HOST_SCAN_TYPE_PASSIVE
                                              : HOST_SCAN_TYPE_ACTIVE;
    params.bss_type = HOST_BSS_TYPE_INFRASTRUCTURE;
    params.bssid = NULL;

    wpa_printf(MSG_DEBUG, "syna_adapt_scan: scan_type=%s (num_ssids=%zu)",
               (params.scan_type == HOST_SCAN_TYPE_PASSIVE) ? "PASSIVE" : "ACTIVE",
            scan->num_ssids);

    /* Copy all requested SSIDs into the ssids[] array */
    params.n_ssids = 0;
    for (size_t s = 0; s < scan->num_ssids; s++) {
        if (scan->ssids[s].ssid == NULL || scan->ssids[s].ssid_len == 0)
            continue;
        uint8_t len = (uint8_t)scan->ssids[s].ssid_len;
        if (len > SSID_NAME_SIZE)
            len = SSID_NAME_SIZE;
        params.ssids[params.n_ssids].length = len;
        memcpy(params.ssids[params.n_ssids].value, scan->ssids[s].ssid, len);
        wpa_printf(MSG_INFO, "Targeted scan for SSID: %.*s", len,
                   params.ssids[params.n_ssids].value);
        params.n_ssids++;
        if (params.n_ssids >= ARRAY_SIZE(params.ssids)) {
            wpa_printf(MSG_DEBUG, "syna_adapt_scan: max SSIDs reached at '%.*s', dropping remaining",
                       (int)scan->ssids[s].ssid_len, (const char *)scan->ssids[s].ssid);
            break;
        }
    }
    if (params.n_ssids == 0)
        wpa_printf(MSG_INFO, "Broadcast scan (no SSID specified)");

    /* Copy frequency list (MHz) into the mhd's freq_list[] array */
    params.n_freq_list = 0;

    if (scan->freqs) {
        int *freq = scan->freqs;
        for (uint32_t i = 0; i < (uint32_t)int_array_len(scan->freqs); i++, freq++) {
            params.freq_list[params.n_freq_list] = (uint16_t)*freq;
            params.n_freq_list++;
            wpa_printf(MSG_DEBUG, "escan params freq_list[%d] = %d MHz", i, *freq);

            if (params.n_freq_list >= ARRAY_SIZE(params.freq_list)) {
                wpa_printf(MSG_DEBUG, "syna_adapt_scan: max freq list reached at %d MHz, dropping remaining",
                           *freq);
                break;
            }
        }
    }

    params.extended_params = NULL;
    params.cb = (mhd_wifi_scan_callback_t)sup_adap_scan_cb;
    params.cb_context = NULL;
    params.interface = MHD_STA_INTERFACE;

    int32_t ret = mhd_wifi_api_scan(&params);
    return (uint32_t) ret;
}

/* When MHD sends scan results to the adaptor, it uses the internal
 * drv_syna_msg_t queue (supp_queue). This helper moves the list entries
 * into the global drv_scan_results_list and notifies supplicant that
 * results are available (supplicant will call drv_syna_get_scan_results()).
 */
int32_t sup_adap_handle_scan_result_cb(struct driver_syna_data *drv, drv_syna_msg_t *drv_msg)
{
    struct dl_list *_scan_result_list = NULL;
    drv_scan_result_t *res;

    if (!drv || !drv_msg) {
        wpa_printf(MSG_ERROR, "Adaptor: bad args to scan result handler");
        return -1;
    }

    /* drv_msg->data is expected to be a pointer to a dl_list of drv_scan_result_t
     * entries produced by the MHD side. Move entries into our global list.
     */
    _scan_result_list = (struct dl_list *)drv_msg->data;
    while ((res = dl_list_first(_scan_result_list, drv_scan_result_t, list)) != NULL) {
        dl_list_del(&res->list);
        dl_list_add_tail(&drv_scan_results_list, &res->list);
    }

    /* Notify supplicant: it should call drv_syna_get_scan_results() to fetch
     * the aggregated list.
     */
    wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS, NULL);
    return 0;
}

/**
 * Simple helper to convert center frequency (MHz) to primary channel.
 * Only covers common 2.4 GHz (1-13 +14) and lower 5 GHz band ranges.
 * Returns 0 if unmapped/unhandled.
 */
uint8_t convert_freq_to_channel(struct hostapd_freq_params *freq,  drv_syna_channel_info_t *params)
{
    params->bw = MHD_CHANSPEC_BW_20;

    if (freq->freq >= 2412 && freq->freq <= 2472 ) {
        params->channel = (uint8_t)((freq->freq - 2407) / 5); /* channel 1..13 */
        params->band = MHD_CHANSPEC_BAND_2G;
    }
    if (freq->freq == 2484) {
        params->channel = 14; /* special case */
        params->band = MHD_CHANSPEC_BAND_2G;
    }
    if (freq->freq >= 5000 && freq->freq <= 5900) {
        params->channel = (uint8_t)((freq->freq - 5000) / 5); /* approximate mapping */
        params->band = MHD_CHANSPEC_BAND_5G;
    }
    if (freq->freq >= 5955 && freq->freq <= 7125) {
        params->channel = (uint8_t)((freq->freq - 5950) / 5); /* approximate mapping */
        params->band = MHD_CHANSPEC_BAND_6G;
    }

    if (freq->bandwidth == 20)
        params->bw = MHD_CHANSPEC_BW_20;
    if (freq->bandwidth == 40)
        params->bw = MHD_CHANSPEC_BW_40;
    if (freq->bandwidth == 80)
        params->bw = MHD_CHANSPEC_BW_80;
    if (freq->bandwidth == 160)
        params->bw = MHD_CHANSPEC_BW_160;
    params->channel_2 = 0;
    return 0;
}

/* Process a single incoming MHD control message. For now we only handle
 * trigger-scan and scan-abort. The function consumes the top-level
 * msg->mhd_msg pointer by freeing it after handing it off; nested pointers
 * inside that payload are not handled here (legacy behavior).
 */
int32_t sup_adap_process_msg(sup_adap_msg_t *msg)
{
    if (!msg) 
        return -1;

    switch (msg->type) {
#ifdef CONFIG_AP
        case SUP_ADAP_CMD_SET_AP:
        {
            /* Map wpa_driver_ap_params to mhd_wifi_ap_params_t and call mhd_wifi_api_start_ap */
            struct wpa_driver_ap_params *ap = (struct wpa_driver_ap_params *) msg->msg;
            if (!ap) {
                wpa_printf(MSG_ERROR, "Adaptor: SET_AP with NULL params");
                break;
            }

            mhd_wifi_ap_params_t ap_params; memset(&ap_params, 0, sizeof(ap_params));
            drv_syna_channel_info_t channel; memset(&channel, 0, sizeof(channel));
            mhd_ssid_t ssid_obj; memset(&ssid_obj, 0, sizeof(ssid_obj));

            if (ap->ssid && ap->ssid_len && ap->ssid_len <= sizeof(ssid_obj.value)) {
                memcpy(ssid_obj.value, ap->ssid, ap->ssid_len);
                ssid_obj.length = (uint8_t) ap->ssid_len;
                ap_params.ssid = &ssid_obj;
            } else if (ap->ssid_len) {
                wpa_printf(MSG_WARNING, "Adaptor: SET_AP SSID length %u invalid", (unsigned)ap->ssid_len);
            }
            ap_params.interface = MHD_AP_INTERFACE;

            if (ap->freq && ap->freq->freq > 0) {
		struct hostapd_freq_params *freq = ap->freq;
                wpa_printf(MSG_ERROR,
                           "Adaptor: Set freq %d (ht_enabled=%d, vht_enabled=%d, he_enabled=%d, eht_enabled=%d, bandwidth=%d MHz, cf1=%d MHz, cf2=%d MHz)",
			   freq->freq, freq->ht_enabled, freq->vht_enabled,
			   freq->he_enabled, freq->eht_enabled, freq->bandwidth,
			   freq->center_freq1, freq->center_freq2);
                convert_freq_to_channel(ap->freq, &channel);
                ap_params.channel = channel.channel;
                ap_params.channel_2 = channel.channel_2;
                ap_params.band = channel.band;
                ap_params.bw = channel.bw;
            } else
                wpa_printf(MSG_ERROR, "Adaptor: SET_AP invalid frequency");

            map_ap_security(ap, &ap_params);
            ap_params.beacon_int = ap->beacon_int;
            ap_params.dtim_period = ap->dtim_period;
            ap_params.hidden = ap->hide_ssid ? 1 : 0;
            ap_params.mfp = 0; /* ieee80211w not mapped here */
            int has_sae = (ap->key_mgmt_suites & (WPA_KEY_MGMT_SAE | WPA_KEY_MGMT_FT_SAE |
				WPA_KEY_MGMT_SAE_EXT_KEY | WPA_KEY_MGMT_FT_SAE_EXT_KEY)) != 0;
            if (has_sae) {
                if ((ap->pairwise_ciphers & WPA_CIPHER_GCMP_256) && (ap->group_cipher & WPA_CIPHER_GCMP_256))
                    ap_params.security = HOST_SECURITY_WPA3_SAE_GCMP_256_PSK;
                else
                    ap_params.security = HOST_SECURITY_WPA3_SAE_PSK;
                ap_params.mfp = 2; /* require MFP for WPA3 */
            }
            else
                ap_params.security = ap->privacy ? HOST_SECURITY_WPA2_AES_PSK : HOST_SECURITY_OPEN;
            ap_params.enable_owe = 0;

            wpa_printf(MSG_DEBUG, "Adaptor: SET_AP ssid='%.*s' len=%d channel=%d beacon_int=%d",
                ssid_obj.length, ssid_obj.value, ssid_obj.length, ap_params.channel, ap_params.beacon_int);

            if (sup_adap_register_ap_event_handler() != 0) {
                wpa_printf(MSG_ERROR, "Adaptor: failed to register AP event handler");
            }

            int32_t rc = mhd_wifi_api_start_ap(&ap_params);
            if (rc != 0) {
                if (msg->drv_priv) {
                    ((driver_syna_data_t *)msg->drv_priv)->ap_started = 0;
                }
                wpa_printf(MSG_ERROR, "Adaptor: mhd_wifi_api_start_ap failed rc=%ld", (long)rc);
                wpa_supplicant_event(msg->cb_ctx, EVENT_INTERFACE_UNAVAILABLE, NULL);
            } else {
                if (msg->drv_priv) {
                    ((driver_syna_data_t *)msg->drv_priv)->ap_started = 1;
                }
                wpa_printf(MSG_DEBUG, "Adaptor: mhd_wifi_api_start_ap dispatched");
            }
            break;
        }
        case SUP_ADAP_CMD_SET_STA_FLAG:
        {
            uint8_t *ptr = msg->msg;
            uint32_t *flag = (uint32_t *)(ptr + ETH_ALEN);
            wpa_printf(MSG_DEBUG, "Adaptor: Set STA FLAGS %02x:%02x:%02x:%02x:%02x:%02x as 0x%x\n",
			    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], *flag);
            //mhd_wifi_api_set_sta_flag(ptr, *flag);
        }
#endif /* CONFIG_AP */
        case SUP_ADAP_CMD_REQ_WIPHY:
        {
            break;
        }

        case SUP_ADAP_CMD_SCAN_ABORT:
        {   
            wpa_printf(MSG_DEBUG, "Adaptor: Received MHD_CMD_SCAN_ABORT");
            //syna_adapt_scan_abort();
            break;
        }

        case SUP_ADAP_CMD_ASSOC_REQ:
        {
            /* Map wpa_driver_associate_params to minimal mhd_wifi_join_params_t
             * We extract: SSID, BSSID (if specified), security type, passphrase, MFP.
             * For now, infer host_security_t from pairwise/group cipher + key mgmt heuristics.
             */
            struct wpa_driver_associate_params *ap = (struct wpa_driver_associate_params *) msg->msg;
            if (!ap) {
                wpa_printf(MSG_ERROR, "Adaptor: ASSOC_REQ with NULL params");
                break;
            }

            mhd_wifi_join_params_t join; 
            memset(&join, 0, sizeof(join));

            /* SSID */
            mhd_ssid_t ssid_obj; 
            memset(&ssid_obj, 0, sizeof(ssid_obj));
            
            if (ap->ssid && ap->ssid_len && ap->ssid_len <= sizeof(ssid_obj.value)) {
                memcpy(ssid_obj.value, ap->ssid, ap->ssid_len);
                ssid_obj.length = (uint8_t) ap->ssid_len;
                join.ssid = &ssid_obj;
            } else if (ap->ssid_len) {
                wpa_printf(MSG_WARNING, "Adaptor: SSID length %u invalid", (unsigned)ap->ssid_len);
            }

            /* BSSID (exact) preference */
            mhd_mac_t bssid_obj; memset(&bssid_obj, 0, sizeof(bssid_obj));
            const uint8_t *target_bssid = ap->bssid ? ap->bssid : ap->bssid_hint;
            if (target_bssid && !is_zero_ether_addr(target_bssid)) {
                memcpy(bssid_obj.octet, target_bssid, sizeof(bssid_obj.octet));
                join.bssid = &bssid_obj;
            }
            wpa_printf(MSG_DEBUG, "Adaptor: assoc bssid "MACDBG" source=%s present=%d",
                       MAC2STRDBG(bssid_obj.octet),
                       ap->bssid ? "bssid" : (ap->bssid_hint ? "bssid_hint" : "none"),
                       join.bssid ? 1 : 0);

            struct hostapd_freq_params freq = ap->freq;
            if (freq.freq <= 0 && ap->freq_hint > 0)
                freq.freq = ap->freq_hint;

            if (freq.freq > 0) {
                assoc_info.freq = freq.freq;
                drv_syna_channel_info_t channel_info;
                memset(&channel_info, 0, sizeof(channel_info));
                convert_freq_to_channel(&freq, &channel_info);
                join.primary_channel = channel_info.channel;
                join.band            = channel_info.band;
                join.bw              = channel_info.bw;
                wpa_printf(MSG_DEBUG,
                           "Adaptor: assoc freq=%d src_freq=%d freq_hint=%d channel=%u band=%u bw=%u",
                           freq.freq, ap->freq.freq, ap->freq_hint,
                           join.primary_channel, join.band, join.bw);
            } else {
                assoc_info.freq = 0;
                wpa_printf(MSG_DEBUG,
                           "Adaptor: assoc freq unavailable src_freq=%d freq_hint=%d",
                           ap->freq.freq, ap->freq_hint);
            }

            /* WPA information element for (Re)Association Request */
            mhd_wpa_ie_t wpaie_obj; memset(&wpaie_obj, 0, sizeof(wpaie_obj));
            if (ap->wpa_ie_len) {
                if (!ap->wpa_ie || ap->wpa_ie_len > UINT8_MAX) {
                    wpa_printf(MSG_ERROR, "Adaptor: invalid WPA IE length %zu", ap->wpa_ie_len);
                    break;
                }
                memcpy(wpaie_obj.wpa_ie, ap->wpa_ie, ap->wpa_ie_len);
                wpaie_obj.wpa_ie_len = (uint8_t) ap->wpa_ie_len;
                join.wpaie = &wpaie_obj;
            }
            map_sta_security(ap, &join);

            /* Management Frame Protection */
            switch (ap->mgmt_frame_protection) {
                case MGMT_FRAME_PROTECTION_REQUIRED: join.mfp = 2; break; /* strict */
                case MGMT_FRAME_PROTECTION_OPTIONAL: join.mfp = 1; break; /* capable */
                default: join.mfp = 0; break; /* disabled */
            }
            join.bss_type = HOST_BSS_TYPE_INFRASTRUCTURE;
            join.interface = MHD_STA_INTERFACE;
            join.disable_eht = ap->disable_eht ? 1 : 0;

            int32_t rc = mhd_wifi_api_join(&join);
            if (join.pwd)
                os_free((void *)join.pwd);
            if (rc != 0) {
                wpa_printf(MSG_ERROR, "Adaptor: mhd_wifi_api_join failed rc=%ld", (long)rc);
            } else {
                wpa_printf(MSG_DEBUG, "Adaptor: mhd_wifi_api_join dispatched");
            }
            break;
        }

        case SUP_ADAP_CMD_NEW_KEY:
        {
            mhd_wifi_key_params_t *kparams = (mhd_wifi_key_params_t *) msg->msg;
            int32_t rc = mhd_wifi_api_set_or_del_key(kparams);
            
            /* Equivalent to nl80211's key-delete error suppression:
             *   if ((ret == -ENOENT || ret == -ENOLINK) && alg == WPA_ALG_NONE) ret = 0;
             * When deleting a key (WPA_ALG_NONE), a firmware "key not found" error
             * (BCME_BADKEYIDX/BCME_NOTFOUND -> rc=-3) can be safely suppressed —
             * the key is no longer in use, so the error is harmless.
             * Install failures (alg != WPA_ALG_NONE) are always propagated.
             */
            if (rc == -3 && kparams->wpa_alg == WPA_ALG_NONE) {
                wpa_printf(MSG_DEBUG, "Adaptor: mhd_wifi_api_set_or_del_key delete key not found ignored (idx=%d)",
                           kparams->key_index);
            }
            else if (rc != 0) {
                wpa_printf(MSG_ERROR, "Adaptor: mhd_wifi_api_set_or_del_key failed rc=%ld", (long)rc);
            } else {
                wpa_printf(MSG_DEBUG, "Adaptor: mhd_wifi_api_set_or_del_key dispatched");
            }
            break;
        }

        case SUP_ADAP_CMD_DEL_KEY:
        {
            
            break;
        }

        case SUP_ADAP_CMD_DEAUTHENTICATE:
        {
            mhd_interface_t *iface = (mhd_interface_t *) msg->msg;
            if (!iface) {
                wpa_printf(MSG_ERROR, "Adaptor: DEAUTHENTICATE with NULL interface");
                break;
            }

            wpa_printf(MSG_INFO, "Adaptor: Received DEAUTHENTICATE for interface %d", *iface);
            int32_t rc = mhd_wifi_api_disconnect(*iface);
            if (rc != 0) {
                wpa_printf(MSG_ERROR, "Adaptor: mhd_wifi_api_disconnect failed rc=%ld", (long)rc);
            } else {
                wpa_printf(MSG_DEBUG, "Adaptor: mhd_wifi_api_disconnect dispatched");
            }
            break;
        }

        case SUP_ADAP_DATA_EAPOL_PKT:
        {
            break;
        }


        ///////////////////Events handling ///////////////////
        case SUP_ADAP_EVT_ASSOC_IND:
        {
            /* SoftAP: External STA associated - forward to supplicant */
            uint8_t *sta_addr = (uint8_t *)msg->msg;
            if (sta_addr) {
                wpa_printf(MSG_DEBUG, "Adaptor: Processing ASSOC_IND for STA %02x:%02x:%02x:%02x:%02x:%02x",
                           sta_addr[0], sta_addr[1], sta_addr[2], sta_addr[3], sta_addr[4], sta_addr[5]);
                
                /* Create drv_syna event to notify supplicant of new STA association */
                drv_syna_msg_t drv_msg;
                os_memset(&drv_msg, 0, sizeof(drv_msg));
                drv_msg.type = DRV_SYNA_EVENT_ASSOC;
                
                /* Allocate assoc info structure */
                struct drv_assoc_info *ainfo = os_zalloc(sizeof(struct drv_assoc_info));
                if (ainfo) {
                    /* Allocate new memory for MAC address and copy the data */
                    ainfo->addr = os_zalloc(ETH_ALEN);
                    if (ainfo->addr) {
                        memcpy((u8 *)ainfo->addr, sta_addr, ETH_ALEN);
                        wpa_printf(MSG_DEBUG, "Adaptor: ASSOC_IND STA addr %02x:%02x:%02x:%02x:%02x:%02x",
                               ainfo->addr[0], ainfo->addr[1], ainfo->addr[2],
                               ainfo->addr[3], ainfo->addr[4], ainfo->addr[5]);
                    } else {
                        wpa_printf(MSG_ERROR, "Adaptor: ASSOC_IND: Failed to allocate ainfo->addr");
                    }
                    /* Transfer staged assoc request IEs to the event message
                     * so handle_event_assoc() can free them exactly once.
                     */
                    ainfo->req_ies = assoc_info.req_ies;
                    ainfo->req_ies_len = assoc_info.req_ies_len;
                    assoc_info.req_ies = NULL;
                    assoc_info.req_ies_len = 0;
                    ainfo->resp_ies = NULL;
                    ainfo->resp_ies_len = 0;
                }
                drv_msg.data = ainfo;
                /* free ainfo (and its addr child) on send failure.
                 * req_ies aliases assoc_info and must NOT be freed here.
                 */
                if (sup_adap_send_supp_msg(&drv_msg) != 0) {
                    if (ainfo) {
                        if (ainfo->addr)
                            os_free((void *)ainfo->addr);
                        if (ainfo->req_ies)
                            os_free((void *)ainfo->req_ies);
                        os_free(ainfo);
                    }
                }
                /* Note: sta_addr will be freed at the end of sup_adap_process_msg */
            }
            break;
        }
        case SUP_ADAP_EVT_SET_SSID:
            /* Control-plane association completed (WLC_E_SET_SSID). Treat this
             * as full association for supplicant purposes (no LINK deferral).
             */
            if (!g_sup_state.assoc_indicated) {
                drv_assoc_info_t *ainfo = os_zalloc(sizeof(*ainfo));
                if (ainfo) {
                    ainfo->reassoc = 0;
                    ainfo->freq = assoc_info.freq;
                    wpa_printf(MSG_DEBUG, "Adaptor: Association freq=%u (from drv assoc params)",
                               ainfo->freq);
                }
                drv_syna_msg_t drv_msg;
                os_memset(&drv_msg, 0, sizeof(drv_msg));
                drv_msg.type = DRV_SYNA_EVENT_ASSOC;
                drv_msg.data = ainfo;
                /* free ainfo on send failure (no children allocated here). */
                if (sup_adap_send_supp_msg(&drv_msg) != 0) {
                    if (ainfo)
                        os_free(ainfo);
                }
                g_sup_state.assoc_indicated = 1;
                wpa_printf(MSG_DEBUG, "Adaptor: Association forwarded on SET_SSID");
            } else {
                wpa_printf(MSG_DEBUG, "Adaptor: SET_SSID assoc already indicated, skipping");
            }
            break;
        case SUP_ADAP_EVT_DISASSOC:
        {
            /* Stage 2: translate disassociation into driver_syna DISASSOC event.
             * If Stage 1 already provided disassoc info, transfer ownership.
             */
            drv_disassoc_info_t *dinfo = (drv_disassoc_info_t *) msg->msg;
            if (dinfo) {
                msg->msg = NULL; /* ownership moved to drv_msg.data */
            } else {
                dinfo = os_zalloc(sizeof(*dinfo));
                if (dinfo) {
                    dinfo->reason_code = 0;
                    dinfo->locally_generated = 0;
                } else {
                    wpa_printf(MSG_WARNING, "Adaptor: alloc drv_disassoc_info_t failed (evt->disassoc)");
                }
            }

            if (dinfo) {
                wpa_printf(MSG_DEBUG,
                           "Adaptor: DISASSOC reason=%u addr=%02x:%02x:%02x:%02x:%02x:%02x ie_len=%u",
                           (unsigned) dinfo->reason_code,
                           dinfo->addr ? dinfo->addr[0] : 0, dinfo->addr ? dinfo->addr[1] : 0,
                           dinfo->addr ? dinfo->addr[2] : 0, dinfo->addr ? dinfo->addr[3] : 0,
                           dinfo->addr ? dinfo->addr[4] : 0, dinfo->addr ? dinfo->addr[5] : 0,
                           (unsigned) dinfo->ie_len);
            }

            drv_syna_msg_t drv_msg;
            os_memset(&drv_msg, 0, sizeof(drv_msg));
            drv_msg.type = DRV_SYNA_EVENT_DISASSOC;
            drv_msg.data = dinfo; /* freed by driver handler */
            sup_adap_send_supp_msg(&drv_msg);
            g_sup_state.assoc_indicated = 0; /* reset for next connection */
            break;
        }

        case SUP_ADAP_EVT_DEAUTH:
        {
            drv_deauth_info_t *dinfo = (drv_deauth_info_t *) msg->msg;
            if (dinfo) {
                msg->msg = NULL; /* ownership moved to drv_msg.data */
            } else {
                dinfo = os_zalloc(sizeof(*dinfo));
                if (dinfo) {
                    dinfo->reason_code = 0;
                    dinfo->locally_generated = 0;
                } else {
                    wpa_printf(MSG_WARNING, "Adaptor: alloc drv_deauth_info_t failed (evt->deauth)");
                }
            }

            if (dinfo) {
                wpa_printf(MSG_DEBUG,
                           "Adaptor: DEAUTH reason=%u addr=%02x:%02x:%02x:%02x:%02x:%02x ie_len=%u",
                           (unsigned) dinfo->reason_code,
                           dinfo->addr ? dinfo->addr[0] : 0, dinfo->addr ? dinfo->addr[1] : 0,
                           dinfo->addr ? dinfo->addr[2] : 0, dinfo->addr ? dinfo->addr[3] : 0,
                           dinfo->addr ? dinfo->addr[4] : 0, dinfo->addr ? dinfo->addr[5] : 0,
                           (unsigned) dinfo->ie_len);
            }

            drv_syna_msg_t drv_msg;
            os_memset(&drv_msg, 0, sizeof(drv_msg));
            drv_msg.type = DRV_SYNA_EVENT_DEAUTH;
            drv_msg.data = dinfo; /* freed by driver handler */
            /* ownership transferred from msg->msg above; if the
             * forward send fails, free dinfo and its children here.
             */
            if (sup_adap_send_supp_msg(&drv_msg) != 0) {
                if (dinfo) {
                    if (dinfo->ie)
                        os_free((void *)dinfo->ie);
                    if (dinfo->addr)
                        os_free((void *)dinfo->addr);
                    os_free(dinfo);
                }
            }
            g_sup_state.assoc_indicated = 0; /* reset for next connection */
            break;
        }
        case SUP_ADAP_CMD_SET_SUPP_PORT:
        {
            if (!msg->msg) {
                wpa_printf(MSG_ERROR, "Adaptor: SET_SUPP_PORT with NULL payload");
                break;
            }

            mhd_interface_t *iface = (mhd_interface_t *) msg->msg;
            const mhd_mac_t *mac_addr = (mhd_mac_t*)((uint8_t *) msg->msg +
                                        sizeof(mhd_interface_t));
            uint32_t *mask = (uint32_t*) ((uint8_t*)mac_addr + sizeof(mhd_mac_t));
            uint32_t *flags = (uint32_t*) ((uint8_t*)mask + sizeof(*mask));
            int32_t rc = mhd_wifi_api_set_supp_port(*iface, mac_addr, *mask, *flags);

            if (rc != 0) {
                wpa_printf(MSG_ERROR, "Adaptor: mhd_wifi_api_set_supp_port failed rc=%ld", (long)rc);
            } else {
                wpa_printf(MSG_DEBUG, "Adaptor: mhd_wifi_api_set_supp_port dispatched");
            }

            break;
        }
        default:
            wpa_printf(MSG_DEBUG, "Adaptor: Ignoring msg type %d", msg->type);
            break;
    }

    /* Free only the top-level payload pointer; handling of nested allocations
     * (e.g., scan->freqs) is left to the sender/receiver convention.
     */
    if (msg->msg) {
        os_free(msg->msg);
        msg->msg = NULL;
    }

    return 0;
}

#ifndef ZEPHYR_OS
int sup_adap_register_mhd_event_handler(mhd_interface_t interface)
{
    return sup_adap_register_sta_event_handler(interface);
}
#endif
