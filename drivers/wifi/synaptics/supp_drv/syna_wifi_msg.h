/**
 * Copyright (c) 2026 Synaptics Incorporated
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file syna_wifi_msg.h
 * contains msg/transport structures
 */

#ifndef DRIVER_SYNA_MSG_H_
#define DRIVER_SYNA_MSG_H_

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "cmsis_os2_port.h"
#include "common.h"
#include "drivers/driver.h"

/**
 * struct wpa_driver_capa - Driver capability information
 */
typedef struct host_driver_capa {
	/** Bitfield of supported key management suites */
	unsigned int key_mgmt;
	unsigned int key_mgmt_iftype[WPA_IF_MAX];

	/** Bitfield of supported cipher suites */
	unsigned int enc;

	/** Bitfield of supported IEEE 802.11 authentication algorithms */
	unsigned int auth;

	u64 flags;

	u64 flags2;

	unsigned int wmm_ac_supported:1;

	unsigned int mac_addr_rand_scan_supported:1;
	unsigned int mac_addr_rand_sched_scan_supported:1;

	/** Maximum number of supported active probe SSIDs */
	int max_scan_ssids;

	/** Maximum number of supported active probe SSIDs for sched_scan */
	int max_sched_scan_ssids;

	/** Maximum number of supported scan plans for scheduled scan */
	unsigned int max_sched_scan_plans;

	/** Maximum interval in a scan plan. In seconds */
	u32 max_sched_scan_plan_interval;

	/** Maximum number of iterations in a single scan plan */
	u32 max_sched_scan_plan_iterations;

	/** Whether sched_scan (offloaded scanning) is supported */
	int sched_scan_supported;

	/** Maximum number of supported match sets for sched_scan */
	int max_match_sets;

	/**
	 * max_remain_on_chan - Maximum remain-on-channel duration in msec
	 */
	unsigned int max_remain_on_chan;

	/**
	 * max_stations - Maximum number of associated stations the driver
	 * supports in AP mode
	 */
	unsigned int max_stations;

	/**
	 * probe_resp_offloads - Bitmap of supported protocols by the driver
	 * for Probe Response offloading.
	 */
	unsigned int probe_resp_offloads;

	unsigned int max_acl_mac_addrs;

	/**
	 * Number of supported concurrent channels
	 */
	unsigned int num_multichan_concurrent;

	/**
	 * extended_capa - extended capabilities in driver/device
	 *
	 * Must be allocated and freed by driver and the pointers must be
	 * valid for the lifetime of the driver, i.e., freed in deinit()
	 */
	const u8 *extended_capa, *extended_capa_mask;
	unsigned int extended_capa_len;

	struct wowlan_triggers wowlan_triggers;

	u32 rrm_flags;

	/* Driver concurrency capabilities */
	unsigned int conc_capab;
	/* Maximum number of concurrent channels on 2.4 GHz */
	unsigned int max_conc_chan_2_4;
	/* Maximum number of concurrent channels on 5 GHz */
	unsigned int max_conc_chan_5_0;

	/* Maximum number of supported CSA counters */
	u16 max_csa_counters;

	/* Maximum number of supported AKM suites in commands */
	unsigned int max_num_akms;

	/* Maximum number of interfaces supported for MBSSID advertisement */
	unsigned int mbssid_max_interfaces;
	/* Maximum profile periodicity for enhanced MBSSID advertisement */
	unsigned int ema_max_periodicity;
} host_driver_capa_t;

/**
 * struct wpa_scan_res - Scan result for an BSS/IBSS
 * @flags: information flags about the BSS/IBSS (WPA_SCAN_*)
 * @bssid: BSSID
 * @freq: frequency of the channel in MHz (e.g., 2412 = channel 1)
 * @max_cw: the max channel width of the connection (calculated during scan
 * result processing)
 * @beacon_int: beacon interval in TUs (host byte order)
 * @caps: capability information field in host byte order
 * @qual: signal quality
 * @noise: noise level
 * @level: signal level
 * @tsf: Timestamp
 * @age: Age of the information in milliseconds (i.e., how many milliseconds
 * ago the last Beacon or Probe Response frame was received)
 * @est_throughput: Estimated throughput in kbps (this is calculated during
 * scan result processing if left zero by the driver wrapper)
 * @snr: Signal-to-noise ratio in dB (calculated during scan result processing)
 * @parent_tsf: Time when the Beacon/Probe Response frame was received in terms
 * of TSF of the BSS specified by %tsf_bssid.
 * @tsf_bssid: The BSS that %parent_tsf TSF time refers to.
 * @beacon_newer: Whether the Beacon frame data is known to be newer
 * @ie_len: length of the following IE field in octets
 * @beacon_ie_len: length of the following Beacon IE field in octets
 *
 * This structure is used as a generic format for scan results from the
 * driver. Each driver interface implementation is responsible for converting
 * the driver or OS specific scan results into this format.
 *
 * If the driver does not support reporting all IEs, the IE data structure is
 * constructed of the IEs that are available. This field will also need to
 * include SSID in IE format. All drivers are encouraged to be extended to
 * report all IEs to make it easier to support future additions.
 *
 * This structure data is followed by ie_len octets of IEs from Probe Response
 * frame (or if the driver does not indicate source of IEs, these may also be
 * from Beacon frame). After the first set of IEs, another set of IEs may follow
 * (with beacon_ie_len octets of data) if the driver provides both IE sets.
 */

typedef struct drv_scan_result{
	struct dl_list list;
	u8 scan_completed;
	struct wpa_driver_scan_ssid ssids;
	unsigned int flags;
	u8 bssid[ETH_ALEN];
	int freq;
	enum chan_width max_cw;
	u16 beacon_int;
	u16 caps;
	int qual;
	int noise;
	int level;
	u64 tsf;
	unsigned int age;
	unsigned int est_throughput;
	int snr;
	u64 parent_tsf;
	u8 tsf_bssid[ETH_ALEN];
	bool beacon_newer;
	u64 scan_cookie;

	size_t ie_len;
	size_t beacon_ie_len;
	/* Followed by ie_len + beacon_ie_len octets of IE data */
} drv_scan_result_t;

/**
 * struct assoc_info - Data for EVENT_ASSOC and EVENT_ASSOCINFO events
 * This structure is optional for EVENT_ASSOC calls and required for
 * EVENT_ASSOCINFO calls. By using EVENT_ASSOC with this data, the
 * driver interface does not need to generate separate EVENT_ASSOCINFO
 * calls.
 * */

typedef struct drv_assoc_info {
	/**
	 * reassoc - Flag to indicate association or reassociation
	 */
	int reassoc;

	/**
	 * req_ies - (Re)Association Request IEs
	 *
	 * If the driver generates WPA/RSN IE, this event data must be
	 * returned for WPA handshake to have needed information. If
	 * wpa_supplicant-generated WPA/RSN IE is used, this
	 * information event is optional.
	 *
	 * This should start with the first IE (fixed fields before IEs
	 * are not included).
	 */
	const u8 *req_ies;

	/**
	 * req_ies_len - Length of req_ies in bytes
	 */
	size_t req_ies_len;

	/**
	 * resp_ies - (Re)Association Response IEs
	 *
	 * Optional association data from the driver. This data is not
	 * required WPA, but may be useful for some protocols and as
	 * such, should be reported if this is available to the driver
	 * interface.
	 *
	 * This should start with the first IE (fixed fields before IEs
	 * are not included).
	 */
	const u8 *resp_ies;

	/**
	 * resp_ies_len - Length of resp_ies in bytes
	 */
	size_t resp_ies_len;

	/**
	 * resp_frame - (Re)Association Response frame
	 */
	const u8 *resp_frame;

	/**
	 * resp_frame_len - (Re)Association Response frame length
	 */
	size_t resp_frame_len;

	/**
	 * beacon_ies - Beacon or Probe Response IEs
	 *
	 * Optional Beacon/ProbeResp data: IEs included in Beacon or
	 * Probe Response frames from the current AP (i.e., the one
	 * that the client just associated with). This information is
	 * used to update WPA/RSN IE for the AP. If this field is not
	 * set, the results from previous scan will be used. If no
	 * data for the new AP is found, scan results will be requested
	 * again (without scan request). At this point, the driver is
	 * expected to provide WPA/RSN IE for the AP (if WPA/WPA2 is
	 * used).
	 *
	 * This should start with the first IE (fixed fields before IEs
	 * are not included).
	 */
	const u8 *beacon_ies;

	/**
	 * beacon_ies_len - Length of beacon_ies */
	size_t beacon_ies_len;

	/**
	 * freq - Frequency of the operational channel in MHz
	 */
	unsigned int freq;

	/**
	 * wmm_params - WMM parameters used in this association.
	 */
	struct wmm_params wmm_params;

	/**
	 * addr - Station address (for AP mode)
	 */
	const u8 *addr;

	/**
	 * The following is the key management offload information
	 * @authorized
	 * @key_replay_ctr
	 * @key_replay_ctr_len
	 * @ptk_kck
	 * @ptk_kek_len
	 * @ptk_kek
	 * @ptk_kek_len
	 */

	/**
	 * authorized - Status of key management offload,
	 * 1 = successful
	 */
	int authorized;

	/**
	 * key_replay_ctr - Key replay counter value last used
	 * in a valid EAPOL-Key frame
	 */
	const u8 *key_replay_ctr;

	/**
	 * key_replay_ctr_len - The length of key_replay_ctr
	 */
	size_t key_replay_ctr_len;

	/**
	 * ptk_kck - The derived PTK KCK
	 */
	const u8 *ptk_kck;

	/**
	 * ptk_kek_len - The length of ptk_kck
	 */
	size_t ptk_kck_len;

	/**
	 * ptk_kek - The derived PTK KEK
	 * This is used in key management offload and also in FILS SK
	 * offload.
	 */
	const u8 *ptk_kek;

	/**
	 * ptk_kek_len - The length of ptk_kek
	 */
	size_t ptk_kek_len;

	/**
	 * subnet_status - The subnet status:
	 * 0 = unknown, 1 = unchanged, 2 = changed
	 */
	u8 subnet_status;

	/**
	 * The following information is used in FILS SK offload
	 * @fils_erp_next_seq_num
	 * @fils_pmk
	 * @fils_pmk_len
	 * @fils_pmkid
	 */

	/**
	 * fils_erp_next_seq_num - The next sequence number to use in
	 * FILS ERP messages
	 */
	u16 fils_erp_next_seq_num;

	/**
	 * fils_pmk - A new PMK if generated in case of FILS
	 * authentication
	 */
	const u8 *fils_pmk;

	/**
	 * fils_pmk_len - Length of fils_pmk
	 */
	size_t fils_pmk_len;

	/**
	 * fils_pmkid - PMKID used or generated in FILS authentication
	 */
	const u8 *fils_pmkid;

	/**
	 * link_addr - Link address of the STA
	 */
	const u8 *link_addr;

	/**
	 * assoc_link_id - Association link id of the MLO association or
	 * -1 if MLO is not used
	 */
	int assoc_link_id;
} drv_assoc_info_t;

/**
 * struct drv_eapol_data - Data for MHD_DATA_EAPOL_PKT and
 * used for maintaining metadata related to EAPOL frame
 * processing.
 */
typedef struct drv_eapol_data {
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
	u16 proto; /* Protocol identifier */
	const u8 *frame; /* Raw byte stream of the frame */
	int len; /* Total length of the frame */
} drv_eapol_data_t;

typedef struct drv_mlme_info {
       /* Reason code for operation */
	u16 reason;

       /* Target MAC address */
	u8 macaddr[ETH_ALEN];

       /* Flag to notify
        * 1 - internal disconnection
        * 0 - sends deauth/disassoc to AP
        */
	u8 local_state_change:1;
} drv_mlme_info_t;

typedef struct drv_deauth_info {
       /**
        * addr - AP's address (in sta mode)
        */
	const u8 *addr;

       /**
        * reason_code - Reason Code (host byte order) used in
        * Deauthentication frame
        */
	u16 reason_code;

       /**
        * ie - Optional IE(s) in Deauthentication frame
        */
	const u8 *ie;

       /**
        * ie_len - Length of ie buffer in octets
        */
	size_t ie_len;

       /**
        * locally_generated - Whether the frame was locally generated
        */
	int locally_generated;
} drv_deauth_info_t;

typedef struct drv_disassoc_info {
       /**
        * addr - AP's address (in sta mode)/STA's address (in AP mode)
        */
       const u8 *addr;

       /**
        * reason_code - Reason Code (host byte order) used in
        * Disassociation frame
        */
       u16 reason_code;

       /**
        * ie - Optional IE(s) in Disassociation frame
        */
       const u8 *ie;

       /**
        * ie_len - Length of ie buffer in octets
        */
        size_t ie_len;

       /**
        * locally_generated - Whether the frame was locally generated
        */
       int locally_generated;
} drv_disassoc_info_t;

/* For EVENT_EXTERNAL_AUTH */
typedef struct drv_external_auth {
	enum {
	DRV_EXT_AUTH_START,
	DRV_EXT_AUTH_ABORT,
	} action;
	u8 *bssid;
	u8 *ssid;
	size_t ssid_len;
	unsigned int key_mgmt_suite;
	u16 status;
	const u8 *pmkid;
	const u8 *mld_addr;
} drv_external_auth_t;

/**
* struct rx_mgmt - Data for EVENT_RX_MGMT events
*/
typedef struct drv_rx_mgmt {
	u8 *frame;
	size_t frame_len;
	u32 datarate;

	/**
	 * drv_priv - Pointer to store driver private BSS information
	 *
	 * If not set to NULL, this is used for comparison with
	 * hostapd_data->drv_priv to determine which BSS should process
	 * the frame.
	 */
	void *drv_priv;

	/**
	 * freq - Frequency (in MHz) on which the frame was received
	 */
	int freq;

	/**
	 * ssi_signal - Signal strength in dBm (or 0 if not available)
	 */
	int ssi_signal;
} drv_rx_mgmt_t;

/* Supplicant Queue ID */
extern osMessageQueueId_t supp_queue;

typedef enum drv_syna_message_type {
    DRV_SYNA_RESP_WIPHY,        // Driver capabilities
    DRV_SYNA_EVENT_SCAN_RESULT, // Scan; drv_scan_result_t
    DRV_SYNA_EVENT_JOIN_START,  // join start; SAE exteranl auth
    DRV_SYNA_EVENT_AUTH,        // SAE exteranl auth
    DRV_SYNA_EVENT_ASSOC,       // Assoc;
    DRV_SYNA_EVENT_DISASSOC,    // Disassociation (WLC_E_DISASSOC_IND);
    DRV_SYNA_EVENT_DEAUTH,      // Deauthentication (WLC_E_DEAUTH_IND);
	DRV_SYNA_EVENT_ASSOC_RESP,
	DRV_SYNA_EVENT_ASSOC_REQ,
    DRV_SYNA_EVENT_ASSOC_COMPLETE, // Association process fully completed (post 4-way)
    DRV_SYNA_EVENT_CH_SWITCH,   // Channel switch announcement complete
} drv_syna_message_type_t;

typedef struct drv_ch_switch_info {
    int freq;           /* New primary channel frequency in MHz */
    int ht_enabled;     /* Whether HT is enabled on the new channel */
    int ch_offset;      /* Secondary channel offset (1=above, -1=below, 0=none) */
    enum chan_width ch_width;
    int cf1;            /* Centre frequency 1 in MHz */
    int cf2;            /* Centre frequency 2 in MHz (80+80 only, else 0) */
    int link_id;        /* MLO link ID */
    u16 punct_bitmap;   /*  Puncturing bitmap */
} drv_ch_switch_info_t;

typedef struct drv_syna_msg{
	drv_syna_message_type_t type;
	void *data;
	uint32_t len;
} drv_syna_msg_t;

typedef struct drv_syna_channel_info_type {
	uint8_t channel;
	uint8_t channel_2;
	uint8_t band;
	uint8_t bw;
} drv_syna_channel_info_t;

extern osMessageQueueId_t supp_queue;
void mhd_send_supp_msg(void *msg);

#endif /* DRIVER_SYNA_MSG_H_ */