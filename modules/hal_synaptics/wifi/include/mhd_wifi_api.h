/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MHD_WIFI_API_H
#define MHD_WIFI_API_H
#ifndef MHD_PREBUILT
#define MHD_PREBUILT
#endif /* MHD_PREBUILT */

#include <stddef.h>
#ifdef MHD_PREBUILT
#include "mhd_pvt_struct.h" /* Private MHD header for internal use only; not for customer-facing code */
#else
#include "mhd_events.h"
#include "mhd_constants.h"
#include "mhd_structures.h"
#include "mhd_typedefs.h"
#include "mhd_wlioctl.h"
#include "mhd_tdls.h"
typedef struct wl_event_rx_frame_data wl_event_rx_frame_data_t;
#endif /* MHD_PREBUILT */

/* WAR: for con-mgr to work without dependancy changes */
/* TODO: cleanup after hardcoded interface index removal */
typedef mhd_ifidx_t mhd_interface_t;
#define MHD_STA_INTERFACE 0
#define MHD_AP_INTERFACE 1
#define MHD_P2P_INTERFACE 2

/* Maximum number of frequency bands supported (2.4 GHz, 5 GHz, 6 GHz) */
#define MHD_WIFI_MAX_BANDS 3

/*
 * @brief Scan result callback prototype.
 *
 * Invoked asynchronously during / after a scan. The lifetime / meaning of
 * the data pointer depends on internal implementation (may be NULL for simple
 * notifications). User should copy out any information needed inside the
 * callback as the underlying buffer may be recycled afterwards.
 *
 * @param context   User-supplied pointer from scan params.
 * @param status    Driver / scan status code (0 typically success, non-zero implementation defined).
 */

/* Action Frame (Mgmt) Helper Layer */
/* Lightweight filtered subscription facility layered atop generic event API.
 * Filters are applied only to WLC_E_ACTION_FRAME_RX (75) events. The payload
 * format is: wl_event_rx_frame_data_t header followed immediately by the 802.11
 * management frame body (starting at Category for Action frames).
 */

/* Category helper constants */
#define MHD_ACTION_CATEGORY_VENDOR  0x7F
#define MHD_ACTION_CATEGORY_ANY     0xFF

/* Definition of STA Flags */
#define MHD_STA_AUTHORIZED BIT(0)
#define MHD_STA_WMM BIT(1)
#define MHD_STA_SHORT_PREAMBLE BIT(2)
#define MHD_STA_MFP BIT(3)
#define MHD_STA_TDLS_PEER BIT(4)
#define MHD_STA_AUTHENTICATED BIT(5)
#define MHD_STA_ASSOCIATED BIT(6)
#define MHD_STA_SPP_AMSDU BIT(7)

/*
 * @brief MHD initialization status codes
 */
typedef enum {
	MHD_INIT_SUCCESS = 0, /* < Initialization completed successfully */
	MHD_INIT_INPROGRESS = -1, /* < Initialization is still in progress */
	MHD_INIT_FAILURE = -2, /* < Initialization failed */
	MHD_INIT_UNKNOWN = -3 /* < Initialization status is unknown */,
} mhd_init_status_t;


/* Return/status codes for action frame helper functions */
typedef enum {
	MHD_ACTION_FRAME_STATUS_OK          = 0,
	MHD_ACTION_FRAME_STATUS_BAD_ARGS    = -1,
	MHD_ACTION_FRAME_STATUS_NO_SLOT     = -2,
	MHD_ACTION_FRAME_STATUS_NOT_FOUND   = -3,
	MHD_ACTION_FRAME_STATUS_OVERSIZE    = -4,
	MHD_ACTION_FRAME_STATUS_LOWER_FAIL  = -5
} mhd_action_frame_status_t;

typedef struct {
	uint8_t category;       /* 0xFF = wildcard */
	uint8_t use_oui;        /* match vendor OUI (only if category==0x7F) */
	uint8_t oui[3];
	uint8_t use_subtype;    /* match second byte (action code / public type / vendor subtype) */
	uint8_t subtype;        /* meaning depends on category */
} mhd_wifi_action_frame_filter_t;

typedef void (*mhd_wifi_action_frame_rx_cb_t)(const mhd_event_header_t *hdr,
                                              const wl_event_rx_frame_data_t *rxmeta,
                                              const uint8_t *action_body,
                                              uint32_t action_body_len,
                                              void *user_ctx);

typedef struct {
	mhd_wifi_action_frame_filter_t filter;
	mhd_wifi_action_frame_rx_cb_t cb;   /* mandatory */
	void *user_ctx;                     /* opaque */
	mhd_ifidx_t interface;          /* interface to listen on */
} mhd_wifi_action_frame_handler_params_t;

/* Register filtered Action Frame handler. Returns 0 success;  -1 bad args;  -2 no slot */
int32_t mhd_wifi_api_set_action_frame_handler(const mhd_wifi_action_frame_handler_params_t *params);  /* returns mhd_action_frame_status_t */
/* Unregister specific handler (match by cb+user_ctx+interface). Returns 0 success;  -1 not found */
int32_t mhd_wifi_api_unset_action_frame_handler(mhd_wifi_action_frame_rx_cb_t cb, void *user_ctx, mhd_ifidx_t interface);  /* returns mhd_action_frame_status_t */

/* Convenience TX wrapper: constructs wl_action_frame_t and invokes core send.
 * Returns MHD_SUCCESS success;  negative on parameter/length errors or underlying failure.
 */
mhd_result_t mhd_wifi_api_send_action_frame(mhd_ifidx_t interface,
                                            const mhd_mac_t *da,
                                            const uint8_t *data,
                                            uint16_t len,
                                            uint32_t packet_id);

/*
 * @param data      Optional pointer to scan result data or NULL.
 */
/*
 * Scan status / completion semantics:
 *
 * The scan callback can be invoked multiple times per issued scan request.
 * The 'status' argument communicates both progress and terminal outcomes.
 * Typical sequence (illustrative):
 * 1. One or more interim result notifications (implementation may pass a list/entry via 'data').
 * 2. A final completion OR abort indication.
 *
 * Common status code meanings (host_ / driver layer defined;  numeric values come from mhd_constants.h):
 * HOST_SCAN_COMPLETED     Scan fully completed (all requested channels processed) - terminal success.
 * HOST_SCAN_INCOMPLETE   Scan is in progress and more results may come
 * HOST_SCAN_ABORTED      Scan cancelled before producing meaningful results (often zero new entries).
 *
 * Usage Guidance:
 * - Treat HOST_SCAN_COMPLETED as the only guaranteed full-success terminal state.
 * - Treat HOST_SCAN_INCOMPLETE as usable partial data
 * - Treat HOST_SCAN_ABORTED as terminal;  results (if any) may be stale or empty.
 * - Do not re-enter driver APIs from inside the callback in ways that could deadlock (e.g., starting a new scan) unless documented safe.
 * - If multiple outstanding scan consumers are multiplexed, each should maintain its own state and ignore statuses for unrelated contexts.
 *
 * Memory / Data Pointer:
 * - 'data' may be NULL for pure status notifications (e.g., COMPLETE without additional payload, or ABORTED).
 * - When non-NULL, ownership remains with driver;  copy out any needed fields before returning.
 *
 * Re-issuing Scans:
 * - You may start another scan after receiving any terminal status (COMPLETE / ABORTED / ERROR).
 * - Avoid starting a new scan on PARTIAL unless you intend to abort the current one.
 */
typedef void (*mhd_wifi_scan_callback_t)(mhd_ifidx_t ifidx, wl_bss_info_t *bss_info, host_scan_status_t status, void *user_data);

/**
 * @brief Parameters for initiating a Wi-Fi scan.
 *
 * Any optional filter can be omitted to broaden scan coverage:
 * - Set @ref bssid to NULL to scan for any BSSID.
 * - Set @ref n_freq_list to 0 to scan all supported frequencies.
 * - Set @ref n_ssids to 0 to scan for all SSIDs.
 * - Set @ref extended_params to NULL to use implementation defaults.
 *
 * If @ref cb is non-NULL, the callback is invoked for partial and/or
 * final scan results based on implementation behavior.
 */
typedef struct {
	mhd_ifidx_t interface; 				/**< Interface on which to perform the scan. */
	host_scan_type_t scan_type; 				/**< Active/passive scan type. */
	host_bss_type_t bss_type; 				/**< Infrastructure/Adhoc/Any BSS type filter. */
	const mhd_mac_t *bssid; 				/**< Optional BSSID filter. */
	const host_scan_extended_params_t *extended_params; 	/**< Optional advanced scan parameters. */
	mhd_wifi_scan_callback_t cb; 				/**< Scan progress/completion callback. */
	void *cb_context; 					/**< User context passed to @ref cb. */

	uint8_t n_freq_list; 					/**< Number of valid entries in @ref freq_list. */
	uint16_t freq_list[WL_SCAN_PARAMS_FREQ_MAX]; 		/**< Optional frequency list filter in MHz. */

	uint8_t n_ssids; 					/**< Number of valid entries in @ref ssids. */
	mhd_ssid_t ssids[WL_SCAN_PARAMS_SSID_MAX]; 		/**< Optional exact-match SSID filter list. */
} mhd_wifi_scan_params_t;

/* Parameters for WiFi MLME transmission operations */
typedef struct {
	const uint8_t *frame; /* Full 802.11 management frame (starts at FC) */
	uint16_t frame_len;       /* Length in bytes */
	uint8_t channel;          /* Target channel (if OFFCHANNEL flag set);  0 = current */
	uint8_t dwell_tu;         /* Off-channel dwell time (TUs) if OFFCHANNEL;  0 default */
	uint16_t flags;           /* MLME flags bitmask (mhd_wifi_ap_mlme_flags_t);  used for both STA and SoftAP */
	mhd_ifidx_t interface;  /* Interface (STA or SoftAP) on which to transmit the frame */
} mhd_wifi_mlme_tx_params_t;

/* Backwards-compatible alias for legacy AP MLME TX params type name. */
typedef mhd_wifi_mlme_tx_params_t mhd_wifi_ap_mlme_tx_params_t;

/* Soft AP Management API */

/* Parameters for starting a Soft AP */
typedef struct {
	const mhd_ssid_t *ssid;   /* < SSID (required, length 1..32). */
	host_security_t security;         /* < Security mask (HOST_SECURITY_OPEN, WPA2/WPA3 bits etc). */
	const mhd_password_t *security_key; /* < Passphrase/key (required for secured modes). */
	uint8_t mfp;                      /* < 0=disabled, 1=capable, 2=required. */
	uint8_t channel;                  /* < Primary channel;  0 = firmware default / STA channel if concurrency. */
	uint8_t channel_2;                /* < Secondary center channel for 80+80 MHz operation. */
	chanspec_band_t band;                     /* < 2=2.4GHz, 5=5GHz, 6=6GHz;  0 derive from channel. */
	chanspec_bw_t bw;                       /* < Channel width code: 20/40/80;  0=default (20). */
	uint8_t hidden;                   /* < 1 = suppress SSID broadcast. */
	mhd_ifidx_t interface;        /* < AP interface identifier. */
	/* -------- Extension fields ---------- */
	int beacon_int;                   /* < Beacon interval (TUs);  0 leave unchanged/default. */
	int dtim_period;                  /* < DTIM period;  0 default. */
	const uint8_t *beacon_head; /* < Optional full beacon head (before TIM). */
	uint16_t beacon_head_len;
	const uint8_t *beacon_tail; /* < Optional beacon tail (after TIM). */
	uint16_t beacon_tail_len;
	const uint8_t *proberesp; /* < Optional Probe Response template. */
	uint16_t proberesp_len;
	const uint8_t *assocresp_ies; /* < Optional (Re)Assoc Response extra IEs. */
	uint16_t assocresp_ies_len;
	const uint8_t *beacon_extra_ies; /* < Extra Beacon IEs (if not using full template). */
	uint16_t beacon_extra_ies_len;
	int isolate;                      /* < Non-zero blocks intra-BSS traffic. */
	int max_inactivity;               /* < Client inactivity timeout (s);  0 default. */
	int multicast_to_unicast;         /* < Convert selected multicast to unicast (future). */
	int basic_rates_100kbps[8];       /* < -1 terminated list;  {-1} unused. */
	int beacon_rate_100kbps;          /* < Beacon TX legacy rate;  0 default. */
	uint8_t country[3];               /* < Optional country code;  zeroed if unused. */
	uint8_t enable_owe;               /* < 1 if OWE transition BSS. */
	uint8_t second_channel;           /* < Secondary channel offset (0 none /1 above /3 below). */
	uint8_t mbssid_index;            /* < Multi-BSSID index (0 primary). */
	const uint8_t *mbssid_elem; /* < Raw MBSSID element blob. */
	uint16_t mbssid_elem_len;
	/* FILS Discovery */
	uint32_t fd_min_int;              /* < FILS Discovery min interval (TUs), 0 default. */
	uint32_t fd_max_int;              /* < FILS Discovery max interval, 0 disable FD. */
	const uint8_t *fils_disc_tmpl; /* < FILS Discovery frame template. */
	uint16_t fils_disc_tmpl_len;
	uint8_t sae_pwe;                  /* < SAE mechanism for PWE derivation.(0 = hunting-and-pecking / 1 = hash-to-element / 2 = both) */
} mhd_wifi_ap_params_t;

/* Station Info */
typedef struct {
	mhd_mac_t mac;         /* Station MAC */
	int8_t rssi_dbm;       /* Last known RSSI (dBm);  INT8_MIN if unknown */
	uint32_t tx_bytes;     /* Cumulative TX bytes */
	uint32_t rx_bytes;     /* Cumulative RX bytes */
	uint32_t inactivity_s; /* Seconds since last observed activity */
	uint16_t aid;          /* Association ID if applicable (0 if unknown) */
	uint8_t is_ht;         /* 1 if HT capabilities negotiated */
	uint8_t is_vht;        /* 1 if VHT capabilities negotiated */
	uint8_t is_he;         /* 1 if HE (11ax) capabilities negotiated */
	uint8_t is_6g;         /* 1 if station operating on 6GHz */
} mhd_wifi_ap_sta_info_t;

typedef struct {
	mhd_mac_t *list;       /* Caller-provided array of MACs */
	size_t max;            /* Capacity of list[] */
	size_t count;          /* Populated count (output) */
	mhd_ifidx_t interface;  /* AP interface */
} mhd_wifi_ap_sta_list_t;

/* Generic MLME TX Parameters */
typedef enum {
	MHD_WIFI_AP_MLME_FLAG_NONE        = 0x0000,
	MHD_WIFI_AP_MLME_FLAG_NOACK       = 0x0001, /* Do not request firmware ACK */
	MHD_WIFI_AP_MLME_FLAG_OFFCHANNEL  = 0x0002, /* Transmit using temporary off-channel context */
	MHD_WIFI_AP_MLME_FLAG_RAW_TIMESTAMP=0x0004, /* Caller supplied timestamp field (beacon/probe) */
} mhd_wifi_ap_mlme_flags_t;

/* Beacon Update Parameters */
typedef struct {
	const uint8_t *extra_ies; /* Optional appended IEs */
	uint16_t extra_ies_len;        /* Length of extra_ies */
	const uint8_t *beacon_head; /* Optional full beacon head override */
	uint16_t beacon_head_len;      /* Length */
	const uint8_t *beacon_tail; /* Optional tail override */
	uint16_t beacon_tail_len;      /* Length */
	int beacon_interval_tu;        /* >0 new beacon interval;  0 unchanged */
	int dtim_period;               /* >0 new DTIM period;  0 unchanged */
	uint8_t hidden;                /* 0/1 toggle broadcast;  0xFF = unchanged */
	uint8_t isolate;               /* 0/1 isolate clients;  0xFF = unchanged */
	uint8_t mbssid_index;          /* For MBSSID update;  0xFF = unchanged */
	mhd_ifidx_t interface;     /* AP interface */
} mhd_wifi_ap_beacon_update_t;

/* Channel Switch Parameters */
typedef struct {
	uint8_t new_channel;     /* Target primary channel */
	uint8_t new_bandwidth;   /* 20/40/80 (0 = auto) */
	uint8_t countdown_beacons;  /* Number of beacons until switch (CSA count) */
	uint8_t block_tx;        /* 1 to block data TX during switch */
	mhd_ifidx_t interface;  /* AP interface */
} mhd_wifi_ap_channel_switch_t;

/* Power Save Parameters */
typedef struct {
	uint32_t max_inactivity_s;  /* 0 = unchanged */
	uint8_t multicast_to_unicast;  /* 0/1 toggle;  0xFF unchanged */
	mhd_ifidx_t interface;  /* AP interface */
} mhd_wifi_ap_ps_params_t;

/* Vendor IE Parameters */
typedef struct {
	const uint8_t *ie; /* Full IE (starts at Element ID) */
	uint16_t ie_len;         /* Length of IE */
	uint8_t frame_types;     /* Bitmask of frames to include (e.g., BEACON=1, PROBERESP=2) */
	mhd_ifidx_t interface;  /* AP interface */
} mhd_wifi_ap_vendor_ie_t;

/* Rate Configuration */
typedef struct {
	int basic_rates_100kbps[8];  /* -1 terminated list */
	int beacon_rate_100kbps;     /* 0 = unchanged */
	mhd_ifidx_t interface;   /* AP interface */
} mhd_wifi_ap_rates_t;

/* OWE Transition Parameters */
typedef struct {
	mhd_mac_t open_bssid;    /* BSSID of open network */
	mhd_mac_t secure_bssid;  /* BSSID of secure transition network */
	mhd_ifidx_t interface;  /* AP interface */
} mhd_wifi_ap_owe_transition_t;

/* AP Statistics */
typedef struct {
	uint32_t uptime_s;           /* Seconds since AP start */
	uint32_t num_stations;       /* Current associated stations */
	uint64_t bytes_tx;           /* Aggregate TX */
	uint64_t bytes_rx;           /* Aggregate RX */
	uint32_t beacon_tx;          /* Beacon frames sent */
	uint32_t last_csa_time_s;    /* Seconds since last CSA (0 if none) */
	uint32_t num_deauths;        /* Count of deauth operations initiated */
	uint32_t num_disassocs;      /* Count of disassoc operations initiated */
	mhd_ifidx_t interface;   /* AP interface */
} mhd_wifi_ap_stats_t;

/* AP Management Function Prototypes */
mhd_result_t mhd_wifi_api_start_ap(const mhd_wifi_ap_params_t *params);
mhd_result_t mhd_wifi_api_stop_ap(mhd_ifidx_t interface, mhd_bool_t force);
int32_t mhd_wifi_api_ap_send_mlme(const mhd_wifi_mlme_tx_params_t *params);
int32_t mhd_wifi_api_ap_get_sta_list(mhd_wifi_ap_sta_list_t *out);
int32_t mhd_wifi_api_ap_get_sta_info(mhd_ifidx_t interface, const mhd_mac_t *mac, mhd_wifi_ap_sta_info_t *out);
int32_t mhd_wifi_api_ap_deauth_sta(mhd_ifidx_t interface, const mhd_mac_t *mac, uint16_t reason_code);
int32_t mhd_wifi_api_ap_disassoc_sta(mhd_ifidx_t interface, const mhd_mac_t *mac, uint16_t reason_code);
int32_t mhd_wifi_api_ap_update_beacon(const mhd_wifi_ap_beacon_update_t *params);
int32_t mhd_wifi_api_ap_channel_switch(const mhd_wifi_ap_channel_switch_t *params);
int32_t mhd_wifi_api_ap_set_rates(const mhd_wifi_ap_rates_t *rates);
int32_t mhd_wifi_api_ap_get_rates(mhd_ifidx_t interface, mhd_wifi_ap_rates_t *out);
int32_t mhd_wifi_api_ap_set_mbssid(mhd_ifidx_t interface, uint8_t index, const uint8_t *elem, uint16_t elem_len);
int32_t mhd_wifi_api_ap_clear_mbssid(mhd_ifidx_t interface, uint8_t index);
int32_t mhd_wifi_api_ap_set_owe_transition(const mhd_wifi_ap_owe_transition_t *params);
int32_t mhd_wifi_api_ap_clear_owe_transition(mhd_ifidx_t interface);
int32_t mhd_wifi_api_ap_get_stats(mhd_wifi_ap_stats_t *out);
int32_t mhd_wifi_api_ap_set_power_params(const mhd_wifi_ap_ps_params_t *params);
int32_t mhd_wifi_api_ap_get_power_params(mhd_ifidx_t interface, mhd_wifi_ap_ps_params_t *out);
int32_t mhd_wifi_api_ap_add_vendor_ie(const mhd_wifi_ap_vendor_ie_t *params);
int32_t mhd_wifi_api_ap_remove_vendor_ie(const mhd_wifi_ap_vendor_ie_t *params);
int32_t mhd_wifi_api_ap_deauth_all(mhd_ifidx_t interface, uint16_t reason_code);
int32_t mhd_wifi_api_ap_force_beacon(mhd_ifidx_t interface);

/*
 * @brief Parameters for joining a Wi-Fi network.
 *
 * Provide either SSID (and optionally BSSID/channel for a specific AP) plus
 * security credentials. For open networks security_key/key_length can be 0.
 */
typedef struct {
	const mhd_mac_t* bssid;   /* < Optional exact BSSID to target;  NULL = any matching AP. */
	const mhd_ssid_t* ssid;   /* < Target SSID (required unless joining by BSSID only). */
	const mhd_password_t *pwd;   /* < Passphrase and length */
	host_security_t security;         /* < Security type (Open/WPA2/etc). */
	uint8_t mfp;                      /* < Management Frame Protection (0=disabled, else enabled modes). */
	host_bss_type_t bss_type;         /* < Infrastructure / Adhoc. */
	uint8_t primary_channel;                  /* < If non-zero, restrict join to this channel (optimization / hidden SSID). */
	chanspec_band_t band;                     /* < Channel frequency band corresponding to primary_channel;  required only when primary_channel is non-zero. Valid values are those defined by chanspec_band_t. */
	chanspec_bw_t bw;                         /* < Channel bandwidth to use with primary_channel;  required only when primary_channel is non-zero. Valid values are those defined by chanspec_bw_t. */
	mhd_ifidx_t interface;        /* < Interface to perform the join on. */
	uint8_t sae_pwe;                  /* < SAE mechanism for PWE derivation.(0 = hunting-and-pecking / 1 = hash-to-element / 2 = both) */
	const mhd_wpa_ie_t *wpaie; /* < WPA information element for (Re)Association Request */
	uint8_t disable_eht;              /* < Suppress EHT for this connection */
} mhd_wifi_join_params_t;

typedef struct {
	const uint32_t *event_list; /* < Pointer to array of event IDs. */
	size_t event_count;               /* < Number of elements in event_list. */
	mhd_event_handler_t event_cb;     /* < Event callback (REV2). */
	void *user_ctx;                   /* < Opaque pointer returned in callback. */
	mhd_ifidx_t interface;        /* < Interface whose events are desired. */
} mhd_wifi_event_handler_params_t;

/* Firmware capability (IOVAR 'cap') */
/* Minimal feature bit groupings derived from firmware token list. These are intentionally
 * compact;  we expose both raw boolean token hits and grouped bitfields for higher level
 * mapping to supplicant wpa_driver_capa if desired. Future expansion can add more flags.
 */
typedef enum {
	MHD_FW_CAP_FLAG_MBSS  = 0x00000001,
	MHD_FW_CAP_FLAG_P2P   = 0x00000002,
	MHD_FW_CAP_FLAG_AMPDU = 0x00000004,
	MHD_FW_CAP_FLAG_TDLS  = 0x00000008,
	MHD_FW_CAP_FLAG_TWT   = 0x00000010,
	MHD_FW_CAP_FLAG_NAN   = 0x00000020,
	MHD_FW_CAP_FLAG_DUALBAND = 0x00000040,
	MHD_FW_CAP_FLAG_VHT   = 0x00000080,
	MHD_FW_CAP_FLAG_6G    = 0x00000100,
	MHD_FW_CAP_FLAG_OWE_AP = 0x00000200,
	MHD_FW_CAP_FLAG_MBSS_MULTI = 0x00000400,
	MHD_FW_CAP_FLAG_MLO   = 0x00000800,
	MHD_FW_CAP_FLAG_EHT   = 0x00001000,
	MHD_FW_CAP_FLAG_BEACON_PROT   = 0x00002000
} mhd_fw_cap_flags_t;

typedef enum {
	MHD_FW_CAP_AKM_SAE = 0x00000001,
	MHD_FW_CAP_AKM_FT  = 0x00000002
} mhd_fw_cap_akm_t;

typedef enum {
	MHD_FW_CAP_SEC_MFP = 0x00000001
} mhd_fw_cap_sec_t;

/*
 * Enumeration of AKM (authentication and key management) suites.
 */
typedef enum
{
	MHD_AKM_RESERVED                    = 0,
	MHD_AKM_8021X                       = 1, /* < WPA2 enterprise */
	MHD_AKM_PSK                         = 2, /* < WPA2 PSK */
	MHD_AKM_FT_8021X                    = 3, /* < 802.11r Fast Roaming enterprise */
	MHD_AKM_FT_PSK                      = 4, /* < 802.11r Fast Roaming PSK */
	MHD_AKM_8021X_SHA256                = 5,
	MHD_AKM_PSK_SHA256                  = 6,
	MHD_AKM_TDLS                        = 7, /* < Tunneled Direct Link Setup */
	MHD_AKM_SAE_SHA256                  = 8,
	MHD_AKM_FT_SAE_SHA256               = 9,
	MHD_AKM_AP_PEER_KEY_SHA256          = 10,
	MHD_AKM_SUITEB_8021X_HMAC_SHA256    = 11,
	MHD_AKM_SUITEB_8021X_HMAC_SHA384    = 12,
	MHD_AKM_SUITEB_FT_8021X_HMAC_SHA384 = 13,
	MHD_AKM_OWE                         = 18,
	MHD_AKM_SAE_EXT_KEY                 = 24, /* < SAE (variable length PMK) 4-way handshake */
	/* TODO: AKM-25 to be added */
} mhd_akm_suite_t;

typedef enum {MHD_FEATURE_DISABLE = 0, MHD_FEATURE_ENABLE = 1} mhd_feature_config_t;
typedef enum { MHD_FEATURE_IDSUP = 0 } mhd_feature_t;

/* Capability representation strategy:
 * - Raw bitfields retained ONLY for tokens that either (a) carry additional nuance beyond grouped flags
 * (e.g., distinguishing ampdu_tx vs ampdu_rx for potential future diagnostics) or (b) are not yet
 * part of an aggregation group (stbc_tx, stbc_rx_1ss, wowl, wmf, ht40_2g, rsdb, mp2p, mbo, estm, btcdyn).
 * - All previously 1:1 duplicated token bits (mbss, mbss_multi, p2p, tdls, twt, nan, dualband, vht160,
 * vht80p80, vht_prop_rates, band6g, owe_ap) have been removed as raw fields;  their presence is indicated
 * solely through the aggregated 'flags' mask (mhd_fw_cap_flags_t).
 * - SAE / FT authentication suite presence is represented only in 'akm' (mhd_fw_cap_akm_t);  raw sae/extsae/ft
 * bits are not stored.
 *
 * Mapping summary (token -> effect):
 * mbssX         => flags |= MBSS (+ MBSS_MULTI if X>1);  mbss_max holds X
 * p2p           => flags |= P2P
 * tdls          => flags |= TDLS
 * twt           => flags |= TWT
 * nan           => flags |= NAN
 * dualband      => flags |= DUALBAND
 * 160|80+80|vht-prop-rates => flags |= VHT
 * 6g            => flags |= 6G
 * owe_ap        => flags |= OWE_AP
 * sae|extsae    => akm   |= SAE
 * ft            => akm   |= FT
 * mfp           => security |= MFP
 * ampdu|ampdu_tx|ampdu_rx => ampdu/ampdu_tx/ampdu_rx raw bits + flags |= AMPDU
 *
 * Guidance:
 * - For feature gating use flags/akm/security masks (preferred stable API surface).
 * - Use remaining raw bits only when token-level granularity is truly required.
 */
typedef struct {
	/* Raw token presence flags (packed bitfields: 1 = present, 0 = absent).
	 * - Do NOT take the address of individual members (was undefined before;  with bitfields it's ill-formed).
	 * - Ensure no external binary interface (IOCTL / firmware) depends on the legacy layout.
	 */
	uint32_t ap:1;
	uint32_t sta:1;
	/* Removed direct raw duplicates of grouped flags (mbss, p2p, tdls, twt, nan, dualband, vht160, vht80p80, vht_prop_rates,
	 * band6g, owe_ap, mbss_multi). Presence is now expressed only via aggregated 'flags' mask:
	 * MHD_FW_CAP_FLAG_MBSS         <- was mbss token
	 * MHD_FW_CAP_FLAG_MBSS_MULTI   <- was mbss_multi derived from mbssX>1
	 * MHD_FW_CAP_FLAG_P2P          <- was p2p token
	 * MHD_FW_CAP_FLAG_TDLS         <- was tdls token
	 * MHD_FW_CAP_FLAG_TWT          <- was twt token
	 * MHD_FW_CAP_FLAG_NAN          <- was nan token
	 * MHD_FW_CAP_FLAG_DUALBAND     <- was dualband token
	 * MHD_FW_CAP_FLAG_VHT          <- was any of vht160/vht80p80/vht_prop_rates
	 * MHD_FW_CAP_FLAG_6G           <- was band6g token
	 * MHD_FW_CAP_FLAG_OWE_AP       <- was owe_ap token
	 */
	uint32_t ampdu:1;
	uint32_t ampdu_tx:1;
	uint32_t ampdu_rx:1;
	uint32_t amsdurx:1;
	uint32_t amsdutx:1;
	/* tdls removed as raw */
	uint32_t dot11h:1;
	/* SAE/FT raw presence consolidated into akm aggregate;  individual bits removed
	 * to reduce redundancy. Use akm field (MHD_FW_CAP_AKM_SAE / _FT).
	 */
	uint32_t wnm:1;
	uint32_t mfp:1;
	uint32_t wowl:1;
	uint32_t wmf:1;
	/* dualband/vht* removed as raw (see flags) */
	uint32_t stbc_tx:1;
	uint32_t stbc_rx_1ss:1;
	/* nan/twt/owe_ap/band6g removed as raw */
	uint32_t ht40_2g:1;
	uint32_t rsdb:1;
	uint32_t mp2p:1;
	uint32_t mbo:1;
	uint32_t estm:1;
	uint32_t btcdyn:1;
	/* mbss_multi removed as raw (derived into flags) */
	/* Optionally reserve remaining bits in current storage unit for future flags.
	 * (Compiler will allocate sufficient uint32_t units to cover 35 bits.)
	 */
	/* Numeric values */
	uint16_t mbss_max;  /* parsed from mbssX token if present */
	/*
	 * Aggregated grouped masks (populated by capability query):
	 *
	 * - flags:    Bitmask of grouped feature flags (see mhd_fw_cap_flags_t). Set if any corresponding raw token is present.
	 * Example: If any of ampdu/ampdu_tx/ampdu_rx is present, MHD_FW_CAP_FLAG_AMPDU is set.
	 * - akm:      Bitmask of supported authentication key management suites (see mhd_fw_cap_akm_t).
	 * Populated based on presence of SAE/FT tokens. Raw sae/ext_sae/ft bits are not present;  use akm mask.
	 * Example: If SAE or extsae token is present, MHD_FW_CAP_AKM_SAE is set.
	 * - security: Bitmask of supported security features (see mhd_fw_cap_sec_t). Example: MHD_FW_CAP_SEC_MFP set if mfp token present.
	 *
	 * These fields are derived during capability parsing and provide a stable, high-level summary for upper layers.
	 * They should be used in preference to raw bitfields for feature checks in most application and driver logic.
	 */
	uint32_t flags;     /* mhd_fw_cap_flags_t (MBSS/P2P/...) */
	uint32_t akm;       /* mhd_fw_cap_akm_t (SAE/FT) */
	uint32_t security;  /* mhd_fw_cap_sec_t (MFP, etc.) */
} mhd_wifi_fw_cap_t;

/*
 * @brief Retrieve band capabilities for all supported frequency bands.
 *
 * This function populates the caller-provided struct pointer with detailed capabilities
 * for each frequency band supported by the wireless hardware, including channel
 * lists, bitrates, and HT/VHT/HE/EHT capabilities.
 *
 *
 * @param[in, out] bands       Pointer to caller-allocated array of band structures.
 * On success, populated with band data (channels/rates allocated).
 * @param[out] num_bands      Pointer to band count variable.
 * Output: actual number of bands populated (valid range: 0 to MHD_WIFI_MAX_BANDS).
 *
 * @return MHD_SUCCESS on success (bands data filled, *num_bands updated to actual count);
 *
 */
mhd_result_t mhd_wifi_api_get_band_capabilities(mhd_wifi_supported_band_t *bands, uint16_t *num_bands);

/* Aggregated capability helper accessors */
static inline uint32_t mhd_fw_cap_get_flags(const mhd_wifi_fw_cap_t *c)    {
	return c->flags;
}
static inline uint32_t mhd_fw_cap_get_akm(const mhd_wifi_fw_cap_t *c)      {
	return c->akm;
}
static inline uint32_t mhd_fw_cap_get_security(const mhd_wifi_fw_cap_t *c) {
	return c->security;
}

/*
 * @brief Retrieve firmware advertised capabilities via IOVAR 'cap'.
 * Populates out with raw token presence booleans and grouped bitfields.
 */
mhd_result_t mhd_wifi_api_get_cap(mhd_wifi_fw_cap_t *out);

/*
 * @brief Register event handler (single-subscription model).
 *
 * Model:
 * - Only ONE handler may be registered per interface at any time. Attempting
 * to register a second handler for the same interface returns -2 (no slot).
 * - The handler receives all firmware events;  it must filter on event IDs it
 * cares about (params->event_list is advisory/documentation only in this mode).
 * - Callback receives immutable header + payload (if any) enabling a unified
 * parsing path.
 */
mhd_result_t mhd_wifi_api_set_event_handler(mhd_wifi_event_handler_params_t *params);

/*
 * @brief Unregister the current event handler for an interface.
 *
 * Since only one handler can exist, unregistration specifies only the interface.
 */
mhd_result_t mhd_wifi_api_unset_event_handler(mhd_ifidx_t interface);

/* EAPOL RECEIVE HANDLER WRAPPER API */
/*
 * The low-level MHD EAPOL layer supports registration of a single receive handler
 * (see mhd_eapol_register_receive_handler). For consistency with the generic event
 * API above, we provide a light-weight multiplexer so multiple upper layers can
 * register an EAPOL receive callback through this Wi-Fi API layer.
 */

/* EAPOL wrapper: use opaque pointer for packet to avoid exposing internal buffer types */
typedef void (*mhd_wifi_eapol_rx_cb_t)(void *eapol_packet, mhd_ifidx_t interface, void *user_ctx);

/*
 * @brief Parameters for registering an EAPOL receive handler.
 */
typedef struct {
	mhd_ifidx_t ifidx;	     /* < Interface index used to filter which interface's EAPOL packets are delivered. */
	mhd_wifi_eapol_rx_cb_t cb;   /* < Mandatory EAPOL packet receive callback. */
	void *user_ctx;              /* < Opaque user context passed back on each receive. */
} mhd_wifi_eapol_handler_params_t;

/*
 * @brief Register an EAPOL receive handler (fan-out wrapper).
 *
 * Underlying driver supports only one raw EAPOL handler;  this API multiplexes up to
 * a fixed number (implementation limit). The first registration installs the core shim.
 * @return 0 success;  -1 invalid params;  -2 core handler conflict;  -3 no slot.
 */
int32_t mhd_wifi_api_set_eapol_handler(const mhd_wifi_eapol_handler_params_t *params);

/*
 * @brief Unregister a specific EAPOL receive handler.
 * Automatically removes the core shim if last handler is removed.
 * @return 0 success;  -1 bad cb;  -2 not found.
 */
int32_t mhd_wifi_api_unset_eapol_handler(mhd_wifi_eapol_rx_cb_t cb, void *user_ctx);

/* Retrieve raw pointer and length of an EAPOL packet supplied to an EAPOL RX callback.
 * pkt: opaque pointer received in mhd_wifi_eapol_rx_cb_t
 * data: out pointer to start of Ethernet frame containing EAPOL
 * len: out length of the contiguous buffer (may include entire frame)
 * Returns 0 on success, <0 on error.
 */
/*
 * @brief Extract raw buffer pointer and length from an opaque EAPOL packet.
 *
 * Pointer returned is start of Ethernet frame (destination MAC).
 * Caller must not modify contents unless it owns / will not pass to other handlers.
 * @return 0 success;  -1 invalid args;  -2 empty buffer.
 */
int32_t mhd_wifi_api_get_eapol_data(void *pkt, uint8_t **data, uint16_t *len);

/* EAPOL TX and MAC helper APIs */
/*
 * @brief Transmit an EAPOL frame.
 *
 * Builds and prepends an Ethernet header (dest|src|0x888E) then queues the frame.
 * eapol_payload must start at IEEE 802.1X header (version/type/length). No encryption
 * is applied here;  driver/firmware handles protection if keys are installed.
 *
 * Return codes:
 * 0  Success
 * -1  Invalid arguments
 * -2  Failed to get source MAC
 * -3  Buffer allocation failure
 * -4  Buffer front adjust failure
 * -5  Data pointer acquisition failure
 * -6  Size set failure
 */
int32_t mhd_wifi_api_send_eapol(mhd_ifidx_t interface,
                                const mhd_mac_t *dest_mac,
                                const mhd_mac_t *src_mac,
                                const uint8_t *eapol_payload,
                                uint16_t eapol_len);

/* Get MAC address for given interface (wrapper). */
/*
 * @brief Get the current MAC address for an interface.
 */
mhd_result_t mhd_wifi_api_get_mac_address(mhd_ifidx_t interface, mhd_mac_t *out_mac);

/*
 * @brief Get current associated BSSID for an interface (STA mode).
 * Copies 6-byte BSSID into out_bssid.
 */
mhd_result_t mhd_wifi_api_get_bssid(mhd_ifidx_t interface, mhd_mac_t *out_bssid);
mhd_result_t mhd_wifi_api_get_ssid(mhd_ifidx_t interface, mhd_ssid_t *ssid);

/*
 * @brief Snapshot of current firmware BSS info for associated STA link.
 *
 * Caller provides buffer storage in @ref ies with capacity @ref ies_buf_len.
 * API fills @ref ies_len with copied IE bytes and sets @ref has_rsn_ie based
 * on whether RSN IE (EID 48) exists in the payload.
 */
typedef struct {
	mhd_mac_t bssid;
	uint16_t beacon_period;
	uint16_t capability;
	uint16_t ie_offset;
	uint32_t ie_length;
	uint32_t bss_info_version;
	uint8_t has_rsn_ie;
	uint8_t *ies;
	uint32_t ies_buf_len;
	uint32_t ies_len;
} mhd_wifi_bss_info_update_t;

/*
 * @brief Refresh current BSS info using WLC_GET_BSS_INFO and parse RSN IE.
 *
 * This follows the Linux-style update path (query firmware BSS record, parse
 * IE payload, detect RSN presence) using MCU SDK ioctl flow.
 *
 * @param interface STA interface to query.
 * @param out Output structure. @ref out->ies is optional; if provided, IE bytes
 *            are copied up to @ref out->ies_buf_len.
 * @return @ref MHD_SUCCESS on success, otherwise error code.
 */
mhd_result_t mhd_wifi_api_update_bss_info(mhd_ifidx_t interface,
	mhd_wifi_bss_info_update_t *out);

/* Key management wrapper */
/* Key installation parameters for PTK/GTK after 4-way handshake. Mirrors
 * wpa_driver_set_key_params subset, mapping into wl_wsec_key_t.
 */
typedef struct {
	int wpa_alg;                  /* enum wpa_alg value from supplicant */
	const uint8_t *key;   /* Key material (PTK temporal key, GTK, IGTK, etc.) */
	uint16_t key_len;             /* Length of key material */
	uint8_t key_index;            /* Key index (GTK/IGTK) */
	uint8_t set_tx;               /* Whether this key used for TX (pairwise/PTK) */
	uint8_t is_pairwise;          /* 1 = pairwise (PTK), 0 = group */
	const uint8_t *seq;   /* Optional sequence / RSC */
	uint8_t seq_len;              /* Length of seq */
	const mhd_mac_t *peer_mac; /* Peer MAC for pairwise key;  NULL for group */
	mhd_ifidx_t interface;    /* Target interface */
	int8_t link_id;               /* MLO link ID: -1 = MLD-wide (PTK), 0..N = per-link GTK/IGTK */
} mhd_wifi_key_params_t;

/* Unified key management API: install or delete a key.
 * To delete: set params->key_len == 0 (key pointer may be NULL) and provide key_index,
 * is_pairwise + peer_mac (for pairwise) or leave peer_mac NULL for group.
 * To install: provide key material (key/key_len > 0) plus algorithm, etc.
 * Returns: 0 success;  -1 bad args;  -2 unsupported alg (install);  -3 programming failure.
 */
int32_t mhd_wifi_api_set_or_del_key(const mhd_wifi_key_params_t *params);
/*
 * @brief Start a scan based on provided parameters.
 * @return 0 on successful dispatch (actual results async), negative on immediate failure.
 */
mhd_result_t mhd_wifi_api_scan(mhd_wifi_scan_params_t *params);

/*
 * @brief Attempt to join a WiFi network using provided credentials.
 *
 * Asynchronous contract:
 * - A return value of 0 (or non-negative driver status) ONLY means the join
 * request was successfully dispatched to firmware / lower layer.
 * - It does NOT guarantee that the station is associated or that keys are
 * installed;  higher layers must wait for a firmware event.
 * - Control-plane association success is definitively indicated by receipt of
 * event WLC_E_SET_SSID with hdr->status == WLC_E_STATUS_SUCCESS on the target
 * interface.
 * - A WLC_E_SET_SSID with hdr->status == WLC_E_STATUS_NO_NETWORKS or other
 * non-success status represents a terminal attempt outcome (no further
 * SET_SSID events are expected for that attempt).
 * - Disassociation after a prior success is signaled by WLC_E_DISASSOC_IND.
 *
 * Caller guidance:
 * - Do not treat the function return code alone as a connection indicator.
 * - Register an event handler (see mhd_wifi_api_set_event_handler) for
 * WLC_E_SET_SSID (and optionally WLC_E_DISASSOC_IND) to observe completion.
 * - For open networks, security_key/key_length may be 0 (implementation must
 * allow NULL key when security == HOST_SECURITY_OPEN).
 *
 * @return 0 (or driver status) success dispatching;  negative on parameter or immediate failure.
 */
mhd_result_t mhd_wifi_api_join(mhd_wifi_join_params_t *params);

/*
 * @brief Disconnect the Wi-Fi station from the currently associated AP.
 *
 * This API initiates a disassociation request from the Access Point (AP)
 * for the given interface. Internally, it issues a WLC_DISASSOC IOCTL
 * to the firmware and resets the join state to JOIN_INITIALIZED.
 *
 * As per IEEE 802.11 standard, disassociation terminates an existing
 * association relationship between a STA and an AP. After successful
 * execution of this API, the station transitions out of the associated
 * state and must perform a new join/association procedure to reconnect.
 *
 * @note
 * - Currently supported only for MHD_STA_INTERFACE.
 * - P2P disconnection handling is not implemented.
 * - This API does not block until link teardown is fully completed
 * at firmware level;  it only triggers the disassociation request.
 *
 * @param[in] interface  Wi-Fi interface identifier.
 * Must be MHD_STA_INTERFACE.
 *
 * @return
 * - MHD_SUCCESS  : Disassociation request successfully issued.
 * - MHD_BADARG   : Invalid interface provided.
 * - Other error codes propagated from IOCTL layer.
 *
 * @retval MHD_SUCCESS   Disassociation triggered successfully.
 * @retval MHD_BADARG    Interface is not supported.
 *
 * @ingroup MHD_WIFI_API
 */

mhd_result_t mhd_wifi_api_disconnect(mhd_ifidx_t interface);

mhd_result_t mhd_wifi_api_config_feature(mhd_feature_t feature, mhd_feature_config_t config);

/*
 * @brief Initialize WiFi Driver and start WiFi Hardware/Radio
 *
 * This function performs complete WiFi initialization
 * Initializes and brings up the WiFi driver and firmware
 *
 * @return MHD_SUCCESS on success, MHD_ERROR on failure
 */
mhd_result_t mhd_wifi_api_wifi_on();

/*
 * @brief Wait and get the status of MHD Initialization
 *
 * This API will block the caller until MHD init flow is completed.
 * After MHD Initialization, this will update the status argument with the final init status.
 * If MHD is stuck during its init flow, it must properly inform the caller about the failure,
 * without blocking indefinitely.
 *
 * @param status Pointer to store the final initialization status
 * Fills MHD_INIT_STATUS_SUCCESS - If MHD and WLAN Firmware is initialized successfully
 * Fills MHD_INIT_STATUS_TIMEOUT - If MHD initialization timed out
 * Fills MHD_INIT_STATUS_ERROR - If MHD/Firmware initialization failed
 * @param timeout Timeout to wait for WLAN Driver and Firmware to get initialized
 * Timeout in milliseconds. Use NEVER_TIMEOUT for indefinite wait.
 * @return 0 on success;  -1 on error
 */
mhd_result_t mhd_wifi_api_wait_for_ready(mhd_init_status_t *status, uint32_t timeout);

/*
 * @brief Deinitialize WiFi hardware
 *
 * This function shuts down the WiFi driver stack, deinitializes
 * the bus interface, such that whenever mhd_wifi_api_wifi_on is called,
 * it must be ready to initialize the driver again and bringup firmware.
 *
 * The function performs the following:
 * - Bring down the WiFi Radio
 * - Deinitialize and cleanup WiFi driver
 * - Deinitialize and cleanup bus interface
 *
 * @return MHD_SUCCESS on success, MHD_ERROR on failure
 */
mhd_result_t mhd_wifi_api_wifi_off(void);

/* @brief : Delete the interface
 *
 * This API deletes the interface with the MHD driver
 *
 * @param ifidx interface index. This is the index returned during
 *				interface creation - mhd_wifi_if_create()
 * @return
 * MHD_SUCCESS on success
 * MHD_BADARG  invalid interface index
 */
mhd_result_t mhd_wifi_if_remove(int handle);

/*
 * @brief To send authentication frame for external auth
 *
 * @param params  point to mlme tx frame parameters.
 */
mhd_result_t mhd_wifi_api_send_auth(const mhd_wifi_mlme_tx_params_t *params);

/*
 * @brief Assoc Mgr commands for fine control of assoc
 *
 * @param cmd     command to control assocation state(PAUSE/ABORT/etc)
 * @param params  parameters for control commands.
 */
mhd_result_t mhd_wifi_api_assoc_mgr_cmd(uint16 cmd, uint16 params);

/*
 * @brief Get current channel for an interface (STA mode).
 */
mhd_result_t mhd_wifi_api_get_channel(mhd_ifidx_t interface, uint32_t* channel);

/*
 * @brief struct mhd_wifi_pmksa_t - PMK Security Association
 *
 * This structure is passed to the set/del_pmksa() method for PMKSA
 * caching.
 *
 */
typedef struct {
	const uint8_t *bssid;   /* The AP's BSSID (may be %NULL) */
	const uint8_t *pmkid;   /* The identifier to refer a PMKSA. */
	const uint8_t *pmk;     /* The PMK for the PMKSA identified by @pmkid */
	size_t pmk_len;                 /* Length of the @pmk. */
	const uint8_t *ssid;    /* SSID to specify the ESS within which a PMKSA */
	size_t ssid_len;                /* Length of the @ssid in octets */
	const uint8_t *cache_id; /* cache identifier advertized by a FILS AP identifying the scope of PMKSA. */
	uint32_t pmk_lifetime;          /* Maximum lifetime for PMKSA in seconds */
	uint8_t pmk_reauth_threshold;   /* Threshold time for reauthentication */
} mhd_wifi_pmksa_t;

mhd_result_t mhd_wifi_api_update_pmksa(const mhd_wifi_pmksa_t *pmksa, uint8_t set);

/*
 * @brief CSI event types
 */
typedef enum {
	MHD_CSI_EVENT_DATA = 0, /* < CSI data available */
	MHD_CSI_EVENT_STOP = 1   /* < CSI capture stopped */
} mhd_csi_event_type_t;

/*
 * @brief CSI event data structure passed to user callback
 *
 * Contains raw CSI data pointer (no decoding in MHD layer).
 * Data format: cfr_dump_header_v3_t (80 bytes) + raw PHY data (each 4 bytes x 64 subcarriers)
 */
typedef struct {
	mhd_csi_event_type_t event_type;   /* < Event type */
	uint8_t *data_ptr;                   /* < Pointer to CSI data (zero-copy) */
	uint32_t data_len;                   /* < Data length in bytes */
	mhd_mac_t peer_mac;                  /* < Peer MAC address */
} mhd_csi_event_data_t;

/*
 * @brief CSI event callback function type
 *
 * @param event_data  Pointer to CSI event data structure
 * @param context     User context pointer (from mhd_csi_config_t)
 */
typedef void (*mhd_csi_event_cb_t)(const mhd_csi_event_data_t *event, void *context);

/*
 * @brief CSI configuration structure
 */
typedef struct {
	mhd_mac_t peer_mac;                  /* < Target peer MAC */
	uint32_t period;                     /* < Capture period (ms) */
	uint32_t duration;                   /* < Total duration (ms, 0=unlimited) */
	uint8_t trigger_mode;                /* < Trigger mode */
	mhd_csi_event_cb_t csi_callback;     /* < Callback function */
	void *callback_context;              /* < User context */
	mhd_ifidx_t interface;          /* < Interface */
} mhd_csi_config_t;

/*
 * @brief Start CSI capture for a specific peer
 *
 * Registers unified event handler for WLC_E_CSI (handles both data and stop), then sends
 * IOCTL to firmware to start periodic CSI capture.
 *
 * @param config  Pointer to CSI configuration structure
 *
 * @return MHD_SUCCESS          - CSI capture started successfully
 * @return MHD_BADARG           - Invalid parameter (NULL config or callback)
 * @return MHD_ERROR            - CSI session already active
 * @return MHD_IOCTL_FAIL       - Firmware IOCTL failed
 *
 * @note Only one CSI session active at a time (single session model)
 * @note trigger_mode parameter exists but not currently used by firmware.
 * Firmware auto-determines mode (active periodic for "csi" IOVAR).
 * Reserved for future when firmware supports passive mode.
 * @note Cleanup happens automatically on WLC_E_CSI stop event (status=1)
 */
mhd_result_t mhd_wifi_api_csi_start(mhd_csi_config_t *config);

/*
 * @brief Stop CSI capture for a specific peer
 *
 * Sends IOCTL to firmware to stop CSI capture. Firmware will respond with
 * WLC_E_CSI event (status=1), which triggers automatic cleanup of event handlers
 * and session state.
 *
 * @param config  Pointer to CSI configuration (peer_mac and interface required)
 *
 * @return MHD_SUCCESS          - Stop request sent successfully
 * @return MHD_BADARG           - Invalid parameter (NULL config)
 * @return MHD_ERROR            - No active CSI session
 * @return MHD_IOCTL_FAIL       - Firmware IOCTL failed
 *
 * @note Does not cleanup immediately - waits for WLC_E_CSI stop event (status=1)
 */
mhd_result_t mhd_wifi_api_csi_stop(mhd_csi_config_t *config);

/*
 * @brief Retrieve RSSI of a client connected to the AP interface.
 *
 * This API queries the WLAN firmware to obtain the Received Signal Strength
 * Indicator (RSSI) of a specific station associated with the local AP
 * interface. The RSSI value returned represents the signal strength of the
 * most recently received frame from the specified client.
 *
 * Internally, the function sends the WLC_GET_RSSI ioctl to the firmware with
 * the client's MAC address and retrieves the RSSI value from the response.
 *
 * @param[out] rssi
 * Pointer to a variable where the RSSI value (in dBm) of the client will
 * be stored.
 *
 * @param[in] client_mac_addr
 * Pointer to the MAC address of the associated client for which RSSI is
 * requested.
 *
 * @return
 * - MHD_SUCCESS : RSSI successfully retrieved.
 * - Error code  : Failure due to invalid arguments, buffer allocation
 * failure, or ioctl communication failure.
 *
 * @note
 * - This API is valid only when the device is operating in AP mode and the
 * specified client is currently associated with the AP.
 * - The ioctl buffer used for communication is aligned to 4 bytes as required
 * by the WLAN firmware interface.
 */
mhd_result_t mhd_wifi_get_ap_client_rssi(mhd_ifidx_t ifidx, int32_t* rssi, const mhd_mac_t* client_mac_addr);

/*
 * @brief Retrieve RSSI of the currently associated AP.
 *
 * This API queries the WLAN firmware to obtain the Received Signal Strength
 * Indicator (RSSI) of the access point to which the device is currently
 * associated in STA mode.
 *
 * Internally, the function issues the WLC_GET_RSSI ioctl to the firmware
 * through the STA interface and retrieves the RSSI value from the response
 * buffer.
 *
 * @param[out] rssi
 * Pointer to a variable where the RSSI value (in dBm) of the associated
 * AP will be stored.
 *
 * @return
 * - MHD_SUCCESS : RSSI successfully retrieved.
 * - Error code  : Failure due to invalid internal handle, buffer
 * allocation failure, or ioctl communication failure.
 *
 * @note
 * - This API is valid only when the device is operating in STA mode and is
 * associated with an access point.
 * - The returned RSSI represents the signal strength of the currently
 * connected AP based on the most recently received frames.
 */
mhd_result_t mhd_wifi_get_rssi(mhd_ifidx_t ifidx, int32_t* rssi);

/* Maximum number of MLO links supported for STA mode */
#define MHD_MAX_MLO_LINKS  3

/*
 * @brief Per-link information returned by the firmware after an MLO association
 */
typedef struct {
	mhd_mac_t bssid;        /* AP's BSSID on this link */
	mhd_mac_t addr;         /* STA's own MAC address on this link */
	uint32_t freq;          /* Operating frequency of this link in MHz */
} mhd_sta_mlo_link_info_t;

/*
 * @brief MLO association state returned by firmware after an MLO association.
 */
typedef struct {
	uint16_t valid_links;                              /* Bitmask of established link IDs (bit N set => link N active) */
	uint8_t assoc_link_id;                             /* Link ID used for Assoc */
	mhd_mac_t ap_mld_addr;                             /* AP MLD address */
	mhd_sta_mlo_link_info_t links[MHD_MAX_MLO_LINKS];  /* Per-link details indexed by link ID */
} mhd_sta_mlo_info_t;

/*
 * @brief Query the firmware for the current MLO association state.
 *
 * @param interface  Interface to query.
 * @param out        Caller-allocated struct to receive MLO info.
 *
 * @return MHD_SUCCESS on success;  MHD_BADARG if out is NULL;
 * MHD_ERROR if the firmware query fails or the device is not MLO-associated.
 * On non-MLO associations, valid_links will be 0 and out->assoc_link_id is undefined.
 */
mhd_result_t mhd_wifi_api_get_sta_mlo_info(mhd_ifidx_t interface, mhd_sta_mlo_info_t *out);

/*
 * @brief Configure Wi-Fi power save mode and PM2 sleep interval.
 *
 * This API sets the power save mode of the STA interface. For PM2 mode,
 * it also configures the firmware sleep interval.
 *
 * Power save behavior (aligned with IEEE 802.11):
 * - NO_POWERSAVE_MODE : STA remains continuously awake
 * - PM1_POWERSAVE_MODE: STA follows legacy PS (TIM/DTIM based wakeup)
 * - PM2_POWERSAVE_MODE: STA enters deep sleep with periodic wakeups
 *
 * @param[in] psmode
 * Power save mode to be configured.
 *
 * @param[in] interval_ms
 * Sleep interval in milliseconds (valid only for PM2 mode).
 * - If 0, a default value (200 ms) is used
 * - Rounded down to nearest multiple of 10 ms
 *
 * @return
 * - MHD_SUCCESS           : Operation successful
 * - MHD_BADARG            : Invalid argument (e.g., unsupported psmode or NULL parameter)
 * - MHD_DELAY_TOO_SHORT   : Interval < 10 ms
 * - MHD_DELAY_TOO_LONG    : Interval > 2000 ms
 * - Error from underlying ioctl/iovar calls
 *
 * @note
 * - Valid PM2 interval range: 10 ms to 2000 ms.
 * - Interval granularity is 10 ms due to firmware constraints.
 * - Uses IOVAR "pm2_sleep_ret" to configure sleep interval.
 * - Optionally configures PM limit via "pm_limit" if supported.
 *
 * @details
 * Internally:
 * - Updates firmware using WLC_SET_PM ioctl.
 * - Stores the configured mode and interval in the mhd_pub_t instance:
 * - psmode
 * - psinterval_ms
 *
 * @warning
 * - Aggressive PM2 intervals may impact latency and throughput.
 * - Ensure compatibility with AP DTIM settings for optimal behavior.
 *
 * @see mhd_get_psmode()
 */
mhd_result_t mhd_set_psmode(mhd_ifidx_t ifidx, mhd_psmode_t psmode, uint16_t interval_ms);

/*
 * @brief Get current Wi-Fi power save mode and PM2 interval.
 *
 * This API retrieves the current power save mode configured in firmware.
 * If the device is operating in PM2 (deep power save) mode, the sleep
 * interval is also returned.
 *
 * Power save modes typically map to IEEE 802.11 STA power management:
 * - NO_POWERSAVE_MODE : Active mode (STA stays awake)
 * - PM1_POWERSAVE_MODE: Legacy PS (listen interval based wakeup)
 * - PM2_POWERSAVE_MODE: Deep sleep with firmware-controlled interval
 *
 * @param[out] psmode
 * Pointer to store the current power save mode.
 *
 * @param[out] time_ms
 * Pointer to store the PM2 sleep interval (in milliseconds).
 * This parameter is required only when the mode is PM2.
 * Ignored for other modes.
 *
 * @return
 * - MHD_SUCCESS        : Operation successful
 * - MHD_BADARG         : Invalid argument (NULL pointer)
 * - Error from underlying ioctl/iovar calls
 *
 * @note
 * - When mode is not PM2, @p time_ms is not accessed.
 * - PM2 interval corresponds to firmware sleep return interval
 * (IOVAR: "pm2_sleep_ret").
 *
 * @see mhd_set_psmode()
 */
mhd_result_t mhd_get_psmode(mhd_ifidx_t ifidx, mhd_psmode_t *psmode, uint16_t *time_ms);

/* WiFi Keep-Alive API */

#define MKEEP_ALIVE_MAX_SLOTS   8
#define MKEEP_ALIVE_MAX_PKT_LEN 256
#define MKEEP_ALIVE_MIN_PKT_LEN 42

typedef struct {
	uint8_t  keep_alive_id;
	uint32_t period_msec;
	uint16_t pkt_len;
	uint8_t  pkt_data[MKEEP_ALIVE_MAX_PKT_LEN];
} mhd_mkeep_alive_config_t;

/*
 * @brief Set WiFi keep-alive configuration
 *
 * Configures a keep-alive slot in the firmware.
 *
 * @param[in] config  Pointer to keep-alive configuration
 *
 * @return MHD_SUCCESS on success, MHD_BADARG or error code otherwise
 */
mhd_result_t mhd_wifi_api_mkeep_alive_set(mhd_ifidx_t ifidx, const mhd_mkeep_alive_config_t *config);

/*
 * @brief Get WiFi keep-alive status from firmware
 *
 * @param[in,out] config  Pointer to config struct; keep_alive_id must be set.
 *                        On success, period_msec, pkt_len, and pkt_data are filled.
 *
 * @return MHD_SUCCESS on success, MHD_BADARG otherwise
 */
mhd_result_t mhd_wifi_api_mkeep_alive_get(mhd_ifidx_t ifidx, mhd_mkeep_alive_config_t *config);

/* TWT negotiation type */
typedef enum {
	MHD_TWT_NEGOTIATION_INDIVIDUAL = 0,
	MHD_TWT_NEGOTIATION_BROADCAST = 1,
	MHD_TWT_NEGOTIATION_WAKE_TBTT = 2,
	} mhd_twt_negotiation_type_t;

/* TWT setup command */
typedef enum {
	MHD_TWT_SETUP_CMD_REQUEST = 0,
	MHD_TWT_SETUP_CMD_SUGGEST = 1,
	MHD_TWT_SETUP_CMD_DEMAND = 2,
	MHD_TWT_SETUP_CMD_GROUPING = 3,
	MHD_TWT_SETUP_CMD_ACCEPT = 4,
	MHD_TWT_SETUP_CMD_ALTERNATE = 5,
	MHD_TWT_SETUP_CMD_DICTATE = 6,
	MHD_TWT_SETUP_CMD_REJECT = 7,
	} mhd_twt_setup_cmd_t;

/* Target wake time reference */
typedef enum {
	MHD_TWT_WAKE_TYPE_BSS = 0,
	MHD_TWT_WAKE_TYPE_OFFSET = 1,
	MHD_TWT_WAKE_TYPE_AUTO = 2,
	} mhd_twt_wake_type_t;

#define MHD_TWT_FLOW_ID_AUTO    0xFFu
#define MHD_TWT_BCAST_ID_AUTO   0xFFu
#define MHD_TWT_CONFIG_ID_ANY   0xFFu

/* TWT setup parameters */
typedef struct {
	mhd_twt_negotiation_type_t negotiation_type;
	mhd_twt_setup_cmd_t setup_cmd;
	mhd_twt_wake_type_t wake_type;
	mhd_mac_t peer_mac;
	mhd_bool_t peer_mac_valid;
	mhd_bool_t trigger;
	mhd_bool_t announce;          /* !unannounced */
	mhd_bool_t unsolicited;
	uint64_t twt_wake_time;      /* explicit target wake time; 0 = FW auto */
	uint32_t twt_wake_interval;  /* wake duration (us) */
	uint32_t twt_interval;       /* wake interval (us) */
	uint32_t twt_interval_max;   /* max wake interval (us) */
	uint8_t  duty_cycle_min;     /* min duty cycle (percentage) */
	uint32_t btwt_persistence;
	uint8_t  frame_recomm;
	uint8_t  flow_id;            /* MHD_TWT_FLOW_ID_AUTO for auto */
	uint8_t  bcast_id;           /* MHD_TWT_BCAST_ID_AUTO for auto */
} mhd_twt_setup_params_t;

/* TWT teardown parameters */
typedef struct {
	mhd_twt_negotiation_type_t negotiation_type;
	mhd_bool_t teardown_all;
	mhd_mac_t peer_mac;
	mhd_bool_t peer_mac_valid;
	uint8_t flow_id;
	uint8_t bcast_id;
	uint8_t config_id;
} mhd_twt_teardown_params_t;

/* Validates params, builds the native wl_twt_setup_t, and sends the "twt" iovar */
mhd_result_t mhd_wifi_api_twt_setup(mhd_ifidx_t ifidx, const mhd_twt_setup_params_t *params);

/* Validates params, builds the native wl_twt_teardown_t, and sends the "twt" iovar */
mhd_result_t mhd_wifi_api_twt_teardown(mhd_ifidx_t ifidx, const mhd_twt_teardown_params_t *params);

/*
 * @brief Configure MCU sleep allowed or not
 *
 * MCU sleep is controlled by this user command
 *
 * Below are the supported values
 * - Disabled: MCU stay awake always
 * - Enabled : MCU is allowed to go deep sleep
 *
 * @param[in] enab   Bool value to enable or disable MCU sleep
 *
 * @return
 *  - WIFI_RESULT_SUCCESS on success
 *  - Error code otherwise
 */
mhd_result_t wifi_set_mcu_sleep_enab(mhd_bool_t enab);

/*
 * @brief Set the Supplicant port to authorize/deauthorize
 *
 * Supplicant port state is controlled by this user command
 *
 * Below are the supported values
 * - Authorize: Move the associated peer to Authorized state
 * - Deauthorize : Deauthorize the associated peer on any failure
 *
 * @param[in] Address of the peer, Authorized Mask, Flags
 * and Interface type
 *
 * @return
 *  - MHD_SUCCESS on success
 *  - Error code otherwise
 */
mhd_result_t
mhd_wifi_api_set_supp_port(mhd_ifidx_t interface, const mhd_mac_t *mac_addr, uint32_t mask, uint32_t flags);

/*
 * @brief Get current MPC mode
 *
 * This API retrieves the MPC mode configured.
 *
 * @param[out] mpcmode
 * Pointer to store the current mpc mode
 *
 * @return
 * - MHD_SUCCESS        : Operation successful
 * - MHD_BADARG         : Invalid argument (NULL pointer)
 * - Error from underlying ioctl/iovar calls
 */
mhd_result_t mhd_get_mpcmode(mhd_ifidx_t ifidx, mhd_mpcmode_t *mpc_mode);

/*
 * @brief Set MPC mode
 *
 * This API to set the MPC mode
 *
 * @param[in] mpcmode
 * MPC mode to be configured
 *
 * @return
 * - MHD_SUCCESS        : Operation successful
 * - MHD_BADARG         : Invalid argument (NULL pointer)
 * - Error from underlying ioctl/iovar calls
 */
mhd_result_t mhd_set_mpcmode(mhd_ifidx_t ifidx, mhd_mpcmode_t mpc_mode);

/*
 * @brief Get NDP (IPv6) offload status
 *
 * @param[out] status  Pointer to store the current NDP offload state
 *
 * @return
 * - MHD_SUCCESS        : Operation successful
 * - Error from underlying ioctl/iovar calls
 */
mhd_result_t mhd_wifi_get_ndoe(mhd_ifidx_t ifidx, uint32_t *status);
mhd_result_t mhd_wifi_set_ndoe(mhd_ifidx_t ifidx, mhd_bool_t enable);

mhd_result_t mhd_wifi_set_arpoe(mhd_ifidx_t ifidx, mhd_bool_t enable);
mhd_result_t mhd_wifi_get_arpoe(mhd_ifidx_t ifidx, uint32_t *status);

/* Name/value pair for symbolic base offset parsing in pkt_filter_add */
typedef struct mhd_pkt_filter_base_name {
	const char *name;
	uint16_t    val;
} mhd_pkt_filter_base_name_t;

#ifdef MHD_WIFI_PACKET_FILTER

typedef struct mhd_wl_pkt_filter_pattern {
	uint32_t offset;
	uint32_t size_bytes;
	uint8_t mask_and_pattern[];
} mhd_wl_pkt_filter_pattern_t;

typedef struct mhd_wl_pkt_filter_pattern_listel {
	uint16_t rel_offs;
	uint16_t base_offs;
	uint16_t size_bytes;
	uint16_t match_flags;
	uint8_t mask_and_data[];
} mhd_wl_pkt_filter_pattern_listel_t;

typedef struct mhd_wl_pkt_filter_pattern_list {
	uint8_t list_cnt;
	uint8_t PAD1[1];
	uint16_t totsize;
	uint8_t patterns[];
} mhd_wl_pkt_filter_pattern_list_t;

typedef struct mhd_wl_apf_program {
	uint16_t version;
	uint16_t instr_len;
	uint32_t inst_ts;
	uint8_t instrs[];
} mhd_wl_apf_program_t;

typedef struct mhd_wl_pkt_filter_pattern_timeout {
	uint32_t offset;
	uint32_t size_bytes;
	uint32_t timeout;
	uint8_t mask_and_pattern[];
} mhd_wl_pkt_filter_pattern_timeout_t;

typedef struct mhd_wl_pkt_filter {
	uint32_t id;
	uint32_t type;
	uint32_t negate_match;
	union {
		mhd_wl_pkt_filter_pattern_t pattern;
		mhd_wl_pkt_filter_pattern_list_t patlist;
		mhd_wl_apf_program_t apf_program;
		mhd_wl_pkt_filter_pattern_timeout_t pattern_timeout;
	} u;
} mhd_wl_pkt_filter_t;

#define MHD_WL_PKT_FILTER_FIXED_LEN offsetof(mhd_wl_pkt_filter_t, u)
#define MHD_WL_PKT_FILTER_PATTERN_FIXED_LEN \
	offsetof(mhd_wl_pkt_filter_pattern_t, mask_and_pattern)
#define MHD_WL_PKT_FILTER_PATTERN_LIST_FIXED_LEN \
	offsetof(mhd_wl_pkt_filter_pattern_list_t, patterns)
#define MHD_WL_PKT_FILTER_PATTERN_LISTEL_FIXED_LEN \
	offsetof(mhd_wl_pkt_filter_pattern_listel_t, mask_and_data)
#define MHD_WL_PKT_FILTER_PATTERN_TIMEOUT_FIXED_LEN \
	offsetof(mhd_wl_pkt_filter_pattern_timeout_t, mask_and_pattern)

/*
 * @brief Add (install) a packet filter
 *
 * @param[in] pkt_filter   Pointer to packet filter definition payload
 * @param[in] payload_len  Byte length of pkt_filter payload
 *
 * @return
 * - MHD_SUCCESS	: Filter installed successfully
 * - Invalid arguments or underlying ioctl/iovar error
 */
mhd_result_t mhd_wifi_pkt_filter_add(mhd_ifidx_t ifidx, const mhd_wl_pkt_filter_t *pkt_filter, uint16_t payload_len);

/*
 * @brief Enable or disable a packet filter
 *
 * @param[in] id      Packet filter ID
 * @param[in] enable  Enable selector (0 or 1)
 *
 * @return
 * - MHD_SUCCESS	: Operation successful
 * - Invalid arguments or underlying ioctl/iovar error
 */
mhd_result_t mhd_wifi_pkt_filter_enable(mhd_ifidx_t ifidx, uint32_t id, uint32_t enable);

/*
 * @brief List installed packet filters
 *
 * @param[in] enable  Packet filter list selector
 *
 * @return
 * - MHD_SUCCESS	: Operation successful
 * - Invalid arguments or underlying ioctl/iovar error
 */
mhd_result_t mhd_wifi_pkt_filter_list(mhd_ifidx_t ifidx, uint32_t enable);

/*
 * @brief Get packet filter match action (mode)
 *
 * @param[in] void
 *
 * @return
 * - MHD_SUCCESS        : Operation successful
 * - Invalid arguments or underlying ioctl/iovar error
 */
mhd_result_t mhd_get_wifi_pkt_filter_mode(mhd_ifidx_t ifidx);

/*
 * @brief Set packet filter match action (mode)
 *
 * @param[in] Packet filter mode
 *
 * @return
 * - MHD_SUCCESS        : Operation successful
 * - Invalid arguments or underlying ioctl/iovar error
 */

mhd_result_t mhd_set_wifi_pkt_filter_mode(mhd_ifidx_t ifidx, uint32_t mode);

/*
 * @brief Uninstall (delete) a packet filter
 *
 * @param[in] packet filter ID
 *
 * @return
 * - MHD_SUCCESS        : Filter deleted successfully
 * - Invalid arguments or underlying ioctl/iovar error
 */
mhd_result_t mhd_wifi_pkt_filter_delete(mhd_ifidx_t ifidx, uint32_t id);

#ifdef MHD_WIFI_PACKET_FILTER_STATS
/*
 * @brief Retrieve packet filter statistic counter values
 *
 * @param[in] id  Packet filter ID
 *
 * @return
 * - MHD_SUCCESS	: Operation successful
 * - Invalid arguments or underlying ioctl/iovar error
 */
mhd_result_t mhd_wifi_pkt_filter_stats(mhd_ifidx_t ifidx, uint32_t id);

/*
 * @brief Clear packet filter statistic counter values
 *
 * @param[in] id  Packet filter ID
 *
 * @return
 * - MHD_SUCCESS        : Counters cleared successfully
 * - Invalid arguments or underlying ioctl/iovar error
 */
mhd_result_t mhd_wifi_pkt_filter_clear_stats(mhd_ifidx_t ifidx, uint32_t id);
#endif /* MHD_WIFI_PACKET_FILTER_STATS */
#endif /* MHD_WIFI_PACKET_FILTER */


/*
 * @brief Print accumulated WiFi sleep statistics.
 */
void mhd_wifi_pm_stats_print(void);

/*
 * @brief Reset accumulated WiFi sleep statistics.
 */
void mhd_wifi_pm_stats_reset(void);

/*
 * @brief Enable or disable WiFi PM debug logging.
 * @param enable  1 to enable, 0 to disable.
 */
void mhd_wifi_pm_dbg_set(uint8_t enable);

/*
 * @brief Read SR status registers.
 * @param reset  Non-zero to reset counters after read.
 * @return 0 on success, negative on error.
 */
int mhd_wifi_sr_status(int reset);

/*
 * @brief Set a named PM timeout value.
 * @param name  Timeout key: "wake_ack", "ht_avail", or "bus_idle".
 * @param ms    Timeout in milliseconds (0 restores default).
 * @return true if the key was recognized, false otherwise.
 */
mhd_bool_t mhd_wifi_pm_set_timeout(const char *name, uint32_t ms);

/*
 * @brief Get a named PM timeout value.
 * @param name  Timeout key: "wake_ack", "ht_avail", or "bus_idle".
 * @param ms    [out] Current timeout in milliseconds.
 * @return true if the key was recognized, false otherwise.
 */
mhd_bool_t mhd_wifi_pm_get_timeout(const char *name, uint32_t *ms);

/*
 * @brief Print all PM timeout values.
 */
void mhd_wifi_pm_print_timeouts(void);

#ifdef MHD_BUS_AWAKE

/*
 * @brief Force WLAN bus power state.
 * @param awake  1 to keep bus awake, 0 to allow sleep.
 * @return 0 on success, negative on error.
 */
int mhd_wifi_bus_keep_awake(int awake);
#endif /* MHD_BUS_AWAKE */

/**
 * @brief Display MHD driver, firmware, and chip version information.
 *
 * This function ensures that the WLAN bus is active before retrieving
 * and printing the following information:
 * - Chip ID and chip revision
 * - Interface MAC address
 * - MHD driver version along with build date and time
 * - Firmware version string
 *
 * The information is printed through the MHD logging interface.
 *
 * @return MHD_SUCCESS on successful retrieval and display of all version
 *         information, otherwise returns the corresponding error code
 *         from the failed operation.
 */
mhd_result_t mhd_show_versions();

/**
 * @brief Get the primary interface index.
 *
 * Retrieves the interface index associated with the primary MHD interface.
 *
 * @return Interface index of the primary interface.
 */
mhd_ifidx_t mhd_interface_get_primary(void);

/**
 * @brief Create a network interface.
 *
 * Creates a new interface with the specified name and interface type.
 *
 * @param[in] name   Null-terminated interface name.
 * @param[in] iftype Interface type to be assigned to the new interface.
 *
 * @return Interface index of the newly created interface.
 *         Returns an invalid interface index if interface creation fails.
 */
mhd_ifidx_t mhd_interface_create(const char *name, mhd_iftype_t iftype);

void *mhd_interface_get_netif(mhd_ifidx_t ifidx);

/**
 * @brief Get the MHD interface index associated with a network interface.
 *
 * Retrieves the MHD interface identifier mapped to the specified
 * network interface pointer.
 *
 * @param[in]  netif      Network interface pointer.
 * @param[out] interface  Pointer to store the corresponding interface index.
 *
 * @return MHD_SUCCESS on success, otherwise an appropriate error code.
 */
mhd_result_t mhd_wifi_api_get_interface_by_netif(void *netif, mhd_ifidx_t *interface);

/**
 * @brief Bind a network interface to an MHD interface.
 *
 * Associates the specified network interface pointer with the given
 * MHD interface index.
 *
 * @param[in] interface  MHD interface index.
 * @param[in] netif      Network interface pointer to bind.
 *
 * @return MHD_SUCCESS on success, otherwise an appropriate error code.
 */
mhd_result_t mhd_wifi_api_bind_netif(mhd_ifidx_t interface, void *netif);

/**
 * @brief Get the MHD interface index associated with an interface name.
 *
 * Looks up the interface index corresponding to the specified
 * interface name.
 *
 * @param[in]  ifname     Null-terminated interface name string.
 * @param[out] interface  Pointer to store the corresponding interface index.
 *
 * @return MHD_SUCCESS on success, otherwise an appropriate error code.
 */
mhd_result_t mhd_wifi_api_get_interface_by_name(const char *ifname, mhd_ifidx_t *interface);

/**
 * @brief Get the interface name for an MHD interface.
 *
 * Retrieves the interface name associated with the specified
 * MHD interface index.
 *
 * @param[in]  interface   MHD interface index.
 * @param[out] ifname      Buffer to store the interface name.
 * @param[in]  ifname_len  Size of the interface name buffer in bytes.
 *
 * @return MHD_SUCCESS on success, otherwise an appropriate error code.
 */
mhd_result_t mhd_wifi_api_get_ifname(mhd_ifidx_t interface, char *ifname, size_t ifname_len);

/**
 * @brief Set the country code for a Wi-Fi interface.
 *
 * Configures the regulatory country code associated with the specified
 * Wi-Fi interface.
 *
 * @param[in] interface Wi-Fi interface on which to set the country code.
 * @param[in] country   Country code to configure.
 *
 * @return MHD_SUCCESS on success.
 * @return An appropriate MHD error code on failure.
 */
mhd_result_t mhd_wifi_api_set_country(mhd_interface_t interface, mhd_country_code_t country);

/**
 * @brief Get the country code for a Wi-Fi interface.
 *
 * Retrieves the regulatory country code currently configured for the
 * specified Wi-Fi interface.
 *
 * @param[in]  interface Wi-Fi interface from which to retrieve the country code.
 * @param[out] country   Pointer to store the configured country code.
 *
 * @return MHD_SUCCESS on success.
 * @return An appropriate MHD error code on failure.
 */
mhd_result_t mhd_wifi_api_get_country(mhd_interface_t interface, mhd_country_code_t *country);

#endif /* MHD_WIFI_API_H */
