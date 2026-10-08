/**
 * Copyright (c) 2026 Synaptics Incorporated
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file syna_wifi.c
 * supplicant <--> driver 
 */

#include "includes.h"
#include "common.h"
#include "drivers/driver.h"

#include "supp_main.h"
#include "sup_adap_msg.h"
#include "syna_wifi.h"
#include "driver_syna_platform.h"
#include "syna_wifi_msg.h"
#include "common/ieee802_11_common.h"
#include "mhd_wifi_api.h" /* core Wi-Fi API */
#include "hostapd.h"
#include "common/hw_features_common.h"
#include "wpa_supplicant_i.h"
#include "bss.h"

void drv_sup_adap_message_receiver(void *eloop_ctx, void *user_ctx);
/* Forward declaration for the Zephyr driver ops table and init cleanup. */
void driver_syna_deinit(void *priv);
/* Forward declaration for capability fetch used by hapd_init before its definition */
static int driver_syna_capa(driver_syna_data_t *drv);
/* The adaptor code populates a global list of drv_scan_result_t entries which
 * should be consumed by drv_syna_get_scan_results(). Declare it here as
 * extern so this file can access and drain it.
 */
extern struct dl_list drv_scan_results_list;

/* Unified logging macro so all driver messages share prefix and get timestamps */
#ifndef SYNA_DRV_LOG
#define SYNA_DRV_LOG(level, fmt, ...) \
    wpa_printf(level, "DRV_SYNA: " fmt, ##__VA_ARGS__)
#endif

#ifndef ZEPHYR_OS
static int drv_syna_read_own_addr(mhd_interface_t interface, u8 *own_addr,
                                  const char *reason)
{
    mhd_mac_t mac;

    if (!own_addr ||
        mhd_wifi_api_get_mac_address(interface, &mac) != MHD_SUCCESS) {
        SYNA_DRV_LOG(MSG_ERROR, "%s failed to get MAC for mhd_ifidx=%d",
                     reason ? reason : "role", interface);
        return -1;
    }

    os_memcpy(own_addr, mac.octet, ETH_ALEN);
    return 0;
}

static void drv_syna_apply_own_addr(driver_syna_data_t *drv,
                                    const u8 *own_addr,
                                    const char *reason)
{
    struct wpa_supplicant *wpa_s;
    bool changed;

    if (!drv || !own_addr)
        return;

    changed = !drv->own_addr_valid ||
              os_memcmp(drv->own_addr, own_addr, ETH_ALEN) != 0;
    os_memcpy(drv->own_addr, own_addr, ETH_ALEN);
    drv->own_addr_valid = true;

    if (drv->ctx) {
        wpa_s = drv->ctx;
        os_memcpy(wpa_s->own_addr, own_addr, ETH_ALEN);
        if (wpa_s->wpa)
            wpa_sm_set_own_addr(wpa_s->wpa, wpa_s->own_addr);
    }

    if (drv->hapd)
        os_memcpy(drv->hapd->own_addr, own_addr, ETH_ALEN);

    if (changed) {
        SYNA_DRV_LOG(MSG_INFO, "%s own_addr=" MACSTR " ifname=%s ifidx=%d",
                     reason ? reason : "role", MAC2STR(drv->own_addr),
                     drv->ifname, drv->mhd_int);
    }
}

static int drv_syna_refresh_own_addr(driver_syna_data_t *drv,
                                     mhd_interface_t interface,
                                     const char *reason)
{
    u8 own_addr[ETH_ALEN];

    if (drv_syna_read_own_addr(interface, own_addr, reason) != 0)
        return -1;

    drv_syna_apply_own_addr(drv, own_addr, reason);
    return 0;
}

static int drv_syna_set_l2_eapol_interface(driver_syna_data_t *drv,
					   mhd_interface_t interface)
{
	struct wpa_supplicant *wpa_s;
	mhd_interface_t old_interface;

	if (!drv || !drv->ctx)
		return 0;

	wpa_s = drv->ctx;
	old_interface = drv->mhd_int;
	if (wpa_s->l2 &&
	    l2_packet_set_mhd_interface(wpa_s->l2, interface) != 0)
		return -1;

	if (wpa_s->l2_br &&
	    l2_packet_set_mhd_interface(wpa_s->l2_br, interface) != 0) {
		if (wpa_s->l2)
			(void) l2_packet_set_mhd_interface(wpa_s->l2,
							 old_interface);
		return -1;
	}

	return 0;
}

static int drv_syna_set_mhd_interface(driver_syna_data_t *drv,
                                      mhd_interface_t interface,
                                      const char *reason)
{
    u8 own_addr[ETH_ALEN];
    mhd_interface_t old_interface;

    if (!drv || (interface != MHD_STA_INTERFACE &&
                 interface != MHD_AP_INTERFACE)) {
        SYNA_DRV_LOG(MSG_ERROR, "%s invalid mhd_ifidx=%d",
                     reason ? reason : "role", interface);
        return -1;
    }

    if (drv_syna_read_own_addr(interface, own_addr, reason) != 0)
        return -1;

    old_interface = drv->mhd_int;
    if (old_interface != interface &&
        drv_syna_set_l2_eapol_interface(drv, interface) != 0) {
        SYNA_DRV_LOG(MSG_ERROR,
                     "%s failed to switch mhd_ifidx %d -> %d ifname=%s",
                     reason ? reason : "role", old_interface, interface,
                     drv->ifname);
        return -1;
    }

    drv->mhd_int = interface;
    drv_syna_apply_own_addr(drv, own_addr, reason);

    if (old_interface != interface) {
        SYNA_DRV_LOG(MSG_INFO,
                     "%s switch mhd_ifidx %d -> %d ifname=%s",
                     reason ? reason : "role", old_interface, interface,
                     drv->ifname);
    }

    return 0;
}
#else
static int drv_syna_set_mhd_interface(driver_syna_data_t *drv,
                                      mhd_interface_t interface,
                                      const char *reason)
{
    mhd_interface_t old_interface;

    if (!drv || (interface != MHD_STA_INTERFACE &&
                 interface != MHD_AP_INTERFACE)) {
        SYNA_DRV_LOG(MSG_ERROR, "%s invalid mhd_ifidx=%d",
                     reason ? reason : "role", interface);
        return -1;
    }

    old_interface = drv->mhd_int;
    drv->mhd_int = interface;
    if (old_interface != interface)
        SYNA_DRV_LOG(MSG_INFO,
                     "%s switch mhd_ifidx %d -> %d ifname=%s",
                     reason ? reason : "role", old_interface, interface,
                     drv->ifname);

    return 0;
}
#endif

#ifndef ZEPHYR_OS
int drv_syna_register_transport(driver_syna_data_t *drv)
{
    return sup_adap_register_mhd_event_handler(drv->mhd_int);
}

int drv_syna_post_init(driver_syna_data_t *drv)
{
    if (drv_syna_refresh_own_addr(drv, drv->mhd_int, "init") != 0) {
        wpa_printf(MSG_ERROR,
                   "Failed to get own address for interface %s",
                   drv->ifname);
        return -1;
    }

    if (eloop_register_read_event(SUPP_SUP_ADAP_MASK,
                                  drv_sup_adap_message_receiver,
                                  drv, NULL) < 0) {
        wpa_printf(MSG_ERROR, "Failed to register supp adaptor read event");
        return -1;
    }

    return 0;
}

void drv_syna_pre_deinit(driver_syna_data_t *drv)
{
    (void)drv;
    eloop_unregister_read_event(SUPP_SUP_ADAP_MASK);
}

void drv_syna_notify_scan_started(driver_syna_data_t *drv)
{
    (void)drv;
}

void drv_syna_prepare_assoc_bandwidth(struct wpa_supplicant *wpa_s,
                                      const u8 *target_bssid,
                                      struct wpa_driver_associate_params *params)
{
    if (wpa_s && target_bssid) {
        struct wpa_bss *bss = wpa_bss_get_bssid_latest(wpa_s, target_bssid);

        if (bss) {
            params->freq.bandwidth = chan_width_to_mhz(bss->max_cw);
            SYNA_DRV_LOG(MSG_DEBUG,
                         "assoc bw derived from bss " MACSTR " (%s) = %d",
                         MAC2STR(target_bssid),
                         params->bssid ? "bssid" : "hint",
                         params->freq.bandwidth);
        } else {
            SYNA_DRV_LOG(MSG_DEBUG,
                         "assoc bss lookup failed for " MACSTR ", continue with default bw",
                         MAC2STR(target_bssid));
        }
    } else {
        SYNA_DRV_LOG(MSG_DEBUG,
                     "assoc no bssid/hint (FW BSS selection), continue with default bw");
    }
}

int drv_syna_validate_key_install(int deleting, int is_pairwise,
                                  struct wpa_driver_set_key_params *params)
{
    if (!deleting && is_pairwise && (params->key_flag & KEY_FLAG_NEXT)) {
        SYNA_DRV_LOG(MSG_DEBUG,
                     "set_key skip unsupported NEXT key (idx=%d flags=0x%x)",
                     params->key_idx, params->key_flag);
        return -1;
    }

    return 0;
}
#endif

#ifdef CONFIG_AP
/* Hostapd (AP mode) initializer: mirrors driver_syna_init but tailored for AP context. */
static void * drv_syna_hapd_init(struct hostapd_data *hapd, struct wpa_init_params *params)
{
    struct driver_syna_data *drv;
    drv = os_zalloc(sizeof(struct driver_syna_data));
    if (!drv) {
        SYNA_DRV_LOG(MSG_ERROR, "hapd_init alloc failed");
        return NULL;
    }
    drv->hapd = hapd;
    drv->ctx = NULL; /* hostapd uses hapd pointer path instead of wpa_s ctx */
    drv->mhd_int = MHD_AP_INTERFACE;
    if (params && params->ifname) os_strlcpy(drv->ifname, params->ifname, sizeof(drv->ifname));
    else os_strlcpy(drv->ifname, "ap0", sizeof(drv->ifname));
#ifndef ZEPHYR_OS
    if (drv_syna_refresh_own_addr(drv, drv->mhd_int, "hapd_init") != 0) {
        SYNA_DRV_LOG(MSG_ERROR, "hapd_init own MAC unavailable for iface=%s",
                     drv->ifname);
        os_free(drv);
        return NULL;
    }
#endif
    dl_list_init(&drv->scan_result_list);
    drv->initialized = 0;

    /* Fetch / cache capabilities for AP decisions (cipher/AKM exposure). */
    if (driver_syna_capa(drv) != 0) {
        SYNA_DRV_LOG(MSG_ERROR, "hapd_init capabilities fetch failed");
        os_free(drv);
        return NULL;
    }

    /* Register adaptor message receiver (reuse same mask). */
    if (eloop_register_read_event(SUPP_SUP_ADAP_MASK, drv_sup_adap_message_receiver, drv, NULL) < 0) {
        SYNA_DRV_LOG(MSG_ERROR, "hapd_init eloop register failed");
        os_free(drv);
        return NULL;
    }

    drv->initialized = 1;
    SYNA_DRV_LOG(MSG_INFO, "hapd_init ifname=%s role=AP", drv->ifname);
    return drv;
}

static void drv_syna_hapd_deinit(void *priv)
{
    struct driver_syna_data *drv = priv;
    if (!drv) return;
    SYNA_DRV_LOG(MSG_INFO, "hapd_deinit ifname=%s ap_started=%d", drv->ifname, drv->ap_started);
    if (drv->ap_started) {
        int32_t rc = mhd_wifi_api_stop_ap(MHD_AP_INTERFACE, MHD_TRUE);
        SYNA_DRV_LOG(MSG_INFO, "stop_ap invoked rc=%ld", (long)rc);
        drv->ap_started = 0;
    }
    /* Reuse generic deinit logic for list cleanup & eloop unregister */
    driver_syna_deinit(drv);
}

static int drv_syna_deinit_ap(void *priv)
{
    struct driver_syna_data *drv = priv;
    mhd_result_t rc = MHD_SUCCESS;
    if (!drv)
        return -1;
    SYNA_DRV_LOG(MSG_INFO, "deinit_ap ifname=%s ap_started=%d",
                 drv->ifname, drv->ap_started);
    if (drv->ap_started) {
        rc = mhd_wifi_api_stop_ap(MHD_AP_INTERFACE, MHD_TRUE);
        SYNA_DRV_LOG(MSG_INFO, "stop_ap rc=%ld", (long)rc);
        drv->ap_started = 0;
    }
    if (drv_syna_set_mhd_interface(drv, MHD_STA_INTERFACE,
                                   "deinit_ap") != 0)
        return -1;
    return rc;
}

#endif

/* Map firmware capability struct into wpa_driver_capa.
 * For now we set a conservative baseline plus specific AKMs/flags inferred from tokens.
 */
static int driver_syna_build_capa_from_fw(driver_syna_data_t *drv, const mhd_wifi_fw_cap_t *fw)
{
    struct wpa_driver_capa *c = &drv->capa;
    int i;
    os_memset(c, 0, sizeof(*c));

    /* Base auth: limit to OPEN only (per current requirement) */
    c->auth = WPA_DRIVER_AUTH_OPEN;

    /* Key management: start with WPA/WPA2 (PSK + 802.1X) and add SAE/FT/OWE if advertised. */
    c->key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA |
                  WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK |
                  WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
                  WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK |
                  WPA_DRIVER_CAPA_KEY_MGMT_PSK_SHA256 |
                  WPA_DRIVER_CAPA_KEY_MGMT_SUITE_B |
                  WPA_DRIVER_CAPA_KEY_MGMT_DPP;
    if (fw->akm & MHD_FW_CAP_AKM_SAE) {
        c->key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_SAE; /* SAE */
        c->key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_SAE_EXT_KEY; /* SAE-EXT-KEY */
    }
    if ((fw->akm & MHD_FW_CAP_AKM_FT) && (fw->akm & MHD_FW_CAP_AKM_SAE))
        c->key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_FT_SAE; /* FT+SAE */
    if (fw->akm & MHD_FW_CAP_AKM_FT)
        c->key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_FT | WPA_DRIVER_CAPA_KEY_MGMT_FT_PSK; /* generic FT support */
    if (fw->flags & MHD_FW_CAP_FLAG_OWE_AP)   c->key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_OWE;

    for (i = 0; i < WPA_IF_MAX; i++)
        c->key_mgmt_iftype[i] = c->key_mgmt;

    /* Cipher suites: assume modern AES-GCMP/CCMP baseline; add BIP variants if mfp present. */
    c->enc = WPA_DRIVER_CAPA_ENC_CCMP | WPA_DRIVER_CAPA_ENC_GCMP |
             WPA_DRIVER_CAPA_ENC_GCMP_256 | WPA_DRIVER_CAPA_ENC_CCMP_256;
    if (fw->mfp) {
        c->enc |= WPA_DRIVER_CAPA_ENC_BIP | WPA_DRIVER_CAPA_ENC_BIP_GMAC_128 |
                  WPA_DRIVER_CAPA_ENC_BIP_GMAC_256 | WPA_DRIVER_CAPA_ENC_BIP_CMAC_256;
    }

    /* Flags: enable AP/P2P if tokens present; multi-band/MBSS approximate mapping.
     * For fake AP enablement in simulation/QEMU builds we unconditionally expose
     * WPA_DRIVER_FLAGS_AP so that the wpa_supplicant AP state machine proceeds
     * even if the underlying stub firmware capability struct reports ap=0.
     * If real firmware capability gating is desired later, guard the following
     * line with a build option (e.g. CONFIG_FAKE_AP_CAP) and fall back to fw->ap.
     */
    c->flags |= WPA_DRIVER_FLAGS_AP; /* fake AP support always advertised */
    if (fw->ap)  c->flags |= WPA_DRIVER_FLAGS_AP; /* retain original condition for clarity */
    if (fw->flags & MHD_FW_CAP_FLAG_P2P) c->flags |= WPA_DRIVER_FLAGS_P2P_CAPABLE;
    if (fw->flags & MHD_FW_CAP_FLAG_DUALBAND) c->flags |= WPA_DRIVER_FLAGS_HT_IBSS; /* placeholder for dualband */
    c->flags |= WPA_DRIVER_FLAGS_BSS_SELECTION; /* allow supplicant FT roam BSS refresh path */
    if (fw->akm & MHD_FW_CAP_AKM_SAE) {
        c->flags |= WPA_DRIVER_FLAGS_SAE; /* Host-side SAE via external authentication */
        c->flags2 |= WPA_DRIVER_FLAGS2_SAE_OFFLOAD_STA;
    }
#ifdef WPA_DRIVER_FLAGS2_6GHz
    if (fw->flags & MHD_FW_CAP_FLAG_6G)   c->flags2 |= WPA_DRIVER_FLAGS2_6GHz;
#endif
#ifdef WPA_DRIVER_FLAGS2_TWT_RESPONDER
    if (fw->flags & MHD_FW_CAP_FLAG_TWT)      c->flags2 |= WPA_DRIVER_FLAGS2_TWT_RESPONDER;
#endif

    if (fw->flags & MHD_FW_CAP_FLAG_MLO)    c->flags2 |= WPA_DRIVER_FLAGS2_MLO;
    if (fw->flags & MHD_FW_CAP_FLAG_BEACON_PROT)
        c->flags |= WPA_DRIVER_FLAGS_BEACON_PROTECTION;

    /* Conservative scan/ remain-on-channel defaults */
    c->max_scan_ssids = 4;
    c->max_sched_scan_ssids = 0;
    c->max_sched_scan_plans = 0;
    c->max_sched_scan_plan_interval = 0;
    c->max_sched_scan_plan_iterations = 0;
    c->sched_scan_supported = 0;
    c->max_match_sets = 0;
    c->num_multichan_concurrent = fw->rsdb ? 2 : 1;
    c->max_remain_on_chan = 500; /* ms placeholder */

    drv->has_capability = 1;

    SYNA_DRV_LOG(MSG_DEBUG, "fw caps mapped key_mgmt=0x%x enc=0x%x flags=0x%llx flags2=0x%llx",
                 c->key_mgmt, c->enc, (unsigned long long)c->flags, (unsigned long long)c->flags2);
    return 0;
}

static int driver_syna_capa(driver_syna_data_t *drv)
{
    mhd_wifi_fw_cap_t fwcap;
    mhd_result_t rc = mhd_wifi_api_get_cap(&fwcap);
    if (rc != MHD_SUCCESS) {
        SYNA_DRV_LOG(MSG_ERROR, "mhd_wifi_api_get_cap failed rc=%ld", (long)rc);
        return -1;
    }
    driver_syna_build_capa_from_fw(drv, &fwcap);
    SYNA_DRV_LOG(MSG_DEBUG, "capabilities cached (key_mgmt=0x%x enc=0x%x)", drv->capa.key_mgmt, drv->capa.enc);
    return 0;
}

static const char *drv_syna_hwmode_str(enum hostapd_hw_mode mode)
{
    switch (mode) {
    case HOSTAPD_MODE_IEEE80211B:
        return "802.11b";
    case HOSTAPD_MODE_IEEE80211G:
        return "802.11g";
    case HOSTAPD_MODE_IEEE80211A:
        return "802.11a";
    case HOSTAPD_MODE_IEEE80211AD:
        return "802.11ad";
    default:
        return "?";
    }
}

static const char *drv_syna_opmode_str(unsigned int opmode)
{
    switch (opmode) {
    case IEEE80211_MODE_INFRA:
        return "INFRA";
    case IEEE80211_MODE_IBSS:
        return "IBSS";
    case IEEE80211_MODE_AP:
        return "AP";
    case IEEE80211_MODE_MESH:
        return "MESH";
    default:
        return "UNUSED";
    }
}

static size_t drv_syna_min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static void drv_syna_copy_he_cap(struct he_capabilities *dst_he,
                                 const mhd_wifi_he_cap_t *src_he)
{
    if (!dst_he || !src_he)
        return;

    dst_he->he_supported = 1;
    os_memcpy(dst_he->mac_cap, src_he->mac_cap_info,
              drv_syna_min_size(sizeof(dst_he->mac_cap),
                                sizeof(src_he->mac_cap_info)));
    os_memcpy(dst_he->phy_cap, src_he->phy_cap_info,
              drv_syna_min_size(sizeof(dst_he->phy_cap),
                                sizeof(src_he->phy_cap_info)));
    os_memcpy(dst_he->mcs, &src_he->he_mcs_nss_supp,
              drv_syna_min_size(sizeof(dst_he->mcs),
                                sizeof(src_he->he_mcs_nss_supp)));
    os_memcpy(dst_he->ppet, src_he->ppe_thres,
              drv_syna_min_size(sizeof(dst_he->ppet),
                                sizeof(src_he->ppe_thres)));
                          
    dst_he->he_6ghz_capa = src_he->cap_6ghz;

    /* Issue: hostapd_check_he_6ghz_capab() compares these 6 GHz capability
     * fields against hostapd's defaults (max_mpdu=2, max_ampdu_len_exp=7,
     * rx_ant_pat=1, tx_ant_pat=1) and returns -1 if firmware reports lower,
     * blocking 6 GHz SoftAP startup.
     *
     * Why done here: These fields are normally settable via hostapd.conf, but
     * the current platform uses wpa_supplicant with CONFIG_AP which does not
     * expose he_6ghz_* config knobs. There is no way to override the defaults
     * without modifying open source code. So we set them as a minimum floor
     * in this port layer. OR is used so any higher firmware value is preserved.
     *
     * When to remove: Once wpa_supplicant AP mode exposes he_6ghz_* config
     * keys (open source change), or hostapd_check_he_6ghz_capab() is updated
     * to warn instead of hard-fail, this workaround can be dropped.
     */
    dst_he->he_6ghz_capa |= (7 << HE_6GHZ_BAND_CAP_MAX_AMPDU_LEN_EXP_SHIFT) &
                              HE_6GHZ_BAND_CAP_MAX_AMPDU_LEN_EXP_MASK;
    dst_he->he_6ghz_capa |= (2 << HE_6GHZ_BAND_CAP_MAX_MPDU_LEN_SHIFT) &
                              HE_6GHZ_BAND_CAP_MAX_MPDU_LEN_MASK;
    dst_he->he_6ghz_capa |= HE_6GHZ_BAND_CAP_RX_ANTPAT_CONS;
    dst_he->he_6ghz_capa |= HE_6GHZ_BAND_CAP_TX_ANTPAT_CONS;
    
}

static void drv_syna_copy_eht_cap(struct eht_capabilities *dst_eht,
                                  const mhd_wifi_eht_cap_t *src_eht)
{
    if (!dst_eht || !src_eht)
        return;

    dst_eht->eht_supported = true;
    dst_eht->mac_cap = src_eht->mac_cap_info;
    os_memcpy(dst_eht->phy_cap, src_eht->phy_cap_info,
              drv_syna_min_size(sizeof(dst_eht->phy_cap),
                                sizeof(src_eht->phy_cap_info)));
    os_memcpy(dst_eht->mcs, &src_eht->eht_mcs_nss_supp,
              drv_syna_min_size(sizeof(dst_eht->mcs),
                                sizeof(src_eht->eht_mcs_nss_supp)));
    os_memset(dst_eht->ppet, 0, sizeof(dst_eht->ppet));
    os_memcpy(dst_eht->ppet, src_eht->ppe_thres,
              drv_syna_min_size(sizeof(dst_eht->ppet),
                                sizeof(src_eht->ppe_thres)));
}

/* Map one types_mask to one hostap opmode by fixed priority. */
static unsigned int drv_syna_type_mask_to_ieee_idx(uint16_t types_mask)
{
    if (types_mask & (uint16_t)(1U << MHD_IFTYPE_STATION))
        return IEEE80211_MODE_INFRA;

    if (types_mask & (uint16_t)(1U << MHD_IFTYPE_ADHOC))
        return IEEE80211_MODE_IBSS;

    if (types_mask & (uint16_t)(1U << MHD_IFTYPE_AP))
        return IEEE80211_MODE_AP;

    if (types_mask & (uint16_t)(1U << MHD_IFTYPE_MESH_POINT))
        return IEEE80211_MODE_MESH;

    return IEEE80211_MODE_NUM;
}
static void drv_syna_fill_he_capab(struct hostapd_hw_modes *dst,
                                   const mhd_wifi_supported_band_t *src,
                                   unsigned int band_idx)
{
    int he_found = 0;
    const mhd_wifi_he_cap_t *mhd_he = NULL;

    if (!dst || !src)
        return;

    if (src->n_hecaps > 0 && !src->he_cap)
        return;

    os_memset(dst->he_capab, 0, sizeof(dst->he_capab));
    dst->flags |= HOSTAPD_MODE_FLAG_HE_INFO_KNOWN;

    for (int i = 0; i < src->n_hecaps; i++) {
        unsigned int target_idx;
        struct he_capabilities *he;

        mhd_he = &src->he_cap[i];
        target_idx = drv_syna_type_mask_to_ieee_idx(mhd_he->types_mask);

        if (target_idx == IEEE80211_MODE_NUM) {
            SYNA_DRV_LOG(MSG_DEBUG,
                         "band[%u] HE[%d]: unsupported types_mask=0x%04x, skip",
                         band_idx, i, (unsigned int)mhd_he->types_mask);
            continue;
        }

        if (!mhd_he->has_he) {
            SYNA_DRV_LOG(MSG_DEBUG,
                         "band[%u] HE[%d] skip: source reports has_he=0 (types_mask=0x%04x)",
                         band_idx, i, (unsigned int)mhd_he->types_mask);
            continue;
        }

        he = &dst->he_capab[target_idx];
        if (he->he_supported) {
            SYNA_DRV_LOG(MSG_DEBUG,
                         "band[%u] HE[%u:%s]: already filled, keep first match",
                         band_idx, target_idx, drv_syna_opmode_str(target_idx));
            continue;
        }

        drv_syna_copy_he_cap(he, mhd_he);

        he_found = 1;
        SYNA_DRV_LOG(MSG_DEBUG,
                     "band[%u] HE[%u:%s]: mapped from types_mask=0x%04x",
                     band_idx, target_idx, drv_syna_opmode_str(target_idx),
                     (unsigned int)mhd_he->types_mask);
    }

    if (!he_found) {
        SYNA_DRV_LOG(MSG_DEBUG,
                     "band[%u] HE: no matching HE entry for any opmode",
                     band_idx);
    }
}

/* ETH/EHT capability fill hook. */
static void drv_syna_fill_eht_capab(struct hostapd_hw_modes *dst,
                                    const mhd_wifi_supported_band_t *src,
                                    unsigned int band_idx)
{
    int eht_found = 0;
    const mhd_wifi_eht_cap_t *mhd_eht = NULL;

    if (!dst || !src)
        return;

    if (src->n_ehtcaps > 0 && !src->eht_cap)
        return;

    os_memset(dst->eht_capab, 0, sizeof(dst->eht_capab));
#ifndef ZEPHYR_OS
    dst->flags |= HOSTAPD_MODE_FLAG_EHT_INFO_KNOWN;
#endif

    for (int i = 0; i < src->n_ehtcaps; i++) {
        unsigned int target_idx;
        struct eht_capabilities *eht;

        mhd_eht = &src->eht_cap[i];
        target_idx = drv_syna_type_mask_to_ieee_idx(mhd_eht->types_mask);

        if (target_idx == IEEE80211_MODE_NUM) {
            SYNA_DRV_LOG(MSG_DEBUG,
                         "band[%u] EHT[%d]: unsupported types_mask=0x%04x, skip",
                         band_idx, i, (unsigned int)mhd_eht->types_mask);
            continue;
        }

        if (!mhd_eht->has_eht) {
            SYNA_DRV_LOG(MSG_DEBUG,
                         "band[%u] EHT[%d] skip: source reports has_eht=0 (types_mask=0x%04x)",
                         band_idx, i, (unsigned int)mhd_eht->types_mask);
            continue;
        }

        eht = &dst->eht_capab[target_idx];

        if (eht->eht_supported) {
            SYNA_DRV_LOG(MSG_DEBUG,
                         "band[%u] EHT[%u:%s]: already filled, keep first match",
                         band_idx, target_idx, drv_syna_opmode_str(target_idx));
            continue;
        }

        drv_syna_copy_eht_cap(eht, mhd_eht);

        eht_found = 1;
        SYNA_DRV_LOG(MSG_DEBUG,
                     "band[%u] EHT[%u:%s]: mapped from types_mask=0x%04x",
                     band_idx, target_idx, drv_syna_opmode_str(target_idx),
                     (unsigned int)mhd_eht->types_mask);
    }

    if (!eht_found) {
        SYNA_DRV_LOG(MSG_DEBUG,
                     "band[%u] EHT: no matching EHT entry for any opmode",
                     band_idx);
    }
}

static void drv_syna_dump_chan_list(struct hostapd_hw_modes *modes, u16 num_modes)
{
    int i;

    if (!modes)
        return;

    for (i = 0; i < num_modes; i++) {
        struct hostapd_hw_modes *mode = &modes[i];
        char str[1000];
        char *pos = str;
        char *end = pos + sizeof(str);
        int j, res;

        for (j = 0; j < mode->num_channels; j++) {
            struct hostapd_channel_data *chan = &mode->channels[j];
            res = os_snprintf(pos, end - pos, " %d%s%s%s",
                              chan->freq,
                              (chan->flag & HOSTAPD_CHAN_DISABLED) ?
                              "[DISABLED]" : "",
                              (chan->flag & HOSTAPD_CHAN_NO_IR) ?
                              "[NO_IR]" : "",
                              (chan->flag & HOSTAPD_CHAN_RADAR) ?
                              "[RADAR]" : "");
            if (os_snprintf_error(end - pos, res))
                break;
            pos += res;
        }

        *pos = '\0';
        SYNA_DRV_LOG(MSG_DEBUG, "Mode IEEE %s%s:%s",
                     drv_syna_hwmode_str(mode->mode),
                     mode->is_6ghz ? "(6GHz)" : "",
                     str);
    }
}

/* Sabre platform could not execute multiple times ioctl query for current band info */
#ifdef HW_FEATURE_CACHE
uint32_t g_num_bands = 0;
struct hostapd_hw_modes *g_hw_mode = NULL;
#endif
struct hostapd_hw_modes *
drv_syna_get_hw_feature_data(void *priv, u16 *num_modes, u16 *flags, u8 *dfs_domain)
{
    (void)priv; /* not used currently */
    uint16_t num_bands;
    mhd_wifi_supported_band_t mhd_bands[MHD_CHANSPEC_BAND_MAX];

#ifdef HW_FEATURE_CACHE
    if (g_hw_mode && g_num_bands) {
	    int i;
	    struct hostapd_hw_modes *pmode = g_hw_mode;
	    *num_modes = g_num_bands;
	    return g_hw_mode;
    }
#endif
    int32_t rc = mhd_wifi_api_get_band_capabilities(mhd_bands, &num_bands);
    if (rc != 0 || num_bands == 0) {
        SYNA_DRV_LOG(MSG_ERROR, "get_hw_feature_data API failed rc=%d num=%u", (int)rc, (unsigned)num_bands);
        return NULL;
    }

    SYNA_DRV_LOG(MSG_DEBUG, "get_hw_feature_data returned %u bands. mhd_band %p", (unsigned)num_bands, mhd_bands);

    /* Allocate hostapd modes array */
    struct hostapd_hw_modes *modes = os_zalloc(sizeof(struct hostapd_hw_modes) * num_bands);
    if (!modes) {
        SYNA_DRV_LOG(MSG_ERROR, "get_hw_feature_data modes alloc failed");
        goto cleanup_bands;
    }
#ifdef HW_FEATURE_CACHE
    g_hw_mode = modes;
    g_num_bands = num_bands;
#endif

    /* Translate each band from mhd format to hostapd format */
    for (uint16_t i = 0; i < num_bands; i++) {
        mhd_wifi_supported_band_t *src = &mhd_bands[i];
        struct hostapd_hw_modes *dst = &modes[i];

        /* Map band enum to hostapd mode */
        switch (src->band) {
            case MHD_CHANSPEC_BAND_2G:
                dst->mode = HOSTAPD_MODE_IEEE80211G;
                dst->is_6ghz = false;
                break;
            case MHD_CHANSPEC_BAND_5G:
                dst->mode = HOSTAPD_MODE_IEEE80211A;
                dst->is_6ghz = false;
                break;
            case MHD_CHANSPEC_BAND_6G:
                dst->mode = HOSTAPD_MODE_IEEE80211A;
                dst->is_6ghz = true;
                break;
            default:
                dst->mode = HOSTAPD_MODE_IEEE80211A;
                dst->is_6ghz = false;
                break;
        }

        SYNA_DRV_LOG(MSG_DEBUG,
                     "band[%u] src_band=%u -> mode=%s%s (mode_id=%d)",
                     i, (unsigned int)src->band,
                     drv_syna_hwmode_str(dst->mode),
                     dst->is_6ghz ? " (6GHz)" : "",
                     (int)dst->mode);
        dst->flags |= HOSTAPD_MODE_FLAG_HT_INFO_KNOWN |
                      HOSTAPD_MODE_FLAG_VHT_INFO_KNOWN;

        /* Translate channels */
        dst->num_channels = src->n_channels;
        if (dst->num_channels > 0) {
            dst->channels = os_calloc(dst->num_channels, sizeof(struct hostapd_channel_data));
            if (!dst->channels) {
                SYNA_DRV_LOG(MSG_ERROR, "get_hw_feature_data channels alloc failed band=%u for %d bytes", i, sizeof(struct hostapd_channel_data));
                goto fail;
            }
            for (int c = 0; c < dst->num_channels; c++) {
                mhd_wifi_channel_t *src_ch = &src->channels[c];
                struct hostapd_channel_data *dst_ch = &dst->channels[c];

                dst_ch->chan = (int)src_ch->hw_value;
                dst_ch->freq = (int)src_ch->center_freq;
                dst_ch->flag        = 0;
                dst_ch->allowed_bw  = ~0u; /* initially all widths allowed, masking done below */
                dst_ch->max_tx_power = (int)src_ch->max_power;
                dst_ch->dfs_cac_ms  = src_ch->dfs_cac_ms;

                if (src_ch->flags & MHD_CHAN_DISABLED)
                    dst_ch->flag |= HOSTAPD_CHAN_DISABLED;

                if (src_ch->flags & MHD_CHAN_NO_IR)
                    dst_ch->flag |= HOSTAPD_CHAN_NO_IR;

                if (src_ch->flags & MHD_CHAN_RADAR)
                    dst_ch->flag |= HOSTAPD_CHAN_RADAR; /* DFS required on channel */

                /* bandwidth restrictions */
                if (src_ch->flags & MHD_CHAN_NO_20MHZ)
                    dst_ch->allowed_bw &= ~HOSTAPD_CHAN_WIDTH_20;
                if (src_ch->flags & MHD_CHAN_NO_HT40PLUS)
                    dst_ch->allowed_bw &= ~HOSTAPD_CHAN_WIDTH_40P;
                if (src_ch->flags & MHD_CHAN_NO_HT40MINUS)
                    dst_ch->allowed_bw &= ~HOSTAPD_CHAN_WIDTH_40M;
                if (src_ch->flags & MHD_CHAN_NO_80MHZ)
                    dst_ch->allowed_bw &= ~HOSTAPD_CHAN_WIDTH_80;
                if (src_ch->flags & MHD_CHAN_NO_160MHZ)
                    dst_ch->allowed_bw &= ~HOSTAPD_CHAN_WIDTH_160;

                /* DFS state */
                switch (src_ch->dfs_state) {
                case MHD_DFS_USABLE:
                    dst_ch->flag |= HOSTAPD_CHAN_DFS_USABLE;
                    break;
                case MHD_DFS_AVAILABLE:
                    dst_ch->flag |= HOSTAPD_CHAN_DFS_AVAILABLE;
                    break;
                case MHD_DFS_UNAVAILABLE:
                    dst_ch->flag |= HOSTAPD_CHAN_DFS_UNAVAILABLE;
                    break;
                default:
                    break;
                }
            }
        }

        /* Translate bitrates */
        dst->num_rates = src->n_bitrates;
        if (dst->num_rates > 0) {
            dst->rates = os_calloc(dst->num_rates, sizeof(int));
            if (!dst->rates) {
                SYNA_DRV_LOG(MSG_ERROR, "get_hw_feature_data rates alloc failed band=%u", i);
                goto fail;
            }
            for (int r = 0; r < dst->num_rates; r++) {
                dst->rates[r] = (int)src->bitrates[r].bitrate; /* already in 100 Kbps units */
            }
        }

        /* Translate HT capabilities */
        if (src->ht_cap.supported) {
            dst->ht_capab = (int)src->ht_cap.cap;
            os_memcpy(&dst->mcs_set, &src->ht_cap.mcs, sizeof(dst->mcs_set));
            dst->a_mpdu_params = src->ht_cap.ampdu_factor |
                                 (src->ht_cap.ampdu_density << 2);
        }

        /* Translate VHT capabilities */
        if (src->vht_cap.supported) {
            dst->vht_capab = src->vht_cap.cap;
            os_memcpy(&dst->vht_mcs_set, &src->vht_cap.vht_mcs, sizeof(dst->vht_mcs_set));
        }

        drv_syna_fill_he_capab(dst, src, i);
        drv_syna_fill_eht_capab(dst, src, i);
        supplicant_dump_ht_capab("DRV_SYNA", dst, i);
        supplicant_dump_vht_capab("DRV_SYNA", dst, i);
        supplicant_dump_he_capab("DRV_SYNA", dst, i);
        supplicant_dump_eht_capab("DRV_SYNA", dst, i);

    }

    /* Set output parameters */
    if (num_modes) *num_modes = num_bands;
    if (flags) *flags = 0; /* No special flags for now */
    if (dfs_domain) *dfs_domain = 0; /* No DFS domain info */

    SYNA_DRV_LOG(MSG_INFO, "get_hw_feature_data success: %u bands", (unsigned)num_bands);
    drv_syna_dump_chan_list(modes, num_bands);

    /* Clean up MHD bands (deep free channels/rates allocated by stub) */
cleanup_bands:
    return modes;

fail:
    /* Clean up partially allocated hostapd modes */
    if (modes) {
        for (uint16_t m = 0; m < num_bands; m++) {
            if (modes[m].channels) os_free(modes[m].channels);
            if (modes[m].rates) os_free(modes[m].rates);
        }
        os_free(modes);
    }
    return NULL;
}

void *driver_syna_init_common(void *ctx, const char *ifname,
                              const void *platform_data)
{
    struct driver_syna_data *drv;
    mhd_init_status_t init_status;

    drv = os_zalloc(sizeof(struct driver_syna_data));
    if (drv == NULL) {
        wpa_printf(MSG_ERROR, "Could not allocate memory for none "
               "driver data");
        return NULL;
    }

    drv->ctx = ctx;

    if (drv_syna_store_platform_init(drv, platform_data) != 0) {
        os_free(drv);
        return NULL;
    }

    if (ifname)
        os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));
    else
        os_strlcpy(drv->ifname, "wlan0", sizeof(drv->ifname));
    /* Determine initial role by name heuristics (minimal P2P detection). */
    if (ifname && os_strncmp(ifname, "p2p-dev-", 8) == 0) {
        drv->mhd_int = MHD_P2P_INTERFACE; /* management */
    } else if (ifname && os_strncmp(ifname, "p2p-", 4) == 0) {
        drv->mhd_int = MHD_P2P_INTERFACE; /* treat as group for now */
        drv->is_p2p_group = 1;
    } else {
        drv->mhd_int = MHD_STA_INTERFACE;
    }
    drv->initialized = 0;

    dl_list_init(&drv->scan_result_list);

    /* Supplicant task should wait until MHD (including WLAN FW) is initialized */
    mhd_wifi_api_wait_for_ready(&init_status, osWaitForever);
    if (init_status != MHD_INIT_SUCCESS) {
        wpa_printf(MSG_ERROR, "MHD failed to initialize. Status: %d", init_status);
        os_free(drv);
        return NULL;
    }

    /* Request driver capabilities from driver
     * and update them to the supplicant's
     * driver interface data (driver_syna_data).
     */
    if (driver_syna_capa(drv)) {
        wpa_printf(MSG_ERROR, "Failed to update driver capabilities");
        os_free(drv);
        return NULL;
    }

    if (drv_syna_register_transport(drv) != 0) {
        wpa_printf(MSG_ERROR, "Failed to register MHD event handler for interface %d", drv->mhd_int);
        os_free(drv);
        return NULL;
    }

    if (drv_syna_post_init(drv) != 0) {
        driver_syna_deinit(drv);
        return NULL;
    }

    drv->initialized = 1;
    SYNA_DRV_LOG(MSG_INFO, "init ifname=%s role=%d", drv->ifname, drv->mhd_int);
    return drv;
}

#ifndef ZEPHYR_OS
void *driver_syna_init(void *ctx, const char *ifname)
{
    return driver_syna_init_common(ctx, ifname, NULL);
}
#endif

void driver_syna_deinit(void *priv)
{
    struct driver_syna_data *drv = priv;
        drv_scan_result_t *cur, *tmp;

    if (!drv)
        return;

    SYNA_DRV_LOG(MSG_INFO, "deinit ifname=%s initialized=%d ap_started=%d", drv->ifname, drv->initialized, drv->ap_started);
#ifdef CONFIG_AP
    if (drv->ap_started) {
        int32_t rc = mhd_wifi_api_stop_ap(MHD_AP_INTERFACE, MHD_TRUE);
        SYNA_DRV_LOG(MSG_INFO, "stop_ap (driver_deinit) rc=%ld", (long)rc);
        drv->ap_started = 0;
    }
#endif

    drv_syna_pre_deinit(drv);

    if (!drv->initialized) {
        /* Already deinitialized or never fully initialized */
        goto free_drv;
    }

    /* Safe traversal: capture next before freeing current */
        #ifdef dl_list_for_each_safe
        dl_list_for_each_safe(cur, tmp, &drv->scan_result_list, drv_scan_result_t, list) {
            dl_list_del(&cur->list);
            if (cur->ssids.ssid)
                os_free((void*)cur->ssids.ssid);
            os_free(cur);
        }
        #else
        while ((cur = dl_list_first(&drv->scan_result_list, drv_scan_result_t, list)) != NULL) {
            dl_list_del(&cur->list);
            if (cur->ssids.ssid)
                os_free((void*)cur->ssids.ssid);
            os_free(cur);
        }
        #endif
    /* Release deep-copied extended capability buffers if present */
    if (drv->capa.extended_capa)
        os_free((void*)drv->capa.extended_capa);
    if (drv->capa.extended_capa_mask)
        os_free((void*)drv->capa.extended_capa_mask);
free_drv:
    /* Best-effort zero of sensitive association identifiers */
    os_memset(drv->bssid, 0, sizeof(drv->bssid));
    os_memset(drv->prev_bssid, 0, sizeof(drv->prev_bssid));
    os_memset(drv->ssid, 0, sizeof(drv->ssid));
    drv->ssid_len = 0;
    drv->initialized = 0;
    os_free(drv);
}

int drv_syna_scan_req(void *priv, struct wpa_driver_scan_params *params)
{
    //To notify the upper layer that the scan has started
    struct driver_syna_data *drv = priv;

    if (!params) {
        SYNA_DRV_LOG(MSG_ERROR, "drv_syna_scan_req: NULL params");
        return -1;
    }

    int32_t ret = (int32_t)syna_adapt_scan(params);
    if (ret != 0) {
        SYNA_DRV_LOG(MSG_ERROR, "drv_syna_scan_req: syna_adapt_scan failed (ret=%d)", (int)ret);
        return -1;
    }

    drv_syna_notify_scan_started(drv);

    SYNA_DRV_LOG(MSG_DEBUG, "drv_syna_scan_req: scan request sent successfully");
    return 0;
}

/* Stop/abort a scheduled scan or request an ongoing scan to be aborted.
 * This sends a SUP_ADAP_CMD_SCAN_ABORT message to the adaptor/MHD task.
 */
int drv_syna_stop_sched_scan(void *priv)
{
    struct driver_syna_data *drv = priv;
    sup_adap_msg_t msg;
    union wpa_event_data event;

    os_memset(&msg, 0, sizeof(sup_adap_msg_t));
    msg.type = SUP_ADAP_CMD_SCAN_ABORT;
    msg.msg = NULL;

    SYNA_DRV_LOG(MSG_DEBUG, "stop_sched_scan -> SUP_ADAP_CMD_SCAN_ABORT");
    send_msg_to_sup_adap(&msg);

    os_memset(&event, 0, sizeof(event));
    event.scan_info.aborted = 1;
    SYNA_DRV_LOG(MSG_DEBUG, "abort_sched_scan: injecting EVENT_SCAN_RESULTS "
                            "(aborted=1) to unblock wpa_supplicant");
    wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS, &event);

    return 0;
}

/* Stop/abort a scheduled scan or request an ongoing scan to be aborted.
 * This sends a SUP_ADAP_CMD_SCAN_ABORT message to the adaptor/MHD task.
 */
int drv_syna_abort_sched_scan(void *priv, u64 scan_cookie)
{
    struct driver_syna_data *drv = priv;
    union wpa_event_data event;
    sup_adap_msg_t msg;

    (void)scan_cookie;

    /* 1. Tell the firmware to stop scanning */
    os_memset(&msg, 0, sizeof(sup_adap_msg_t));
    msg.type = SUP_ADAP_CMD_SCAN_ABORT;
    msg.msg = NULL;

    SYNA_DRV_LOG(MSG_DEBUG, "abort_sched_scan -> SUP_ADAP_CMD_SCAN_ABORT");
    send_msg_to_sup_adap(&msg);

    /* 3. Inject EVENT_SCAN_RESULTS with aborted=1
     *    (nl80211: send_scan_event(bss, aborted=1, ...)
     *     which sets info->aborted=1 then calls wpa_supplicant_event)
     */
    os_memset(&event, 0, sizeof(event));
    event.scan_info.aborted = 1;
    SYNA_DRV_LOG(MSG_DEBUG, "abort_sched_scan: injecting EVENT_SCAN_RESULTS "
                            "(aborted=1) to unblock wpa_supplicant");
    wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS, &event);

    return 0;
}

/* Build an array of scan results by consuming entries from the global
 * drv_scan_results_list. Caller (wpa_supplicant) will free the returned
 * structure; driver entries are freed here after their contents are copied.
 */
struct wpa_scan_results *drv_syna_get_scan_results(void *priv)
{
    struct wpa_scan_results *wpa_results;
    struct driver_syna_data *drv = priv;
    drv_scan_result_t *drv_scan_res;
    struct wpa_scan_res **tmp;
    struct wpa_scan_res *r;
    u8 *pos, *ies;
    size_t alloc_size;
    unsigned int total_count = 0;
    drv_scan_result_t *c;

    (void) drv; /* drv may be unused when results are provided via global list */

    /* Count total available in global list (where results are actually added) */
    dl_list_for_each(c, &drv_scan_results_list, drv_scan_result_t, list) total_count++;

    tmp = os_zalloc(sizeof(struct wpa_scan_res *) * total_count);
    if (!tmp)
        return NULL;

    wpa_results = os_zalloc(sizeof(*wpa_results));
    if (!wpa_results) {
        os_free(tmp);
        return NULL;
    }
    os_memset(wpa_results, 0, sizeof(*wpa_results));

    wpa_results->num = 0;

    /* First, consume entries from the per-driver list (preferred). This
     * matches the path where driver_syna_events.c enqueues partial results
     * into drv->scan_result_list. If that list is empty, fall back to the
     * global adaptor-populated list drv_scan_results_list which some code
     * paths may use.
     */
    while ((drv_scan_res = dl_list_first(&drv->scan_result_list, drv_scan_result_t, list)) != NULL) {
        /* Remove from driver's list */
        dl_list_del(&drv_scan_res->list);

        if (wpa_results->num >= MAX_SCAN_RESULTS) {
            /* Too many results; drop the rest. Free this entry safely once. */
            if (drv_scan_res->ssids.ssid)
                os_free((void *) drv_scan_res->ssids.ssid);
            os_free(drv_scan_res);
            continue;
        }

        /* Allocate wpa_scan_res with space for IEs (ie_len + beacon_ie_len)
         * Note: beacon_ie_len may be zero in many implementations.
         */
        alloc_size = sizeof(*r) + drv_scan_res->ie_len + drv_scan_res->beacon_ie_len;
        r = os_zalloc(alloc_size);
        if (!r) {
            /* Allocation failure: free remaining driver entry and stop */
            if (drv_scan_res->ssids.ssid)
                os_free((void *) drv_scan_res->ssids.ssid);
            os_free(drv_scan_res);
            break;
        }

        /* Copy basic fields */
        r->flags = drv_scan_res->flags;
        memcpy(r->bssid, drv_scan_res->bssid, ETH_ALEN);
        r->freq = drv_scan_res->freq;
        r->max_cw = drv_scan_res->max_cw;
        r->beacon_int = drv_scan_res->beacon_int;
        r->caps = drv_scan_res->caps;
        r->qual = drv_scan_res->qual;
        r->noise = drv_scan_res->noise;
        r->level = drv_scan_res->level;
        r->tsf = drv_scan_res->tsf;
        r->age = drv_scan_res->age;
        r->est_throughput = drv_scan_res->est_throughput;
        r->snr = drv_scan_res->snr;
        r->parent_tsf = drv_scan_res->parent_tsf;
        memcpy(r->tsf_bssid, drv_scan_res->tsf_bssid, ETH_ALEN);
        r->beacon_newer = drv_scan_res->beacon_newer;
        r->ie_len = drv_scan_res->ie_len;
        r->beacon_ie_len = drv_scan_res->beacon_ie_len;

        /* Copy the IE data that follows drv_scan_result_t */
        pos = (u8 *)(drv_scan_res + 1);
        ies = (u8 *)((char *)r + sizeof(*r));
        if (r->ie_len)
            memcpy(ies, pos, r->ie_len);
        if (r->beacon_ie_len && drv_scan_res->ie_len + r->beacon_ie_len > 0)
            memcpy(ies + r->ie_len, pos + drv_scan_res->ie_len, r->beacon_ie_len);

        tmp[wpa_results->num++] = r;

        /* Free driver-side allocations for this entry */
        if (drv_scan_res->ssids.ssid)
            os_free((void *) drv_scan_res->ssids.ssid);
        os_free(drv_scan_res);
    }

    /* If no entries taken from per-driver list, try the global list. */
    if (wpa_results->num == 0) {
        while ((drv_scan_res = dl_list_first(&drv_scan_results_list, drv_scan_result_t, list)) != NULL) {
            dl_list_del(&drv_scan_res->list);

            if (wpa_results->num >= MAX_SCAN_RESULTS) {
                if (drv_scan_res->ssids.ssid)
                    os_free((void *) drv_scan_res->ssids.ssid);
                os_free(drv_scan_res);
                continue;
            }

            alloc_size = sizeof(*r) + drv_scan_res->ie_len + drv_scan_res->beacon_ie_len;
            r = os_zalloc(alloc_size);
            if (!r) {
                if (drv_scan_res->ssids.ssid)
                    os_free((void *) drv_scan_res->ssids.ssid);
                os_free(drv_scan_res);
                break;
            }

            r->flags = drv_scan_res->flags;
            memcpy(r->bssid, drv_scan_res->bssid, ETH_ALEN);
            r->freq = drv_scan_res->freq;
            r->max_cw = drv_scan_res->max_cw;
            r->beacon_int = drv_scan_res->beacon_int;
            r->caps = drv_scan_res->caps;
            r->qual = drv_scan_res->qual;
            r->noise = drv_scan_res->noise;
            r->level = drv_scan_res->level;
            r->tsf = drv_scan_res->tsf;
            r->age = drv_scan_res->age;
            r->est_throughput = drv_scan_res->est_throughput;
            r->snr = drv_scan_res->snr;
            r->parent_tsf = drv_scan_res->parent_tsf;
            memcpy(r->tsf_bssid, drv_scan_res->tsf_bssid, ETH_ALEN);
            r->beacon_newer = drv_scan_res->beacon_newer;
            r->ie_len = drv_scan_res->ie_len;
            r->beacon_ie_len = drv_scan_res->beacon_ie_len;

            pos = (u8 *)(drv_scan_res + 1);
            ies = (u8 *)((char *)r + sizeof(*r));
            if (r->ie_len)
                memcpy(ies, pos, r->ie_len);
            if (r->beacon_ie_len && drv_scan_res->ie_len + r->beacon_ie_len > 0)
                memcpy(ies + r->ie_len, pos + drv_scan_res->ie_len, r->beacon_ie_len);

            tmp[wpa_results->num++] = r;

            if (drv_scan_res->ssids.ssid)
                os_free((void *) drv_scan_res->ssids.ssid);
            os_free(drv_scan_res);
        }
    }

    wpa_results->res = tmp;
    /* wpa_results->num is size_t; use %zu */
    SYNA_DRV_LOG(MSG_INFO, "get_scan_results returning %zu entries", wpa_results->num);
    return wpa_results;
}

static u32 wpa_cipher_to_cipher_suite(unsigned int cipher)
{
	switch (cipher) {
	case WPA_CIPHER_CCMP_256:
		return RSN_CIPHER_SUITE_CCMP_256;
	case WPA_CIPHER_GCMP_256:
		return RSN_CIPHER_SUITE_GCMP_256;
	case WPA_CIPHER_CCMP:
		return RSN_CIPHER_SUITE_CCMP;
	case WPA_CIPHER_GCMP:
		return RSN_CIPHER_SUITE_GCMP;
	case WPA_CIPHER_TKIP:
		return RSN_CIPHER_SUITE_TKIP;
	case WPA_CIPHER_WEP104:
		return RSN_CIPHER_SUITE_WEP104;
	case WPA_CIPHER_WEP40:
		return RSN_CIPHER_SUITE_WEP40;
	case WPA_CIPHER_GTK_NOT_USED:
		return RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED;
	}

	return 0;
}

int drv_syna_assoc(void *priv, struct wpa_driver_associate_params *params)
{
    driver_syna_data_t *drv;
    struct wpa_supplicant *wpa_s;
    sup_adap_msg_t msg;


    if (!priv || !params) {
        SYNA_DRV_LOG(MSG_ERROR, "%s invalid args", __func__);
        return -1;
    }
    drv = priv;
    wpa_s = drv->ctx;

    /* Ignore Assoc request from supplicant when operating in AP mode,
     * since AP mode configs are handled in .set_ap driver API
     */
    if (params->mode == IEEE80211_MODE_AP) {
        if (drv_syna_set_mhd_interface(drv, MHD_AP_INTERFACE,
                                       "associate-ap") != 0) {
            SYNA_DRV_LOG(MSG_ERROR,
                         "failed to select AP MHD interface for ifname=%s",
                         drv->ifname);
            return -1;
        }

        if (params->freq.freq)
            drv->assoc_freq = params->freq.freq;
        return 0;
    }
    if (params->freq.freq) {
        drv->assoc_freq = params->freq.freq;
        SYNA_DRV_LOG(MSG_DEBUG, "assoc freq=%d (freq)", drv->assoc_freq);
    } else if (params->freq_hint) {
        drv->assoc_freq = params->freq_hint;
        SYNA_DRV_LOG(MSG_DEBUG, "assoc freq=%d (freq_hint)", drv->assoc_freq);
    } else {
        drv->assoc_freq = 0;
    SYNA_DRV_LOG(MSG_DEBUG, "assoc freq=%d", drv->assoc_freq);
    }
        
    /* Fill bandwidth from BSS cache when available; do not block association if unavailable.
     * When WPA_DRIVER_FLAGS_BSS_SELECTION is advertised, supplicant leaves params->bssid NULL
     * on initial association and only provides params->bssid_hint (firmware is expected to do
     * the final BSS pick / roaming). Fall back to the hint so we can still derive bandwidth.
     */
    {
        const u8 *target_bssid = params->bssid ? params->bssid : params->bssid_hint;

        drv_syna_prepare_assoc_bandwidth(wpa_s, target_bssid, params);
    }

    if (params->ssid && params->ssid_len) {
        os_memcpy(drv->ssid, params->ssid, params->ssid_len);
        drv->ssid_len = params->ssid_len;
    }
    if (params->pairwise_suite != WPA_CIPHER_NONE) {
        u32 cipher = wpa_cipher_to_cipher_suite(params->pairwise_suite);
        SYNA_DRV_LOG(MSG_DEBUG, "  * pairwise=0x%x", cipher);
    }
    if (params->group_suite != WPA_CIPHER_NONE) {
        u32 cipher = wpa_cipher_to_cipher_suite(params->group_suite);
        SYNA_DRV_LOG(MSG_DEBUG, "  * group=0x%x", cipher);
    }
    SYNA_DRV_LOG(MSG_INFO, "associate ssid=%s len=%zu freq=%d", wpa_ssid_txt(drv->ssid, drv->ssid_len), drv->ssid_len, drv->assoc_freq);

    /* Promote STA interface to P2P group if association flagged as P2P. */
    if (params->p2p) {
        drv->is_p2p_group = 1;
        if (drv->mhd_int == MHD_STA_INTERFACE)
            drv->mhd_int = MHD_P2P_INTERFACE; /* reuse P2P interface enum */
    }

    msg.type = SUP_ADAP_CMD_ASSOC_REQ;
    msg.msg = os_zalloc(sizeof(struct wpa_driver_associate_params));
    if (msg.msg != NULL) {
        struct wpa_driver_associate_params *assoc = msg.msg;
        os_memcpy(msg.msg, params, sizeof(struct wpa_driver_associate_params));
        /* NULL out heap-pointer fields so cleanup doesn't free caller's buffers */
        assoc->ssid = NULL;
        assoc->psk = NULL;
        assoc->passphrase = NULL;
        assoc->sae_password = NULL;

        if (params->ssid && params->ssid_len) {
            assoc->ssid = os_zalloc(params->ssid_len);
            if (!assoc->ssid)
                goto assoc_alloc_fail;
            os_memcpy((void *)assoc->ssid, params->ssid, params->ssid_len);
            assoc->ssid_len = params->ssid_len;
        } else {
            assoc->ssid = NULL;
            assoc->ssid_len = 0;
        }
        assoc->group_suite = params->group_suite;
        assoc->pairwise_suite = params->pairwise_suite;
        if (params->psk) {
            assoc->psk = os_zalloc(PMK_LEN);
            if (!assoc->psk)
                goto assoc_alloc_fail;
            os_memcpy((void *)assoc->psk, params->psk, PMK_LEN);
        }
        if (params->passphrase) {
            assoc->passphrase = os_strdup(params->passphrase);
            if (!assoc->passphrase)
                goto assoc_alloc_fail;
        }
        if (params->sae_password) {
            assoc->sae_password = os_strdup(params->sae_password);
            if (!assoc->sae_password)
                goto assoc_alloc_fail;
            assoc->sae_pwe = params->sae_pwe;
        }
        if (params->wpa_ie_len) {
            os_memcpy((void *)assoc->wpa_ie, params->wpa_ie, params->wpa_ie_len);
        }
        SYNA_DRV_LOG(MSG_DEBUG, "assoc request dispatched to adaptor");
        /* If queue-put fails the adaptor never takes ownership;
         * reuse the alloc-fail cleanup to avoid leaking nested buffers.
         */
        if (send_msg_to_sup_adap(&msg) != 0)
            goto assoc_alloc_fail;
    }
    else {
        SYNA_DRV_LOG(MSG_ERROR, "%s alloc failed for assoc msg", __func__);
        return -1;
    }

    return 0;

assoc_alloc_fail:
    {
        struct wpa_driver_associate_params *a = msg.msg;
        if (a->sae_password) os_free((void *)a->sae_password);
        if (a->passphrase) os_free((void *)a->passphrase);
        if (a->psk) os_free((void *)a->psk);
        if (a->ssid) os_free((void *)a->ssid);
    }
    os_free(msg.msg);
    SYNA_DRV_LOG(MSG_ERROR, "%s failed to prepare/send assoc params", __func__);
    return -1;
}

static void drv_syna_log_key_flags(enum key_flag key_flag)
{
    SYNA_DRV_LOG(MSG_DEBUG,
                 "set_key flag bits: MOD=%d DEF=%d RX=%d TX=%d GRP=%d PAIR=%d PMK=%d",
                 !!(key_flag & KEY_FLAG_MODIFY),
                 !!(key_flag & KEY_FLAG_DEFAULT),
                 !!(key_flag & KEY_FLAG_RX),
                 !!(key_flag & KEY_FLAG_TX),
                 !!(key_flag & KEY_FLAG_GROUP),
                 !!(key_flag & KEY_FLAG_PAIRWISE),
                 !!(key_flag & KEY_FLAG_PMK));
}

int drv_syna_set_key(void *priv, struct wpa_driver_set_key_params *params)
{
    driver_syna_data_t *drv = priv;
    sup_adap_msg_t msg;
    mhd_wifi_key_params_t *kparams;
    if (!drv || !params)
        return -1;
    int deleting = (params->alg == WPA_ALG_NONE);

    int is_pairwise = 0;
    const u8 *addr = params->addr;
    if (addr && !is_broadcast_ether_addr(addr))
        is_pairwise = 1;

    if (!deleting) {
        if (check_key_flag(params->key_flag)) {
            SYNA_DRV_LOG(MSG_ERROR, "set_key invalid key_flag");
            return -1;
        }
        if (!is_pairwise) {
            if ((params->key_flag & KEY_FLAG_PAIRWISE) ||
                !(params->key_flag & KEY_FLAG_GROUP)) {
                SYNA_DRV_LOG(MSG_ERROR, "set_key invalid key_flag combination for group key");
                return -1;
            }
        } else if (!(params->key_flag & KEY_FLAG_PAIRWISE)) {
            SYNA_DRV_LOG(MSG_ERROR, "set_key pairwise addr but missing KEY_FLAG_PAIRWISE");
            return -1;
        }
    }

    SYNA_DRV_LOG(MSG_DEBUG,
        "set_key %s ifname=%s alg=%d pairwise=%d idx=%d tx=%d seq_len=%u key_len=%u flags=0x%x",
        deleting ? "delete" : "install",
        params->ifname ? params->ifname : "(null)", params->alg,
        is_pairwise, params->key_idx, params->set_tx, (unsigned)params->seq_len,
        (unsigned)params->key_len, params->key_flag);
    drv_syna_log_key_flags(params->key_flag);
    if (drv_syna_validate_key_install(deleting, is_pairwise, params) != 0)
        return -1;

    msg.type = SUP_ADAP_CMD_NEW_KEY;
    msg.msg = os_zalloc(sizeof(mhd_wifi_key_params_t));
    if (!msg.msg) {
        SYNA_DRV_LOG(MSG_ERROR, "%s alloc failed", __func__);
        return -1;
    }
    kparams = msg.msg;
    if (!deleting) {
        kparams->key = os_zalloc(params->key_len);
        if (!kparams->key) {
            SYNA_DRV_LOG(MSG_ERROR, "%s key alloc failed", __func__);
            os_free(msg.msg);
            return -1;
        }
        kparams->wpa_alg = params->alg;
        os_memcpy((void *)kparams->key, params->key, params->key_len);
        kparams->key_len = (uint16_t) params->key_len;
    } else {
        kparams->wpa_alg = WPA_ALG_NONE;
        kparams->key = NULL;
        kparams->key_len = 0; /* deletion */
    }
    kparams->key_index = (uint8_t) params->key_idx;
    kparams->set_tx = (uint8_t) params->set_tx;
    kparams->is_pairwise = (uint8_t) is_pairwise;
    if (!deleting && params->seq && params->seq_len) {
        kparams->seq = params->seq;
        kparams->seq_len = (uint8_t) params->seq_len;
    }
    if (is_pairwise && addr) {
        static mhd_mac_t peer;
        os_memcpy(peer.octet, addr, ETH_ALEN);
        kparams->peer_mac = &peer;
    }
    kparams->interface = drv->mhd_int;
    kparams->link_id  = params->link_id;

    if (send_msg_to_sup_adap(&msg) != 0) {
        SYNA_DRV_LOG(MSG_ERROR, "%s failed to enqueue set_key msg", __func__);
        os_free((void *)kparams->key); /* safe even when key==NULL (delete case) */
        os_free(msg.msg);
        return -1;
    }

    return 0;
}

int drv_syna_deauth(void *priv, const u8 *addr, u16 reason_code)
{
    driver_syna_data_t *drv = (driver_syna_data_t *) priv;
    sup_adap_msg_t msg;

    if (!drv) {
        SYNA_DRV_LOG(MSG_ERROR, "deauth invalid args");
        return -1;
    }

    SYNA_DRV_LOG(MSG_INFO, "deauth reason=%u iface=%d", reason_code, drv->mhd_int);

    /* Send disconnect request to adaptor task which will call mhd_wifi_api_disconnect */
    msg.type = SUP_ADAP_CMD_DEAUTHENTICATE;
    msg.msg = os_zalloc(sizeof(mhd_interface_t));
    if (msg.msg != NULL) {
        mhd_interface_t *iface = msg.msg;
        *iface = drv->mhd_int;

        SYNA_DRV_LOG(MSG_DEBUG, "deauth request dispatched to adaptor");
        send_msg_to_sup_adap(&msg);
    } else {
        SYNA_DRV_LOG(MSG_ERROR, "deauth alloc failed for disconnect msg");
        return -1;
    }

    /* Since it is locally generated disconnect, supplicant has already
     * generated DEAUTH event.
     */
    drv->ignore_next_local_deauth = 1;

    return 0;
}

int drv_syna_get_ssid (void *priv, u8 *ssid)
{
    struct driver_syna_data *drv = priv;
    mhd_interface_t iface = drv ? drv->mhd_int : MHD_STA_INTERFACE;
    mhd_ssid_t ssid_struct;
    mhd_result_t rc;

    if (!drv || !ssid)
        return -1;

    /* TODO: Currently this is a WAR */
    if (drv->ssid_len) {
        os_memcpy(ssid, drv->ssid, drv->ssid_len);
        return drv->ssid_len;
    }

    os_memset(&ssid_struct, 0, sizeof(ssid_struct));
    rc = mhd_wifi_api_get_ssid(iface, &ssid_struct);
    if (rc == MHD_SUCCESS) {
        /* Cache latest SSID in driver state */
        if (ssid_struct.length > 0) {
            os_memcpy(drv->ssid, ssid_struct.value, ssid_struct.length);
            os_memcpy(ssid, ssid_struct.value, ssid_struct.length);
        }
        drv->ssid_len = ssid_struct.length;
    SYNA_DRV_LOG(MSG_DEBUG, "get_ssid -> len=%d", (int)ssid_struct.length);
    return ssid_struct.length; /* length per wpa_supplicant contract */
    }

    if (rc == MHD_NOTASSOCIATED) { /* Not associated */
        drv->ssid_len = 0;
        /* Optional: zero out caller buffer to be explicit */
        os_memset(ssid, 0, 32); /* supplicant alloc is 32 bytes */
    SYNA_DRV_LOG(MSG_DEBUG, "get_ssid not-associated -> len=0");
    return 0; /* zero length indicates no association */
    }

    /* Underlying failure */
    SYNA_DRV_LOG(MSG_ERROR, "get_ssid underlying error rc=%d", (int)rc);
    return -1;
}

int drv_syna_get_capa(void *priv, struct wpa_driver_capa *capa)
{
    driver_syna_data_t *drv = priv;
    if (!drv)
        return -1;
    if (!drv->has_capability) {
        /* Lazy fetch if not already populated */
        if (driver_syna_capa(drv) != 0)
            return -1;
    }
    os_memcpy(capa, &drv->capa, sizeof(*capa));
    SYNA_DRV_LOG(MSG_DEBUG, "get_capa copy out success");
    return 0;
}

int drv_syna_get_bssid(void *priv, u8 *bssid)
{
    struct driver_syna_data *drv = priv;
    mhd_mac_t mac;
    mhd_result_t rc;
    mhd_interface_t iface = drv ? drv->mhd_int : MHD_STA_INTERFACE;
    uint8_t zero_addr[ETH_ALEN];

    if (!drv || !bssid)
        return -1;

    /* In fake AP mode always present zero BSSID so supplicant doesn't emit CONNECTED as STA */
    if (drv->mhd_int == MHD_AP_INTERFACE) {
        os_memset(bssid, 0, ETH_ALEN);
        return 0;
    }

    os_memset(zero_addr, 0, ETH_ALEN);
    if (os_memcmp(zero_addr, drv->bssid, ETH_ALEN)) {
        os_memcpy(bssid, drv->bssid, ETH_ALEN);
        return 0;
    }

    rc = mhd_wifi_api_get_bssid(iface, &mac);
    SYNA_DRV_LOG(MSG_DEBUG, "get_bssid iface=%d rc=%d prev_assoc=%d", iface, (int)rc, drv?drv->associated: -1);
    if (rc == MHD_SUCCESS) {
        os_memcpy(drv->bssid, mac.octet, ETH_ALEN);
        os_memcpy(bssid, mac.octet, ETH_ALEN);
        drv->associated = 1;
        SYNA_DRV_LOG(MSG_DEBUG, "get_bssid -> %02x:%02x:%02x:%02x:%02x:%02x",
                     bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5]);
        return 0;
    }
    if (rc == MHD_NOTASSOCIATED) {
        os_memset(drv->bssid, 0, ETH_ALEN);
        os_memset(bssid, 0, ETH_ALEN);
        drv->associated = 0;
        SYNA_DRV_LOG(MSG_DEBUG, "get_bssid not-associated -> zero MAC");
        return 0;
    }
    os_memset(bssid, 0, ETH_ALEN);
    SYNA_DRV_LOG(MSG_DEBUG, "get_bssid error rc=%d -> zero MAC", (int)rc);
    return -1;
}

const char * drv_syna_get_ifname(void *priv)
{
    struct driver_syna_data *drv = priv;
    if (!drv || drv->ifname[0] == '\0') {
        SYNA_DRV_LOG(MSG_DEBUG, "get_ifname fallback->wlan0");
        return "wlan0"; /* fallback */
    }
    SYNA_DRV_LOG(MSG_DEBUG, "get_ifname -> %s", drv->ifname);
    return drv->ifname;
}
const u8 *drv_syna_get_mac_addr(void *priv)
{
    driver_syna_data_t *drv = priv;

    if (!drv || !drv->own_addr_valid)
        return NULL;

    return drv->own_addr;
}

/**
 * send_mlme - Send management frame from MLME
 * Generic MLME TX path (management frame injection)
 */
int drv_syna_send_mlme(void *priv, const u8 *data, size_t data_len,
                              int noack, unsigned int freq, const u16 *csa_offs,
                              size_t csa_offs_len, int no_encrypt,
                              unsigned int wait, int link_id)
{
    (void)csa_offs; (void)csa_offs_len; (void)wait; (void)link_id; /* not mapped presently */
    driver_syna_data_t *drv;
    struct ieee80211_mgmt *mgmt;
    u16 fc;
    uint16_t flags = 0;
    uint8_t channel = 0;
    mhd_result_t rc;

    if (!priv || !data || !data_len) {
        SYNA_DRV_LOG(MSG_ERROR, "send_mlme invalid args");
        return -1;
    }
    drv = priv;
    mgmt = (struct ieee80211_mgmt *) data;

    fc = le_to_host16(mgmt->frame_control);
    SYNA_DRV_LOG(MSG_DEBUG, "drv_syna_send_mlme: send_mlme - da=" MACSTR
        " noack=%d freq=%u no_encrypt=%d fc=0x%x (%s) iface=%d",
        MAC2STR(mgmt->da), noack, freq, no_encrypt, fc, fc2str(fc), drv->mhd_int);

    if (noack)
        flags |= MHD_WIFI_AP_MLME_FLAG_NOACK;

    SYNA_DRV_LOG(MSG_DEBUG, "drv_syna_send_mlme: CMD_FRAME freq=%u noack=%d",
        freq, noack);

    if (WLAN_FC_GET_TYPE(fc) == WLAN_FC_TYPE_MGMT) {
        if (WLAN_FC_GET_STYPE(fc) == WLAN_FC_STYPE_AUTH) {
            mhd_wifi_mlme_tx_params_t tx = {0};
            tx.frame = data;
            if (data_len > UINT16_MAX)
                return -1;
            tx.frame_len = (uint16_t)data_len;
            tx.channel = channel;
            tx.dwell_tu = 0;
            tx.flags = flags;
            tx.interface = drv->mhd_int;
            rc = mhd_wifi_api_send_auth(&tx);
            if (rc != 0) {
                SYNA_DRV_LOG(MSG_ERROR, "send_mlme hw rc=%d len=%u", (int)rc, (unsigned)data_len);
                return -1; /* propagate failure */
            }
            SYNA_DRV_LOG(MSG_DEBUG, "send_mlme len=%u flags=0x%x", (unsigned)data_len, flags);
        } else {
            SYNA_DRV_LOG(MSG_ERROR, "send_mlme: unsupported management frame subtype");
            return -1;
        }
    } else {
        SYNA_DRV_LOG(MSG_ERROR, "Driver only allows MGMT packet type");
        return -1;
    }

    return 0;
}

/**
 * Called by the framework - Indicate the status of external
 * authentication processing to the host driver.
 */
int drv_syna_sta_external_auth(void *priv, struct external_auth *params)
{
    driver_syna_data_t *drv = priv;
    mhd_result_t rc = MHD_SUCCESS;

    if (!drv || !params) {
        SYNA_DRV_LOG(MSG_ERROR, "invalid args");
        return -1;
    }
    /* Do not send the status to drivers which do not support AP SME or use wpa_supplicant/hostapd SME. */
    if (drv->mhd_int == MHD_AP_INTERFACE) {
        return 0;
    }

    SYNA_DRV_LOG(MSG_DEBUG, "drv_syna_sta_external_auth: External auth status: %u", params->status);
    /* Issue disassoc on Auth failure */
    if (params->status) {
        /* Auth response status is not OK - nothing to send to driver */
        return 0;
    }

    rc = mhd_wifi_api_assoc_mgr_cmd(WL_ASSOC_MGR_CMD_PAUSE_ON_EVT,
        WL_ASSOC_MGR_PARAMS_EVENT_NONE);
    if (rc) {
        SYNA_DRV_LOG(MSG_ERROR, "External Auth status update failed: ret=%d", (int)rc);
    }

    return rc;
}

#ifndef ZEPHYR_OS
static int drv_syna_send_action(void *priv, unsigned int freq,
                                unsigned int wait, const u8 *dst,
                                const u8 *src, const u8 *bssid,
                                const u8 *data, size_t data_len,
                                int no_cck, int link_id)
{
    driver_syna_data_t *drv = priv;
    mhd_mac_t da;
    mhd_result_t rc;

    (void)no_cck;
    (void)link_id;

    // data_len > 0xFFFF: guard against silent truncation when casting
    if (!drv || !dst || !src || !bssid || !data || data_len == 0 || data_len > 0xFFFF) {
        SYNA_DRV_LOG(MSG_ERROR, "send_action invalid args");
        return -1;
    }

    os_memcpy(da.octet, dst, ETH_ALEN);

    wpa_printf(MSG_DEBUG, "DRV_SYNA: send_action dst=" MACSTR
               " src=" MACSTR " bssid=" MACSTR
               " freq=%u wait=%u iface=%d len=%u",
               MAC2STR(dst), MAC2STR(src), MAC2STR(bssid),
               freq, wait, drv->mhd_int, (unsigned)data_len);
    wpa_hexdump(MSG_DEBUG, "DRV_SYNA: send_action frame body", data, data_len);

    rc = mhd_wifi_api_send_action_frame(drv->mhd_int, &da, data,
                                        (uint16_t)data_len, 0);
    if (rc != (mhd_result_t) MHD_ACTION_FRAME_STATUS_OK) {
        SYNA_DRV_LOG(MSG_ERROR, "send_action failed rc=%d len=%u",
                     (int)rc, (unsigned)data_len);
        return -1;
    }

    wpa_printf(MSG_DEBUG, "DRV_SYNA: send_action OK rc=%d", (int)rc);
    SYNA_DRV_LOG(MSG_DEBUG, "send_action dst=" MACSTR " len=%u iface=%d",
                 MAC2STR(dst), (unsigned)data_len, drv->mhd_int);
    return 0;
}

static int drv_syna_add_pmkid(void *priv, struct wpa_pmkid_params *params)
{
    driver_syna_data_t *drv = priv;
    mhd_result_t rc = MHD_SUCCESS;

    if (!drv) {
        SYNA_DRV_LOG(MSG_ERROR, "invalid args");
        return -1;
    }

    if (params->bssid)
        SYNA_DRV_LOG(MSG_DEBUG, "drv_syna_sta_add_pmkid: Add PMKID for " MACSTR,
                MAC2STR(params->bssid));
    else if (params->fils_cache_id && params->ssid_len) {
        SYNA_DRV_LOG(MSG_DEBUG,
                "drv_syna_sta_add_pmkid: Add PMKSA for cache id %02x%02x SSID %s",
                params->fils_cache_id[0], params->fils_cache_id[1],
                wpa_ssid_txt(params->ssid, params->ssid_len));
    }
    mhd_wifi_pmksa_t pmksa = {0};
    pmksa.bssid = params->bssid;
    pmksa.pmkid = params->pmkid;
    pmksa.pmk = params->pmk;
    pmksa.pmk_len = params->pmk_len;
    pmksa.ssid_len = params->ssid_len;
    pmksa.ssid = params->ssid;
    pmksa.cache_id = params->fils_cache_id;
    pmksa.pmk_lifetime = params->pmk_lifetime;
    pmksa.pmk_reauth_threshold = params->pmk_reauth_threshold;
    rc = mhd_wifi_api_update_pmksa(&pmksa, TRUE);
    if (rc) {
        SYNA_DRV_LOG(MSG_ERROR, "Add pmksa failed: ret=%d", (int)rc);
    }

    return rc;
}

static int drv_syna_remove_pmkid(void *priv, struct wpa_pmkid_params *params)
{
    driver_syna_data_t *drv = priv;
    mhd_result_t rc = MHD_SUCCESS;

    if (!drv) {
        SYNA_DRV_LOG(MSG_ERROR, "invalid args");
        return -1;
    }

    if (params->bssid)
        SYNA_DRV_LOG(MSG_DEBUG, "drv_syna_remove_pmkid: Delete PMKID for " MACSTR,
               MAC2STR(params->bssid));
    else if (params->fils_cache_id && params->ssid_len) {
        SYNA_DRV_LOG(MSG_DEBUG,
                "drv_syna_remove_pmkid: Delete PMKSA for cache id %02x%02x SSID %s",
                params->fils_cache_id[0], params->fils_cache_id[1],
                wpa_ssid_txt(params->ssid, params->ssid_len));
    }
    mhd_wifi_pmksa_t pmksa = {0};
    pmksa.bssid = params->bssid;
    pmksa.pmkid = params->pmkid;
    pmksa.pmk = params->pmk;
    pmksa.pmk_len = params->pmk_len;
    pmksa.ssid_len = params->ssid_len;
    pmksa.ssid = params->ssid;
    pmksa.cache_id = params->fils_cache_id;
    pmksa.pmk_lifetime = params->pmk_lifetime;
    pmksa.pmk_reauth_threshold = params->pmk_reauth_threshold;
    rc = mhd_wifi_api_update_pmksa(&pmksa, FALSE);
    if (rc) {
        SYNA_DRV_LOG(MSG_ERROR, "Delete pmksa failed: ret=%d", (int)rc);
    }

    return rc;
}

static int drv_syna_flush_pmkid(void *priv)
{
    driver_syna_data_t *drv = priv;
    mhd_result_t rc = MHD_SUCCESS;

    if (!drv) {
        SYNA_DRV_LOG(MSG_ERROR, "invalid args");
        return -1;
    }
    SYNA_DRV_LOG(MSG_DEBUG, "drv_syna_flush_pmkid: Flush PMKIDs");

    rc = mhd_wifi_api_update_pmksa(NULL, FALSE);
    if (rc) {
        SYNA_DRV_LOG(MSG_ERROR, "Flush pmksa failed: ret=%d", (int)rc);
    }

    return rc;
}
#endif

#ifdef CONFIG_AP
/* Minimal translation of AP params -> firmware start API. */
static int drv_syna_set_ap(void *priv, struct wpa_driver_ap_params *params)
{
    driver_syna_data_t *drv = priv;
    if (!drv || !params) {
        SYNA_DRV_LOG(MSG_ERROR, "set_ap invalid args");
        return -1;
    }
    if (drv_syna_set_mhd_interface(drv, MHD_AP_INTERFACE, "set_ap") != 0) {
        SYNA_DRV_LOG(MSG_ERROR,
                     "failed to select AP MHD interface for ifname=%s",
                     drv->ifname);
        return -1;
    }

    /* Log basic parameters */
    const u8 *ssid = params->ssid;
    size_t ssid_len = params->ssid_len;
    SYNA_DRV_LOG(MSG_INFO, "set_ap entry ssid=%s len=%u privacy=%d beacon_int=%d hw_start=%s",
                 ssid_len ? wpa_ssid_txt(ssid, ssid_len) : "(none)", (unsigned)ssid_len,
                 params->privacy, params->beacon_int, drv->ap_started?"already":"pending");

    if (params->pairwise_ciphers != WPA_CIPHER_NONE) {
        u32 cipher = wpa_cipher_to_cipher_suite(params->pairwise_ciphers);
        SYNA_DRV_LOG(MSG_DEBUG, "  * pairwise=0x%x", cipher);
    }
    if (params->group_cipher != WPA_CIPHER_NONE) {
        u32 cipher = wpa_cipher_to_cipher_suite(params->group_cipher);
        SYNA_DRV_LOG(MSG_DEBUG, "  * group=0x%x", cipher);
    }

    /* If already started, treat as reconfiguration: stop then start fresh */
    if (drv->ap_started) {
        int32_t rc_stop = mhd_wifi_api_stop_ap(MHD_AP_INTERFACE, MHD_TRUE);
        SYNA_DRV_LOG(MSG_INFO, "set_ap reconfig: stop_ap rc=%ld", (long)rc_stop);
        drv->ap_started = 0;
    }

    /* Package AP params in sup_adap_msg_t and post to adaptor task */
    sup_adap_msg_t msg;
    msg.type = SUP_ADAP_CMD_SET_AP;
    msg.cb_ctx = drv->ctx;
    msg.drv_priv = drv;
    msg.msg = os_zalloc(sizeof(struct wpa_driver_ap_params));
    struct hostapd_freq_params *freq_param = os_zalloc(sizeof(struct hostapd_freq_params));
    if (msg.msg != NULL) {
        os_memcpy(msg.msg, params, sizeof(struct wpa_driver_ap_params));
        /* Deep copy frequency struct to avoid dangling pointer */
        os_memcpy(freq_param, ((struct wpa_driver_ap_params *)msg.msg)->freq,
                  sizeof(struct hostapd_freq_params));
        ((struct wpa_driver_ap_params *)msg.msg)->freq = freq_param;

        SYNA_DRV_LOG(MSG_DEBUG, "set_ap request dispatched to adaptor");
        send_msg_to_sup_adap(&msg);
    } else {
        SYNA_DRV_LOG(MSG_ERROR, "%s alloc failed for set_ap msg", __func__);
    }
     drv->mhd_int = MHD_AP_INTERFACE;
    return 0; /* Always return 0 so hostapd proceeds */
}

/* Station deauthentication */
extern mhd_result_t mhd_wifi_deauth_sta(const mhd_mac_t* mac, mhd_dot11_reason_code_t reason, mhd_interface_t interface);
static int drv_syna_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr,
                               u16 reason, int link_id)
{
    (void)own_addr; (void)link_id; /* Unused in current mapping */
    driver_syna_data_t *drv = priv;
    if (!drv || !addr) {
        SYNA_DRV_LOG(MSG_ERROR, "sta_deauth invalid args");
        return -1;
    }
    mhd_mac_t mac; os_memset(&mac, 0, sizeof(mac)); os_memcpy(mac.octet, addr, ETH_ALEN);
    int32_t rc = mhd_wifi_deauth_sta(&mac, reason, MHD_AP_INTERFACE);
    SYNA_DRV_LOG(rc==0?MSG_INFO:MSG_ERROR, "sta_deauth %02x:%02x:%02x:%02x:%02x:%02x reason=%u rc=%ld",
                 addr[0],addr[1],addr[2],addr[3],addr[4],addr[5], reason, (long)rc);
    return rc==0?0:-1;
}

/* Station disassociation */
static int drv_syna_sta_disassoc(void *priv, const u8 *own_addr, const u8 *addr,
                                 u16 reason,
                                 int link_id)
{
    (void)own_addr; /* Unused */
    (void)link_id; /* Unused in current mapping */
    driver_syna_data_t *drv = priv;
    if (!drv || !addr) {
        SYNA_DRV_LOG(MSG_ERROR, "sta_disassoc invalid args");
        return -1;
    }
    mhd_mac_t mac; os_memset(&mac, 0, sizeof(mac)); os_memcpy(mac.octet, addr, ETH_ALEN);
#if 0 // TODO: mhd API
    int32_t rc = mhd_wifi_api_ap_disassoc_sta(MHD_AP_INTERFACE, &mac, reason);
#else
    int32_t rc = 0;
#endif
    SYNA_DRV_LOG(rc==0?MSG_INFO:MSG_ERROR, "sta_disassoc %02x:%02x:%02x:%02x:%02x:%02x reason=%u rc=%ld",
                 addr[0],addr[1],addr[2],addr[3],addr[4],addr[5], reason, (long)rc);
    return rc==0?0:-1;
}

/* Remove station entry by deauth (reason 2) */
static int drv_syna_sta_remove(void *priv, const u8 *addr)
{
    driver_syna_data_t *drv = priv;
    if (!drv || !addr) {
        SYNA_DRV_LOG(MSG_ERROR, "sta_remove invalid args");
        return -1;
    }
    mhd_mac_t mac; os_memset(&mac, 0, sizeof(mac)); os_memcpy(mac.octet, addr, ETH_ALEN);
    const uint16_t reason = 2; /* WLAN_REASON_PREV_AUTH_NOT_VALID (common generic) */
    int32_t rc = mhd_wifi_deauth_sta(&mac, reason, MHD_AP_INTERFACE);
    int success = (rc == 0);
    SYNA_DRV_LOG(success?MSG_INFO:MSG_ERROR, "sta_remove %02x:%02x:%02x:%02x:%02x:%02x rc=%ld",
                 addr[0],addr[1],addr[2],addr[3],addr[4],addr[5], (long)rc);
    return success?0:-1;
}
static int drv_syna_hapd_send_eapol(void *priv, const u8 *addr, const u8 *data,
                               size_t data_len, int encrypt,
                               const u8 *own_addr, u32 flags, int link_id)
{
    driver_syna_data_t *drv = priv;
    mhd_mac_t src_mac_local, dest_mac_local;
    int32_t rc;
    u16 key_info = 0;
    if (!drv || !addr) {
        SYNA_DRV_LOG(MSG_ERROR, "send_eapol invalid args");
        return -1;
    }
    SYNA_DRV_LOG(MSG_INFO, "From %02x:%02x:%02x:%02x:%02x:%02x\n",
        own_addr[0], own_addr[1], own_addr[2], own_addr[3], own_addr[4], own_addr[5]);
    SYNA_DRV_LOG(MSG_INFO, "To %02x:%02x:%02x:%02x:%02x:%02x\n",
        addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    if (data && data_len >= 7)
        key_info = (u16) (((u16) data[5] << 8) | data[6]);
    SYNA_DRV_LOG(MSG_DEBUG,
                 "send_eapol len=%zu encrypt=%d flags=0x%x link_id=%d key_info=0x%04x",
                 data_len, encrypt, flags, link_id, key_info);
    os_memcpy(src_mac_local.octet, own_addr, ETH_ALEN);
    os_memcpy(dest_mac_local.octet, addr, ETH_ALEN);
    rc = mhd_wifi_api_send_eapol(MHD_AP_INTERFACE, &dest_mac_local, &src_mac_local, data, data_len);
    if (rc != 0) {
        SYNA_DRV_LOG(MSG_ERROR, "send_eapol tx failed rc=%ld key_info=0x%04x",
                     (long) rc, key_info);
        return -1;
    }
    return 0;
}
static int drv_syna_hapd_sta_set_flags(void *priv, const u8 *addr,
				       unsigned int total_flags,
				       unsigned int flags_or,
				       unsigned int flags_and)
{
    driver_syna_data_t *drv = priv;
    sup_adap_msg_t msg;
    uint32_t *flags;
    if (!drv || !addr) {
        SYNA_DRV_LOG(MSG_ERROR, "send_eapol invalid args");
        return -1;
    }
    SYNA_DRV_LOG(MSG_INFO, "Set STA flags - addr=" MACSTR
              " total_flags=0x%x flags_or=0x%x flags_and=0x%x authorized=%d",
               MAC2STR(addr), total_flags, flags_or, flags_and,
               !!(total_flags & WPA_STA_AUTHORIZED));
    if (total_flags & WPA_STA_AUTHORIZED)
        SYNA_DRV_LOG(MSG_INFO, "STA_AUTHORIZED\n");
    if (total_flags & WPA_STA_WMM)
        SYNA_DRV_LOG(MSG_INFO, "STA_WMM\n");
    if (total_flags & WPA_STA_SHORT_PREAMBLE)
        SYNA_DRV_LOG(MSG_INFO, "STA_SHORT_PREAMBLE\n");
    if (total_flags & WPA_STA_MFP)
        SYNA_DRV_LOG(MSG_INFO, "STA_MFP\n");
    if (total_flags & WPA_STA_TDLS_PEER)
        SYNA_DRV_LOG(MSG_INFO, "STA_TDLS_PEER\n");

    msg.type = SUP_ADAP_CMD_SET_STA_FLAG;
    msg.msg = os_zalloc(sizeof(total_flags) + ETH_ALEN);
    os_memcpy(msg.msg, addr, ETH_ALEN);
    flags = (uint32_t *)(msg.msg + ETH_ALEN);
    *flags = total_flags;
    SYNA_DRV_LOG(MSG_DEBUG, "set_sta_flag request dispatched to adaptor");
    // TODO: local verify can't identify the flags is working
    //send_msg_to_sup_adap(&msg);

    return 0;
}

#endif /* CONFIG_AP */

int drv_syna_set_supp_port(void *priv, int authorized)
{
       driver_syna_data_t *drv = (driver_syna_data_t*) priv;
       sup_adap_msg_t msg;
       mhd_mac_t mac_addr;
       uint32_t *mask;
       uint32_t *flags_set;
       uint8_t *msg_ptr = NULL;
       mhd_interface_t *iface = NULL;
           static const u8 zero_bssid[ETH_ALEN];

           if (!drv || os_memcmp(drv->bssid, zero_bssid, ETH_ALEN) == 0) {
               return -1;
       }

       os_memset(&mac_addr, 0, sizeof(mhd_mac_t));
       os_memcpy(mac_addr.octet, drv->bssid, ETH_ALEN);

       msg.type = SUP_ADAP_CMD_SET_SUPP_PORT;
       msg.msg  = os_zalloc(sizeof(mhd_interface_t) + sizeof(mhd_mac_t) + (2 * sizeof(uint32_t)));

       if (msg.msg != NULL) {

           iface    = (mhd_interface_t *)msg.msg;
           *iface   =  drv->mhd_int;

           msg_ptr  = (uint8_t*)(msg.msg) + sizeof(mhd_interface_t);

           os_memcpy(msg_ptr, (uint8_t*)&mac_addr, sizeof(mhd_mac_t));
           msg_ptr += sizeof(mhd_mac_t);

           mask = (uint32_t *)(msg_ptr);
           *mask = MHD_STA_AUTHORIZED;
           msg_ptr += sizeof(uint32_t);

           if(authorized) {
               flags_set = (uint32_t *)(msg_ptr);
               *flags_set = MHD_STA_AUTHORIZED;
           }

           SYNA_DRV_LOG(MSG_DEBUG, "set_supp_port request dispatched to adaptor");
           send_msg_to_sup_adap(&msg);

       } else {
           SYNA_DRV_LOG(MSG_ERROR, "%s alloc failed for set_supp_port msg", __func__);
       }
       return 0;
}

#ifndef ZEPHYR_OS
static int drv_syna_get_sta_mlo_info(void *priv, struct driver_sta_mlo_info *mlo_info)
{
    driver_syna_data_t *drv = priv;
    mhd_sta_mlo_info_t info;
    mhd_result_t rc;
    int i;

    if (!drv || !mlo_info)
        return -1;

    os_memset(mlo_info, 0, sizeof(*mlo_info));

    rc = mhd_wifi_api_get_sta_mlo_info(drv->mhd_int, &info);
    if (rc != MHD_SUCCESS) {
        /* Non-MLO association or query failed: return zeroed struct (valid_links=0). */
        SYNA_DRV_LOG(MSG_DEBUG, "drv_syna_get_sta_mlo_info: not MLO or query failed (rc=%d)", (int)rc);
        return 0;
    }

    if (!info.valid_links) {
        SYNA_DRV_LOG(MSG_DEBUG, "drv_syna_get_sta_mlo_info: valid_links=0, non-MLO association");
        return 0;
    }

    mlo_info->valid_links   = info.valid_links;
    mlo_info->req_links     = info.valid_links; /* for this driver req == accepted */
    mlo_info->assoc_link_id = info.assoc_link_id;
    os_memcpy(mlo_info->ap_mld_addr, info.ap_mld_addr.octet, ETH_ALEN);

    for_each_link(info.valid_links, i) {
        if (i >= ARRAY_SIZE(info.links))
            break;
        os_memcpy(mlo_info->links[i].bssid, info.links[i].bssid.octet, ETH_ALEN);
        os_memcpy(mlo_info->links[i].addr,  info.links[i].addr.octet,  ETH_ALEN);
        mlo_info->links[i].freq = info.links[i].freq;
        SYNA_DRV_LOG(MSG_DEBUG,
            "drv_syna_get_sta_mlo_info: link[%d] bssid=" MACSTR " addr=" MACSTR " freq=%u",
            i, MAC2STR(mlo_info->links[i].bssid),
            MAC2STR(mlo_info->links[i].addr),
            mlo_info->links[i].freq);
    }

    SYNA_DRV_LOG(MSG_DEBUG,
        "drv_syna_get_sta_mlo_info: valid_links=0x%x assoc_link_id=%u ap_mld=" MACSTR,
        mlo_info->valid_links, mlo_info->assoc_link_id,
        MAC2STR(mlo_info->ap_mld_addr));
    return 0;
}
#endif

#ifndef ZEPHYR_OS
const struct wpa_driver_ops wpa_driver_syna_ops = {
    .name = "Drv Syna",
    .desc = "Driver Synaptics Wifi Chips",

    .init = driver_syna_init,
    .deinit = driver_syna_deinit,

    .scan2 = drv_syna_scan_req,
    .get_scan_results2 = drv_syna_get_scan_results,

    .get_bssid = drv_syna_get_bssid,
    .get_ssid = drv_syna_get_ssid,

    .set_key = drv_syna_set_key,

    .associate = drv_syna_assoc,
    .deauthenticate = drv_syna_deauth,

    .get_capa = drv_syna_get_capa,
    .get_hw_feature_data = drv_syna_get_hw_feature_data,

    .send_action = drv_syna_send_action,
    .send_mlme = drv_syna_send_mlme,
    .send_external_auth_status = drv_syna_sta_external_auth,
    .get_sta_mlo_info = drv_syna_get_sta_mlo_info,
    .add_pmkid = drv_syna_add_pmkid,
    .remove_pmkid = drv_syna_remove_pmkid,
    .flush_pmkid = drv_syna_flush_pmkid,

    .get_ifname = drv_syna_get_ifname,
    .get_mac_addr = drv_syna_get_mac_addr,

    /* Allow supplicant to request stopping scheduled scans / abort scans */
    .stop_sched_scan = drv_syna_stop_sched_scan,
    .abort_scan      = drv_syna_abort_sched_scan,
    .set_supp_port   = drv_syna_set_supp_port,

#ifdef CONFIG_AP
    .hapd_init = drv_syna_hapd_init,
    .hapd_deinit = drv_syna_hapd_deinit,
    .set_ap = drv_syna_set_ap,
    .sta_deauth = drv_syna_sta_deauth,
    .sta_disassoc = drv_syna_sta_disassoc,
    .sta_remove = drv_syna_sta_remove,
    .hapd_send_eapol = drv_syna_hapd_send_eapol,
    .sta_set_flags = drv_syna_hapd_sta_set_flags,
    .deinit_ap = drv_syna_deinit_ap,
#endif
};

/* Register the Syna driver ops struct to Supplicant supported drivers' list */
const struct wpa_driver_ops *const wpa_drivers[] = {
    &wpa_driver_syna_ops,
    NULL
};
#endif
