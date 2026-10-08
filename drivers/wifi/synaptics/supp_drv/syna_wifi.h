/**
 * Copyright (c) 2026 Synaptics Incorporated
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file syna_wifi.h
 * contains Synaptics Wi-Fi driver definitions, API (Linkers) and structures
 */

#include "includes.h"
#include "common.h"
#include "drivers/driver.h"
#include "mhd_wifi_api.h"

#ifndef DRIVER_SYNA_H_
#define DRIVER_SYNA_H_

#include "syna_wifi_msg.h"

#ifdef ZEPHYR_OS
#include "drivers/driver_zephyr.h"
#endif

#define MAX_SCAN_RESULTS 64
#define MAX_BAND_SCAN_RESULTS 32

typedef struct driver_syna_data {
	struct hostapd_data *hapd;
	void *ctx;
	/* Interface identity */
	char ifname[32];
	mhd_interface_t mhd_int; /* MHD interface role (STA/AP/P2P) */

	/* Lifecycle flag */
	int initialized;

	/* Driver capabilities info */
	struct wpa_driver_capa capa;
	int has_capability;

	/* Scan related info */
	int aborted;
	u64 scan_start_tsf;
	u8 scan_start_tsf_bssid[ETH_ALEN];
	u64 scan_cookie;
	struct dl_list scan_result_list; /* list of drv_scan_result_t */

	/* Assoc realted info */
	u8 bssid[ETH_ALEN];
	u8 prev_bssid[ETH_ALEN];
	int associated;
	u8 ssid[SSID_MAX_LEN];
	size_t ssid_len;
	unsigned int assoc_freq;
    unsigned int connect_reassoc:1;

	/* P2P role indicators (minimal detection) */
	int is_p2p_group; /* 1 if this interface is a P2P group (client or GO) */

	/* Deauth/Disconnect info */
	u32 ignore_next_local_deauth;

	/* AP mode lifecycle */
	int ap_started; /* 1 if mhd_wifi_api_start_ap succeeded */

	unsigned int last_mgmt_freq;
#ifdef ZEPHYR_OS
	struct zep_wpa_supp_dev_callbk_fns cbs;
#endif

	/* Appended to preserve the layout expected by existing Zephyr objects. */
	u8 own_addr[ETH_ALEN];
	bool own_addr_valid;
} driver_syna_data_t;

uint32_t syna_adapt_scan(struct wpa_driver_scan_params *scan);
int drv_syna_scan_req(void *priv, struct wpa_driver_scan_params *params);
struct wpa_scan_results *drv_syna_get_scan_results(void *priv);
int drv_syna_assoc(void *priv, struct wpa_driver_associate_params *params);
int drv_syna_set_key(void *priv, struct wpa_driver_set_key_params *params);
int drv_syna_deauth(void *priv, const u8 *addr, u16 reason_code);
int drv_syna_get_capa(void *priv, struct wpa_driver_capa *capa);
int drv_syna_set_supp_port(void *priv, int authorized);
int drv_syna_process_msg(struct driver_syna_data *drv, drv_syna_msg_t *drv_msg);
int drv_syna_send_mlme(void *priv, const u8 *data, size_t data_len,
					   int noack, unsigned int freq, const u16 *csa_offs,
					   size_t csa_offs_len, int no_encrypt,
					   unsigned int wait, int link_id);
int drv_syna_sta_external_auth(void *priv, struct external_auth *params);

#endif