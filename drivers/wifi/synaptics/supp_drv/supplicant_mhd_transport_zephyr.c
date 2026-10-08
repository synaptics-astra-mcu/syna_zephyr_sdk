/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#ifdef ZEPHYR_OS

#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

#include "includes.h"
#include "sup_adap_msg.h"
#include "supplicant_mhd_event.h"
#include "supplicant_mhd_transport.h"
#include "supplicant_mhd_transport_zephyr.h"

#ifndef WL_CHANSPEC_BAND_MASK
#define WL_CHANSPEC_BAND_MASK      0xc000
#endif

#ifndef WL_CHANSPEC_BAND_SHIFT
#define WL_CHANSPEC_BAND_SHIFT     14
#endif

static driver_syna_data_t *zephyr_drv;
static bool zephyr_scan_results_initialized;
static struct net_if *zephyr_scan_iface;
static scan_result_cb_t zephyr_scan_result_cb;
static uint8_t zephyr_scan_seen_bssids[MAX_SCAN_RESULTS][ETH_ALEN];
static size_t zephyr_scan_seen_count;

extern uint32_t sc_result_cnt;
extern uint32_t syna_adapt_scan(struct wpa_driver_scan_params *scan);

static void sup_adap_scan_cb(mhd_ifidx_t ifidx, void *data, uint32_t status,
                             void *context)
{
    ARG_UNUSED(ifidx);
    ARG_UNUSED(context);

    if (sup_adap_transport_handle_zephyr_scan_event(data, status))
        return;

    if (status == HOST_SCAN_COMPLETED)
        wpa_printf(MSG_INFO, "sup_adap_scan_cb: scan completed (per-driver)");
    else if (status == HOST_SCAN_ABORTED)
        wpa_printf(MSG_INFO, "sup_adap_scan_cb: scan aborted (per-driver)");
}

static uint16_t syna_zephyr_channel_to_frequency(uint8_t band, uint8_t channel)
{
    switch (band) {
    case WIFI_FREQ_BAND_2_4_GHZ:
        return channel == 14 ? 2484 : 2407 + (5 * channel);
    case WIFI_FREQ_BAND_5_GHZ:
        return 5000 + (5 * channel);
    case WIFI_FREQ_BAND_6_GHZ:
        return 5950 + (5 * channel);
    default:
        return 0;
    }
}

/* Scan results carry the advertised RSN/WPA IEs, not a host_security_t.
 * Decode the AKM list rather than guessing the security from the privacy bit.
 */
static enum wifi_security_type syna_zephyr_scan_security(const wl_bss_info_t *bi)
{
    const uint8_t *ies, *ie, *pos, *end;
    size_t remaining, ie_len, count;
    bool has_security_ie = false, has_wpa_psk = false, has_rsn_psk = false;
    bool has_psk_sha256 = false;
    bool has_sae = false, has_ft_sae = false, has_sae_ext = false;
    bool has_sae_h2e = false;
    bool has_eap = false, has_ft_psk = false, has_dpp = false;

    if (!(bi->capability & BIT(4)))
        return WIFI_SECURITY_TYPE_NONE;

    /* A malformed record must never let the IE walk escape the BSS buffer. */
    if (bi->length < sizeof(*bi) || bi->ie_offset < sizeof(*bi) ||
        bi->ie_offset > bi->length || bi->ie_length > bi->length - bi->ie_offset)
        return WIFI_SECURITY_TYPE_UNKNOWN;

    ies = (const uint8_t *)bi + bi->ie_offset;
    remaining = bi->ie_length;
    while (remaining >= 2) {
        ie = ies;
        ie_len = ie[1];
        if (ie_len > remaining - 2)
            return WIFI_SECURITY_TYPE_UNKNOWN;
        ies += ie_len + 2;
        remaining -= ie_len + 2;

        /* RSNXE (244) advertises SAE H2E in bit 5 of its first octet.
         * This bit indicates H2E support, not whether HNP is also supported.
         */
        if (ie[0] == 244) {
            if (ie_len >= 1 && (ie[2] & BIT(5)))
                has_sae_h2e = true;
            continue;
        }

        /* RSN (48) or WPA vendor IE (00:50:f2:01). */
        if (ie[0] == 48) {
            has_security_ie = true;
            pos = ie + 2;
        } else if (ie[0] == 221 && ie_len >= 4 &&
                   ie[2] == 0x00 && ie[3] == 0x50 &&
                   ie[4] == 0xf2 && ie[5] == 0x01) {
            has_security_ie = true;
            pos = ie + 6;
        } else {
            continue;
        }

        end = ie + 2 + ie_len;
        /* Version, group cipher, pairwise count/list, AKM count/list. */
        if ((size_t)(end - pos) < 8 || pos[0] != 1 || pos[1] != 0)
            return WIFI_SECURITY_TYPE_UNKNOWN;
        pos += 6;
        count = pos[0] | ((size_t)pos[1] << 8);
        pos += 2;
        if (count > (size_t)(end - pos) / 4)
            return WIFI_SECURITY_TYPE_UNKNOWN;
        pos += count * 4;
        if ((size_t)(end - pos) < 2)
            return WIFI_SECURITY_TYPE_UNKNOWN;
        count = pos[0] | ((size_t)pos[1] << 8);
        pos += 2;
        if (count > (size_t)(end - pos) / 4)
            return WIFI_SECURITY_TYPE_UNKNOWN;

        for (size_t index = 0; index < count; index++, pos += 4) {
            if (ie[0] == 48 && pos[0] == 0x00 && pos[1] == 0x0f &&
                pos[2] == 0xac) {
                switch (pos[3]) {
                case 1: case 3: case 5: case 11: case 12: case 13:
                    has_eap = true;
                    break;
                case 2: has_rsn_psk = true; break;
                case 4: has_ft_psk = true; break;
                case 6: has_psk_sha256 = true; break;
                case 8: has_sae = true; break;
                case 9: has_ft_sae = true; break;
                case 24: case 25: has_sae_ext = true; break;
                default: break;
                }
            } else if (ie[0] == 48 && pos[0] == 0x50 &&
                       pos[1] == 0x6f && pos[2] == 0x9a && pos[3] == 2) {
                has_dpp = true;
            } else if (ie[0] == 221 && pos[0] == 0x00 &&
                       pos[1] == 0x50 && pos[2] == 0xf2) {
                if (pos[3] == 1)
                    has_eap = true;
                else if (pos[3] == 2)
                    has_wpa_psk = true;
            }
        }
    }

    if (remaining)
        return WIFI_SECURITY_TYPE_UNKNOWN;
    if (has_sae_ext)
        return WIFI_SECURITY_TYPE_SAE_EXT_KEY;
    if ((has_sae || has_ft_sae) &&
        (has_wpa_psk || has_rsn_psk || has_psk_sha256 || has_ft_psk))
        return WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL;
    if (has_sae)
        return has_sae_h2e ? WIFI_SECURITY_TYPE_SAE_H2E : WIFI_SECURITY_TYPE_SAE_HNP;
    if (has_ft_sae)
        return WIFI_SECURITY_TYPE_FT_SAE;
    if (has_dpp)
        return WIFI_SECURITY_TYPE_DPP;
    if (has_eap)
        return WIFI_SECURITY_TYPE_EAP;
    if (has_psk_sha256)
        return WIFI_SECURITY_TYPE_PSK_SHA256;
    if (has_ft_psk)
        return WIFI_SECURITY_TYPE_FT_PSK;
    if (has_rsn_psk)
        return WIFI_SECURITY_TYPE_PSK;
    if (has_wpa_psk)
        return WIFI_SECURITY_TYPE_WPA_PSK;
    return has_security_ie ? WIFI_SECURITY_TYPE_UNKNOWN : WIFI_SECURITY_TYPE_WEP;
}

int send_msg_to_sup_adap(sup_adap_msg_t *msg)
{
    return sup_adap_process_msg(msg);
}

int sup_adap_register_ap_event_handler(void)
{
    size_t event_count = 0;
    const uint32_t *event_list = sup_adap_transport_ap_events(&event_count);
    mhd_wifi_event_handler_params_t eh_ap;

    eh_ap.event_list = event_list;
    eh_ap.event_count = event_count;
    eh_ap.event_cb = mhd_wifi_common_events_handler;
    eh_ap.user_ctx = sup_adap_transport_state();
    eh_ap.interface = MHD_AP_INTERFACE;

    if (mhd_wifi_api_set_event_handler(&eh_ap) != 0)
        return -1;

    return 0;
}

int sup_adap_send_supp_msg(void *msg)
{
    if (!zephyr_drv)
        return -1;

    return drv_syna_process_msg(zephyr_drv, msg);
}

void sup_adap_transport_bind_zephyr_drv(driver_syna_data_t *drv)
{
    zephyr_drv = drv;
}

void sup_adap_transport_unbind_zephyr_drv(driver_syna_data_t *drv)
{
    if (zephyr_drv == drv)
        zephyr_drv = NULL;
}

bool sup_adap_transport_handle_zephyr_scan_event(void *data, uint32_t status)
{
    wl_bss_info_t *bi = (wl_bss_info_t *)data;

    if (!zephyr_scan_result_cb)
        return false;

    if (status == HOST_SCAN_INCOMPLETE && bi) {
        struct wifi_scan_result result = {0};
        chanspec_band_t band =
            (chanspec_band_t)(CHSPEC_BAND(bi->chanspec) >> WL_CHANSPEC_BAND_SHIFT);

        for (size_t index = 0; index < zephyr_scan_seen_count; index++) {
            if (os_memcmp(zephyr_scan_seen_bssids[index], &bi->BSSID,
                          ETH_ALEN) == 0)
                return true;
        }

        if (zephyr_scan_seen_count >= ARRAY_SIZE(zephyr_scan_seen_bssids))
            return true;

        os_memcpy(zephyr_scan_seen_bssids[zephyr_scan_seen_count++],
                  &bi->BSSID, ETH_ALEN);

        result.ssid_length = MIN(bi->SSID_len, WIFI_SSID_MAX_LEN);
        os_memcpy(result.ssid, bi->SSID, result.ssid_length);
        result.channel = mhd_chspec_primary20_chan(bi->chanspec);
        switch (band) {
        case MHD_CHANSPEC_BAND_2G:
            result.band = WIFI_FREQ_BAND_2_4_GHZ;
            break;
        case MHD_CHANSPEC_BAND_5G:
            result.band = WIFI_FREQ_BAND_5_GHZ;
            break;
        case MHD_CHANSPEC_BAND_6G:
            result.band = WIFI_FREQ_BAND_6_GHZ;
            break;
        default:
            result.band = WIFI_FREQ_BAND_UNKNOWN;
            break;
        }
        result.security = syna_zephyr_scan_security(bi);
        result.rssi = CLAMP(bi->RSSI, INT8_MIN, INT8_MAX);
        result.mac_length = ETH_ALEN;
        os_memcpy(result.mac, &bi->BSSID, ETH_ALEN);
        zephyr_scan_result_cb(zephyr_scan_iface, 0, &result);
        return true;
    }

    scan_result_cb_t cb = zephyr_scan_result_cb;
    struct net_if *iface = zephyr_scan_iface;

    zephyr_scan_result_cb = NULL;
    zephyr_scan_iface = NULL;
    cb(iface, status == HOST_SCAN_COMPLETED ? 0 : -ECANCELED, NULL);
    return true;
}

int syna_adapt_zephyr_scan(struct net_if *iface, struct wifi_scan_params *scan,
                           scan_result_cb_t cb)
{
    mhd_wifi_scan_params_t params = {0};
    size_t index;
    int32_t ret;

    if (!iface || !cb)
        return -EINVAL;
    if (zephyr_scan_result_cb)
        return -EINPROGRESS;

    params.interface = MHD_STA_INTERFACE;
    params.scan_type = (!scan || scan->scan_type == WIFI_SCAN_TYPE_ACTIVE) ?
        HOST_SCAN_TYPE_ACTIVE : HOST_SCAN_TYPE_PASSIVE;
    params.bss_type = HOST_BSS_TYPE_INFRASTRUCTURE;
    params.cb = (mhd_wifi_scan_callback_t)sup_adap_scan_cb;

    if (scan) {
        for (index = 0;
             index < ARRAY_SIZE(scan->ssids) && scan->ssids[index] &&
             params.n_ssids < ARRAY_SIZE(params.ssids);
             index++) {
            size_t length = MIN(strlen(scan->ssids[index]),
                                sizeof(params.ssids[0].value));

            params.ssids[params.n_ssids].length = length;
            os_memcpy(params.ssids[params.n_ssids].value, scan->ssids[index],
                      length);
            params.n_ssids++;
        }

        for (index = 0; index < ARRAY_SIZE(scan->band_chan); index++) {
            uint16_t frequency;

            if (!scan->band_chan[index].channel)
                break;
            frequency = syna_zephyr_channel_to_frequency(scan->band_chan[index].band,
                                                         scan->band_chan[index].channel);
            if (frequency && params.n_freq_list < ARRAY_SIZE(params.freq_list))
                params.freq_list[params.n_freq_list++] = frequency;
        }
    }

    zephyr_scan_seen_count = 0;
    zephyr_scan_iface = iface;
    zephyr_scan_result_cb = cb;
    ret = mhd_wifi_api_scan(&params);
    if (ret != MHD_SUCCESS) {
        zephyr_scan_result_cb = NULL;
        zephyr_scan_iface = NULL;
        return -EIO;
    }

    return 0;
}

int sup_adap_register_mhd_event_handler(driver_syna_data_t *drv,
                                        mhd_interface_t interface)
{
    if (!drv)
        return -1;

    if (!zephyr_scan_results_initialized) {
        dl_list_init(&drv_scan_results_list);
        zephyr_scan_results_initialized = true;
    }

    if (sup_adap_register_sta_event_handler(interface) != 0) {
        (void)mhd_wifi_api_unset_event_handler(interface);
        return -1;
    }

    sup_adap_transport_bind_zephyr_drv(drv);
    wpa_printf(MSG_DEBUG, "Adaptor: registered MHD event handler (if=%d)",
               interface);
    return 0;
}

int sup_adap_unregister_mhd_event_handler(driver_syna_data_t *drv,
                                          mhd_interface_t interface)
{
    mhd_result_t rc;

    sup_adap_transport_unbind_zephyr_drv(drv);

    rc = mhd_wifi_api_unset_event_handler(interface);
    if (rc != MHD_SUCCESS && rc != MHD_NOTFOUND) {
        wpa_printf(MSG_ERROR,
                   "Adaptor: failed to unregister MHD event handler (if=%d rc=%ld)",
                   interface, (long)rc);
        return -1;
    }

    return 0;
}

#endif
