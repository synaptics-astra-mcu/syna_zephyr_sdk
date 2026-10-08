/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_INTERNAL_H
#define SYNA_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Primitive type aliases
 */

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef unsigned int uint;

/*
 * Compiler packing directives
 */

#if defined(_MSC_VER)
#define BWL_PRE_PACKED_STRUCT
#define BWL_POST_PACKED_STRUCT
#elif defined(__GNUC__) || defined(__lint)
#define BWL_PRE_PACKED_STRUCT
#define BWL_POST_PACKED_STRUCT  __attribute__ ((packed))
#elif defined(__CC_ARM)
#define BWL_PRE_PACKED_STRUCT   __packed
#define BWL_POST_PACKED_STRUCT
#else
#define BWL_PRE_PACKED_STRUCT   __packed
#define BWL_POST_PACKED_STRUCT
#endif

/*
 * Boolean constants
 */

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE  1
#endif

typedef enum {
    MHD_FALSE = 0,
    MHD_TRUE  = 1
} mhd_bool_t;

/*
 * Result / error codes
 */


/* From mhd_error.h */
typedef enum {
    MHD_SUCCESS         =   0,
    MHD_ERROR           =  -1,
    MHD_TIMEOUT         =  -2,
    MHD_PENDING         =  -4,
    MHD_BADARG          =  -5,
	MHD_NOTFOUND        =  -12,
    MHD_NOTASSOCIATED   = -17,
    MHD_UNKNOWN_NETWORK_STACK_ERROR =   -28,
    MHD_TCPIP_SOCKET_CLOSED = -129,
    MHD_RESULT_FORCE_32_BIT = 0x7FFFFFFE
} mhd_result_t;

#define MHD_MK_CNTRY(a, b, rev)                                 \
	(((unsigned char)(a)) + (((unsigned char)(b)) << 8) +        \
	 (((unsigned short)(rev)) << 16))
typedef enum
{
	MHD_COUNTRY_UNITED_STATES = MHD_MK_CNTRY('U', 'S', 999), /* US United_States */
	MHD_COUNTRY_EUROPE        = MHD_MK_CNTRY('E', 'U', 999), /* EU Europe */
	MHD_COUNTRY_AUSTRALIA     = MHD_MK_CNTRY('A', 'U', 999), /* AU Australia */
	MHD_COUNTRY_WORLD_WIDE    = MHD_MK_CNTRY('W', 'W', 999), /* WW World_Wide */
} mhd_country_code_t;

/*
 * Network / address types
 */

#define ETHER_ADDR_LEN  6

struct ether_addr {
	uint8_t octet[ETHER_ADDR_LEN];
};

typedef struct ether_addr mhd_mac_t;
typedef struct ether_addr wl_ether_addr_t;

typedef enum {
	HOST_IPV4 = 4,
	HOST_IPV6 = 6
} host_ip_version_t;

typedef struct {
	host_ip_version_t version;
	union {
		uint32_t v4;
		uint32_t v6[4];
	} ip;
} mhd_ip_address_t;

typedef uint32_t host_time_t;

#define MHD_P2P_INTERFACE_IDX 2
#define MHD_INVAL_INTERFACE_IDX -1
typedef int32_t mhd_ifidx_t;

typedef enum {
	MHD_IFTYPE_UNSPECIFIED,
	MHD_IFTYPE_ADHOC,
	MHD_IFTYPE_STATION,
	MHD_IFTYPE_AP,
	MHD_IFTYPE_AP_VLAN,
	MHD_IFTYPE_WDS,
	MHD_IFTYPE_MONITOR,
	MHD_IFTYPE_MESH_POINT,
	MHD_IFTYPE_P2P_CLIENT,
	MHD_IFTYPE_P2P_GO,
	MHD_IFTYPE_P2P_DEVICE,
	MHD_IFTYPE_OCB,
	MHD_IFTYPE_NAN,
	MHD_IFTYPE_MAX
} mhd_iftype_t;

typedef enum {
	NO_POWERSAVE_MODE  = 0,
	PM1_POWERSAVE_MODE = 1,
	PM2_POWERSAVE_MODE = 2,
	PSMODE_FORCE_32BIT = 0x7FFFFFFF
} mhd_psmode_t;

typedef enum {
	MPC_MODE0 = 0,
	MPC_MODE1 = 1,
	MPC_MODE2 = 2,
	MPC_MODE_INVALID
} mhd_mpcmode_t;

/*
 * Security types
 */

/* Security flags (from mhd_structures_ext.h) */
#define WEP_ENABLED         0x0001
#define TKIP_ENABLED        0x0002
#define AES_ENABLED         0x0004
#define SHA256_ENABLED      0x0008
#define SHARED_ENABLED      0x00008000
#define WPA_SECURITY        0x00200000
#define WPA2_SECURITY       0x00400000
#define WPA3_SECURITY       0x00800000
#define GCMP_256_ENABLED    0x01000000
#define ENTERPRISE_ENABLED  0x02000000
#define WPS_ENABLED         0x10000000
#define IBSS_ENABLED        0x20000000
#define WPA3_OWE            0x40000000
#define SAE_EXT_KEY_ENABLED 0x04000000

typedef enum {
	MHD_MFP_DISABLE = 0,
	MHD_MFP_CAPABLE,
	MHD_MFP_REQUIRED
} mhd_mfp_capability_t;

typedef enum {
	MHD_SECURE_OPEN = 0,
	MHD_WPA_PSK_AES,
	MHD_WPA2_PSK_AES,
	MHD_WEP_OPEN,
	MHD_WEP_SHARED,
	MHD_WPA_PSK_TKIP,
	MHD_WPA_PSK_MIXED,
	MHD_WPA2_PSK_TKIP,
	MHD_WPA2_PSK_MIXED,
	MHD_WPA2_PSK_SHA256,
	MHD_WPA3_PSK_SAE,
	MHD_WPA3_PSK_SAE_EXT_KEY,
	MHD_WPS_OPEN,
	MHD_WPS_AES,
	MHD_IBSS_OPEN,
	MHD_WPA_ENT_AES,
	MHD_WPA_ENT_TKIP,
	MHD_WPA_ENT_MIXED,
	MHD_WPA2_ENT_AES,
	MHD_WPA2_ENT_TKIP,
	MHD_WPA2_ENT_MIXED,
	MHD_STA_SECURITY_AUTO = 99,
	MHD_STA_SECURITY_MAX
} mhd_sta_security_t;

typedef enum {
	HOST_SECURITY_OPEN		   = 0,
	HOST_SECURITY_WEP_PSK		= WEP_ENABLED,
	HOST_SECURITY_WEP_SHARED	 = (WEP_ENABLED | SHARED_ENABLED),
	HOST_SECURITY_WPA_TKIP_PSK   = (WPA_SECURITY | TKIP_ENABLED),
	HOST_SECURITY_WPA_AES_PSK	= (WPA_SECURITY | AES_ENABLED),
	HOST_SECURITY_WPA_MIXED_PSK  = (WPA_SECURITY | AES_ENABLED | TKIP_ENABLED),
	HOST_SECURITY_WPA2_AES_PSK   = (WPA2_SECURITY | AES_ENABLED),
	HOST_SECURITY_WPA2_TKIP_PSK  = (WPA2_SECURITY | TKIP_ENABLED),
	HOST_SECURITY_WPA2_MIXED_PSK = (WPA2_SECURITY | AES_ENABLED | TKIP_ENABLED),
	HOST_SECURITY_WPA2_SHA256_PSK = (WPA2_SECURITY | AES_ENABLED | SHA256_ENABLED),
	HOST_SECURITY_WPA2_GCMP_256_PSK = (WPA2_SECURITY | AES_ENABLED | GCMP_256_ENABLED),
	HOST_SECURITY_WPA3_SAE_PSK   = (WPA3_SECURITY | AES_ENABLED),
	HOST_SECURITY_WPA3_SAE_GCMP_256_PSK = (WPA3_SECURITY | AES_ENABLED | GCMP_256_ENABLED),
	HOST_SECURITY_WPA3_OWE	   = (WPA3_OWE | AES_ENABLED),
	HOST_SECURITY_WPA3_SAE_EXT_KEY_PSK = (WPA3_SECURITY | AES_ENABLED | SAE_EXT_KEY_ENABLED),
	HOST_SECURITY_WPA3_SAE_EXT_KEY_GCMP_256_PSK = (WPA3_SECURITY | AES_ENABLED | SAE_EXT_KEY_ENABLED | GCMP_256_ENABLED),
	HOST_SECURITY_WPA_WPA2_MIXED = (WPA_SECURITY | WPA2_SECURITY | AES_ENABLED | TKIP_ENABLED),
	HOST_SECURITY_WPA2_WPA3_MIXED = (WPA2_SECURITY | WPA3_SECURITY | AES_ENABLED),
	HOST_SECURITY_WPA_TKIP_ENT   = (ENTERPRISE_ENABLED | WPA_SECURITY | TKIP_ENABLED),
	HOST_SECURITY_WPA_AES_ENT	= (ENTERPRISE_ENABLED | WPA_SECURITY | AES_ENABLED),
	HOST_SECURITY_WPA_MIXED_ENT  = (ENTERPRISE_ENABLED | WPA_SECURITY | AES_ENABLED | TKIP_ENABLED),
	HOST_SECURITY_WPA2_TKIP_ENT  = (ENTERPRISE_ENABLED | WPA2_SECURITY | TKIP_ENABLED),
	HOST_SECURITY_WPA2_AES_ENT   = (ENTERPRISE_ENABLED | WPA2_SECURITY | AES_ENABLED),
	HOST_SECURITY_WPA2_MIXED_ENT = (ENTERPRISE_ENABLED | WPA2_SECURITY | AES_ENABLED | TKIP_ENABLED),
	HOST_SECURITY_IBSS_OPEN	  = IBSS_ENABLED,
	HOST_SECURITY_WPS_OPEN	   = WPS_ENABLED,
	HOST_SECURITY_WPS_SECURE	 = (WPS_ENABLED | AES_ENABLED),
	HOST_SECURITY_UNKNOWN		= -1,
	HOST_SECURITY_FORCE_32_BIT   = 0x7fffffff
} host_security_t;

typedef enum {
	MHD_AP_OPEN = 0,
	MHD_AP_WPA_AES_PSK,
	MHD_AP_WPA2_AES_PSK,
	MHD_AP_WEP_OPEN,
	MHD_AP_WEP_SHARED,
	MHD_AP_WPA_TKIP_PSK,
	MHD_AP_WPA_MIXED_PSK,
	MHD_AP_WPA2_TKIP_PSK,
	MHD_AP_WPA2_MIXED_PSK,
	MHD_AP_WPA2_SHA256_PSK,
	MHD_AP_WPA3_SAE_PSK,
	MHD_AP_WPA_WPA2_MIXED_PSK,
	MHD_AP_WPA2_WPA3_MIXED_PSK,
	MHD_AP_WPS_OPEN,
	MHD_AP_WPS_AES,
	MHD_AP_SECURITY_MAX
} mhd_ap_security_t;

#define MAX_WPAIE_SIZE  256

typedef struct {
	uint8_t wpa_ie_len;
	uint8_t wpa_ie[MAX_WPAIE_SIZE];
} mhd_wpa_ie_t;

/*
 * Scan types
 */

#define WL_SCAN_PARAMS_SSID_MAX 	10
#define WL_SCAN_PARAMS_FREQ_MAX 	254

typedef enum {
	HOST_SCAN_TYPE_ACTIVE			  = 0x00,
	HOST_SCAN_TYPE_PASSIVE			 = 0x01,
	HOST_SCAN_TYPE_PROHIBITED_CHANNELS = 0x04
} host_scan_type_t;

typedef enum {
	HOST_SCAN_INCOMPLETE,
	HOST_SCAN_COMPLETED,
	HOST_SCAN_ABORTED
} host_scan_status_t;

typedef enum {
	HOST_BSS_TYPE_INFRASTRUCTURE = 0,
	HOST_BSS_TYPE_ADHOC		  = 1,
	HOST_BSS_TYPE_ANY			= 2,
	HOST_BSS_TYPE_UNKNOWN		= -1
} host_bss_type_t;

/*
 * SSID / password / 802.11 frame types
 */

#define SSID_NAME_SIZE  (32)
#define PASSWD_SIZE_MIN (8)
#define PASSWD_SIZE	 (64)

typedef struct {
	uint32_t length;
	uint8_t value[SSID_NAME_SIZE];
} mhd_ssid_t;

typedef struct {
	uint8_t length;
	uint8_t value[PASSWD_SIZE];
} mhd_password_t;

typedef struct wlc_ssid {
	uint32_t SSID_len;
	uint8_t  SSID[32];
} wlc_ssid_t;

typedef struct wl_ext_auth_evt {
	wlc_ssid_t	   ssid;
	struct ether_addr bssid;
	unsigned int	  key_mgmt_suite;
	int			   status;
} wl_ext_auth_evt_t;

/*
 * WAR: FW currently does not include MLD address in
 * external authentication event data. Use separate
 * event information structure to carry MLD address
 * from host side while keeping wl event structure
 * definitions identical between FW and host.
 *
 * TODO: Remove this workaround once FW adds native
 * support for delivering MLD address in ext auth
 * event data.
 */
typedef struct mhd_ext_auth_evt {
	wl_ext_auth_evt_t *wl_ext_auth;
	struct ether_addr mld_addr;
} mhd_ext_auth_evt_t;

/* Frame control macros */
#define FC_TYPE_MASK	 0xC
#define FC_TYPE_SHIFT	2
#define FC_SUBTYPE_MASK  0xF0
#define FC_SUBTYPE_SHIFT 4
#define FC_TYPE_MNG	  0
#define FC_TYPE_CTL	  1
#define FC_TYPE_DATA	 2
#define FC_SUBTYPE_AUTH   11
#define FC_SUBTYPE_ACTION 13
#define FC_KIND(t, s)	(((t) << FC_TYPE_SHIFT) | ((s) << FC_SUBTYPE_SHIFT))
#define FC_AUTH		  FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_AUTH)
#define FC_ACTION		FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ACTION)

/* 802.11 management frame header */
BWL_PRE_PACKED_STRUCT struct dot11_management_header {
	uint16		   fc;
	uint16		   durid;
	struct ether_addr da;
	struct ether_addr sa;
	struct ether_addr bssid;
	uint16		   seq;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_management_header dot11_management_header_t;
#define DOT11_MGMT_HDR_LEN  24

typedef enum {
	MHD_DOT11_RC_RESERVED	= 0,
	MHD_DOT11_RC_UNSPECIFIED = 1
} mhd_dot11_reason_code_t;

/*
 * Chanspec types
 */

typedef uint16_t wl_chanspec_t;

#define WL_CHANSPEC_BAND_MASK  0xc000
#define WL_CHANSPEC_BAND_SHIFT 14
#define WL_CHANSPEC_BW_MASK	0x3800
#define WL_CHANSPEC_BW_SHIFT   11
#define CHSPEC_BAND(chspec)	((chspec) & WL_CHANSPEC_BAND_MASK)
#define CHSPEC_BW(chspec)	  ((chspec) & WL_CHANSPEC_BW_MASK)

#define CH_MAX_2G_CHANNEL				14u	/* Max channel in 2G band */
#define CH_MAX_5G_CHANNEL			   177u	/* Max channel in 5G band */

#define MHD_CHANSPEC_BAND_MAX  3

typedef enum {
	MHD_CHANSPEC_BAND_2G	  = 0,
	MHD_CHANSPEC_BAND_6G	  = 1,
	MHD_CHANSPEC_BAND_5G	  = 3,
	MHD_CHANSPEC_BAND_UNKNOWN = 0xFF
} chanspec_band_t;

typedef enum {
	MHD_CHANSPEC_BW_5	= 0,
	MHD_CHANSPEC_BW_10   = 1,
	MHD_CHANSPEC_BW_20   = 2,
	MHD_CHANSPEC_BW_40   = 3,
	MHD_CHANSPEC_BW_80   = 4,
	MHD_CHANSPEC_BW_160  = 5,
	MHD_CHANSPEC_BW_8080 = 6,
	MHD_CHANSPEC_BW_2P5  = 7,
	MHD_CHANSPEC_BW_INVAL = 0xFFFF
} chanspec_bw_t;

/*
 * Channel / rate / capability types
 */

typedef enum {
	MHD_DFS_USABLE,
	MHD_DFS_UNAVAILABLE,
	MHD_DFS_AVAILABLE
} mhd_dfs_state_t;

typedef struct {
	chanspec_band_t band;
	uint32_t		center_freq;
	uint16_t		freq_offset;
	uint16_t		hw_value;
	uint32_t		flags;
	int			 max_antenna_gain;
	int			 max_power;
	int			 max_reg_power;
	bool			beacon_found;
	uint32_t		orig_flags;
	int			 orig_mag;
	int			 orig_mpwr;
	mhd_dfs_state_t dfs_state;
	unsigned long   dfs_state_entered;
	unsigned int	dfs_cac_ms;
} mhd_wifi_channel_t;

typedef struct {
	uint32_t flags;
	uint16_t bitrate;
	uint16_t hw_value;
	uint16_t hw_value_short;
} mhd_wifi_rate_t;

#define MHD_HT_MCS_MASK_LEN 10

typedef struct mhd_ht_mcs_info_s {
	uint8_t  rx_mask[MHD_HT_MCS_MASK_LEN];
	uint16_t rx_highest;
	uint8_t  tx_params;
	uint8_t  reserved[3];
} __attribute__ ((packed)) mhd_ht_mcs_info_t;

typedef struct mhd_wifi_ht_cap_s {
	mhd_bool_t	   supported;
	uint16_t		 cap;
	uint8_t		  ampdu_factor;
	uint8_t		  ampdu_density;
	mhd_ht_mcs_info_t mcs;
} mhd_wifi_ht_cap_t;

typedef struct mhd_vht_mcs_info_s {
	uint16_t rx_mcs_map;
	uint16_t rx_highest;
	uint16_t tx_mcs_map;
	uint16_t tx_highest;
} __attribute__ ((packed)) mhd_vht_mcs_info_t;

typedef struct mhd_wifi_vht_cap_s {
	mhd_bool_t		 supported;
	uint32_t		   cap;
	mhd_vht_mcs_info_t vht_mcs;
} mhd_wifi_vht_cap_t;

/* HE (802.11ax) capabilities */
#define MHD_HE_MAC_CAP_LEN	   6u
#define MHD_HE_PHY_CAP_LEN	  11u
#define MHD_HE_PPE_THRES_MAX_LEN 25u

typedef uint8_t mhd_he_mac_cap_t[MHD_HE_MAC_CAP_LEN];
typedef uint8_t mhd_he_phy_cap_t[MHD_HE_PHY_CAP_LEN];

typedef struct mhd_he_mcs_nss_supp_s {
	uint16_t rx_mcs_80;
	uint16_t tx_mcs_80;
	uint16_t rx_mcs_160;
	uint16_t tx_mcs_160;
} mhd_he_mcs_nss_supp_t;

typedef struct mhd_wifi_he_cap_s {
	mhd_bool_t		   has_he;
	uint16_t			 types_mask;
	mhd_he_mac_cap_t	 mac_cap_info;
	mhd_he_phy_cap_t	 phy_cap_info;
	mhd_he_mcs_nss_supp_t he_mcs_nss_supp;
	uint8_t			  ppe_thres[MHD_HE_PPE_THRES_MAX_LEN];
	uint16_t			 cap_6ghz;
} mhd_wifi_he_cap_t;

/* EHT (802.11be) capabilities */
#define MHD_EHT_MAC_CAP_LEN		2u
#define MHD_EHT_PHY_CAP_LEN		9u
#define MHD_EHT_PPE_THRES_MAX_LEN 32u
#define WL_EHT_MCS_NSS_SET_MAX_LEN 9u

typedef uint8_t mhd_eht_mac_cap_t[MHD_EHT_MAC_CAP_LEN];
typedef uint8_t mhd_eht_phy_cap_t[MHD_EHT_PHY_CAP_LEN];

struct mhd_eht_mcs_nss_supp_20mhz_only {
	union {
		struct {
			uint8_t rx_tx_mcs7_max_nss;
			uint8_t rx_tx_mcs9_max_nss;
			uint8_t rx_tx_mcs11_max_nss;
			uint8_t rx_tx_mcs13_max_nss;
		};
		uint8_t rx_tx_max_nss[4];
	};
};

struct mhd_eht_mcs_nss_supp_bw {
	union {
		struct {
			uint8_t rx_tx_mcs9_max_nss;
			uint8_t rx_tx_mcs11_max_nss;
			uint8_t rx_tx_mcs13_max_nss;
		};
		uint8_t rx_tx_max_nss[3];
	};
};

typedef struct mhd_eht_mcs_nss_supp_s {
	union {
		struct mhd_eht_mcs_nss_supp_20mhz_only only_20mhz;
		struct {
			struct mhd_eht_mcs_nss_supp_bw _80;
			struct mhd_eht_mcs_nss_supp_bw _160;
			struct mhd_eht_mcs_nss_supp_bw _320;
		};
	};
} mhd_eht_mcs_nss_supp_t;

typedef struct mhd_wifi_eht_cap_s {
	mhd_bool_t			 has_eht;
	uint16_t			   types_mask;
	uint16_t			   mac_cap_info;
	mhd_eht_phy_cap_t	  phy_cap_info;
	mhd_eht_mcs_nss_supp_t eht_mcs_nss_supp;
	uint8_t				ppe_thres[MHD_EHT_PPE_THRES_MAX_LEN];
} mhd_wifi_eht_cap_t;

typedef struct {
	chanspec_band_t		 band;
	mhd_wifi_channel_t	 *channels;
	int					 n_channels;
	mhd_wifi_rate_t		*bitrates;
	int					 n_bitrates;
	mhd_wifi_ht_cap_t	   ht_cap;
	mhd_wifi_vht_cap_t	  vht_cap;
	int					 n_hecaps;
	mhd_wifi_he_cap_t	  *he_cap;
	int					 n_ehtcaps;
	mhd_wifi_eht_cap_t	 *eht_cap;
} mhd_wifi_supported_band_t;

/*
 * Event types
 */

typedef enum {
	WLC_E_NONE						  =  -1,
	WLC_E_SET_SSID					  =   0,
	WLC_E_JOIN						  =   1,
	WLC_E_START						 =   2,
	WLC_E_AUTH						  =   3,
	WLC_E_AUTH_IND					  =   4,
	WLC_E_DEAUTH						=   5,
	WLC_E_DEAUTH_IND					=   6,
	WLC_E_ASSOC						 =   7,
	WLC_E_ASSOC_IND					 =   8,
	WLC_E_REASSOC					   =   9,
	WLC_E_REASSOC_IND				   =  10,
	WLC_E_DISASSOC					  =  11,
	WLC_E_DISASSOC_IND				  =  12,
	WLC_E_QUIET_START				   =  13,
	WLC_E_QUIET_END					 =  14,
	WLC_E_BEACON_RX					 =  15,
	WLC_E_LINK						  =  16,
	WLC_E_MIC_ERROR					 =  17,
	WLC_E_NDIS_LINK					 =  18,
	WLC_E_ROAM						  =  19,
	WLC_E_TXFAIL						=  20,
	WLC_E_PMKID_CACHE				   =  21,
	WLC_E_RETROGRADE_TSF				=  22,
	WLC_E_PRUNE						 =  23,
	WLC_E_AUTOAUTH					  =  24,
	WLC_E_EAPOL_MSG					 =  25,
	WLC_E_SCAN_COMPLETE				 =  26,
	WLC_E_ADDTS_IND					 =  27,
	WLC_E_DELTS_IND					 =  28,
	WLC_E_BCNSENT_IND				   =  29,
	WLC_E_BCNRX_MSG					 =  30,
	WLC_E_BCNLOST_MSG				   =  31,
	WLC_E_ROAM_PREP					 =  32,
	WLC_E_PFN_NET_FOUND				 =  33,
	WLC_E_PFN_NET_LOST				  =  34,
	WLC_E_RESET_COMPLETE				=  35,
	WLC_E_JOIN_START					=  36,
	WLC_E_ROAM_START					=  37,
	WLC_E_ASSOC_START				   =  38,
	WLC_E_IBSS_ASSOC					=  39,
	WLC_E_RADIO						 =  40,
	WLC_E_PSM_WATCHDOG				  =  41,
	WLC_E_CCX_ASSOC_START			   =  42,
	WLC_E_CCX_ASSOC_ABORT			   =  43,
	WLC_E_PROBREQ_MSG				   =  44,
	WLC_E_SCAN_CONFIRM_IND			  =  45,
	WLC_E_PSK_SUP					   =  46,
	WLC_E_COUNTRY_CODE_CHANGED		  =  47,
	WLC_E_EXCEEDED_MEDIUM_TIME		  =  48,
	WLC_E_ICV_ERROR					 =  49,
	WLC_E_UNICAST_DECODE_ERROR		  =  50,
	WLC_E_MULTICAST_DECODE_ERROR		=  51,
	WLC_E_TRACE						 =  52,
	WLC_E_BTA_HCI_EVENT				 =  53,
	WLC_E_IF							=  54,
	WLC_E_P2P_DISC_LISTEN_COMPLETE	  =  55,
	WLC_E_RSSI						  =  56,
	WLC_E_PFN_BEST_BATCHING			=  57,
	WLC_E_EXTLOG_MSG					=  58,
	WLC_E_ACTION_FRAME				  =  59,
	WLC_E_ACTION_FRAME_COMPLETE		 =  60,
	WLC_E_PRE_ASSOC_IND				 =  61,
	WLC_E_PRE_REASSOC_IND			   =  62,
	WLC_E_CHANNEL_ADOPTED			   =  63,
	WLC_E_AP_STARTED					=  64,
	WLC_E_DFS_AP_STOP				   =  65,
	WLC_E_DFS_AP_RESUME				 =  66,
	WLC_E_WAI_STA_EVENT				 =  67,
	WLC_E_WAI_MSG					   =  68,
	WLC_E_ESCAN_RESULT				  =  69,
	WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE =  70,
	WLC_E_PROBRESP_MSG				  =  71,
	WLC_E_P2P_PROBREQ_MSG			  =  72,
	WLC_E_DCS_REQUEST				   =  73,
	WLC_E_FIFO_CREDIT_MAP			  =  74,
	WLC_E_ACTION_FRAME_RX			  =  75,
	WLC_E_WAKE_EVENT					=  76,
	WLC_E_RM_COMPLETE				   =  77,
	WLC_E_HTSFSYNC					  =  78,
	WLC_E_OVERLAY_REQ				   =  79,
	WLC_E_CSA_COMPLETE_IND			  =  80,
	WLC_E_EXCESS_PM_WAKE_EVENT		 =  81,
	WLC_E_PFN_SCAN_NONE				=  82,
	WLC_E_PFN_SCAN_ALLGONE			 =  83,
	WLC_E_GTK_PLUMBED				  =  84,
	WLC_E_ASSOC_IND_NDIS			   =  85,
	WLC_E_REASSOC_IND_NDIS			 =  86,
	WLC_E_ASSOC_REQ_IE				 =  87,
	WLC_E_ASSOC_RESP_IE				=  88,
	WLC_E_ASSOC_RECREATED			  =  89,
	WLC_E_ACTION_FRAME_RX_NDIS		 =  90,
	WLC_E_AUTH_REQ					  =  91,
	WLC_E_TDLS_PEER_EVENT			  =  92,
	WLC_E_SPEEDY_RECREATE_FAIL		 =  93,
	WLC_E_NATIVE						=  94,
	WLC_E_PKTDELAY_IND				 =  95,
	WLC_E_AWDL_AW					  =  96,
	WLC_E_AWDL_ROLE					=  97,
	WLC_E_AWDL_EVENT				   =  98,
	WLC_E_NIC_AF_TXS				   =  99,
	WLC_E_NIC_NIC_REPORT			   = 100,
	WLC_E_BEACON_FRAME_RX			  = 101,
	WLC_E_SERVICE_FOUND				= 102,
	WLC_E_GAS_FRAGMENT_RX			  = 103,
	WLC_E_GAS_COMPLETE				 = 104,
	WLC_E_P2PO_ADD_DEVICE			  = 105,
	WLC_E_P2PO_DEL_DEVICE			  = 106,
	WLC_E_WNM_STA_SLEEP			   = 107,
	WLC_E_TXFAIL_THRESH				= 108,
	WLC_E_PROXD						= 109,
	WLC_E_IBSS_COALESCE				= 110,
	WLC_E_AWDL_RX_PRB_RESP			= 111,
	WLC_E_AWDL_RX_ACT_FRAME		   = 112,
	WLC_E_AWDL_WOWL_NULLPKT		   = 113,
	WLC_E_AWDL_PHYCAL_STATUS		  = 114,
	WLC_E_AWDL_OOB_AF_STATUS		  = 115,
	WLC_E_AWDL_SCAN_STATUS			= 116,
	WLC_E_AWDL_AW_START			   = 117,
	WLC_E_AWDL_AW_END				 = 118,
	WLC_E_AWDL_AW_EXT				 = 119,
	WLC_E_AWDL_PEER_CACHE_CONTROL	 = 120,
	WLC_E_CSA_START_IND				= 121,
	WLC_E_CSA_DONE_IND				 = 122,
	WLC_E_CSA_FAILURE_IND			  = 123,
	WLC_E_CCA_CHAN_QUAL				= 124,
	WLC_E_BSSID						= 125,
	WLC_E_TX_STAT_ERROR				= 126,
#ifdef MHD_DHCPC
	WLC_E_DHCPC_EVENT				  = 126,
#endif
	WLC_E_BCMC_CREDIT_SUPPORT		  = 127,
	WLC_E_PSTA_PRIMARY_INTF_IND		= 128,
	WLC_E_BT_WIFI_HANDOVER_REQ		 = 130,
	WLC_E_SPW_TXINHIBIT				= 131,
	WLC_E_FBT_AUTH_REQ_IND			 = 132,
	WLC_E_RSSI_LQM					 = 133,
	WLC_E_PFN_GSCAN_FULL_RESULT		= 134,
	WLC_E_PFN_SWC					  = 135,
	WLC_E_AUTHORIZED				   = 136,
	WLC_E_PROBREQ_MSG_RX			  = 137,
	WLC_E_PFN_SCAN_COMPLETE			= 138,
	WLC_E_RMC_EVENT					= 139,
	WLC_E_DPSTA_INTF_IND			   = 140,
	WLC_E_RRM						  = 141,
	WLC_E_PFN_SSID_EXT				 = 142,
	WLC_E_ROAM_EXP_EVENT			   = 143,
	WLC_E_QUICK_REASSOC				= 187,
	WLC_E_TWT						  = 195,
	WLC_E_MLO_LINK_INFO				= 203,
#ifdef MHD_WLAN_CSI
	WLC_E_CSI						  = 207,
#endif
	WLC_E_LAST						 = 208,
	WLC_E_FORCE_32_BIT				 = 0x7FFFFFFE
} mhd_event_num_t;

/* Event message flags */
#define WLC_EVENT_MSG_LINK	 0x01
#define WLC_EVENT_MSG_FLUSHTXQ 0x02
#define WLC_EVENT_MSG_GROUP	0x04
#define WLC_EVENT_MSG_UNKBSS   0x08
#define WLC_EVENT_MSG_UNKIF	0x10

#define WLC_SUP_STATUS_OFFSET	  256
#define WLC_DOT11_SC_STATUS_OFFSET 512

typedef enum {
	WLC_E_STATUS_SUCCESS		   =  0,
	WLC_E_STATUS_FAIL			  =  1,
	WLC_E_STATUS_TIMEOUT		   =  2,
	WLC_E_STATUS_NO_NETWORKS	   =  3,
	WLC_E_STATUS_ABORT			 =  4,
	WLC_E_STATUS_NO_ACK			=  5,
	WLC_E_STATUS_UNSOLICITED	   =  6,
	WLC_E_STATUS_ATTEMPT		   =  7,
	WLC_E_STATUS_PARTIAL		   =  8,
	WLC_E_STATUS_NEWSCAN		   =  9,
	WLC_E_STATUS_NEWASSOC		  = 10,
	WLC_E_STATUS_11HQUIET		  = 11,
	WLC_E_STATUS_SUPPRESS		  = 12,
	WLC_E_STATUS_NOCHANS		   = 13,
	WLC_E_STATUS_CCXFASTRM		 = 14,
	WLC_E_STATUS_CS_ABORT		  = 15,
	WLC_E_STATUS_WAIT_RXBCN_TIMEOUT = 22,
	WLC_E_STATUS_6G_NO_TPE		 = 23,
	WLC_E_STATUS_CHANNELSWITCH	 = 24,
	WLC_E_STATUS_PREF_LINK_SWAP_FAIL = 25,
	WLC_E_STATUS_MLO_ROAM_FAIL	 = 26,
	/* WPA supplicant status */
	WLC_SUP_DISCONNECTED		   = 0  + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_CONNECTING			 = 1  + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_IDREQUIRED			 = 2  + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_AUTHENTICATING		 = 3  + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_AUTHENTICATED		  = 4  + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_KEYXCHANGE			 = 5  + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_KEYED				  = 6  + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_TIMEOUT				= 7  + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_LAST_BASIC_STATE	   = 8  + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_KEYXCHANGE_WAIT_M1	 = (int)WLC_SUP_AUTHENTICATED,
	WLC_SUP_KEYXCHANGE_PREP_M2	 = (int)WLC_SUP_KEYXCHANGE,
	WLC_SUP_KEYXCHANGE_WAIT_M3	 = (int)WLC_SUP_LAST_BASIC_STATE,
	WLC_SUP_KEYXCHANGE_PREP_M4	 = 9  + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_KEYXCHANGE_WAIT_G1	 = 10 + WLC_SUP_STATUS_OFFSET,
	WLC_SUP_KEYXCHANGE_PREP_G2	 = 11 + WLC_SUP_STATUS_OFFSET,
	/* 802.11 status codes */
	WLC_DOT11_SC_SUCCESS				  =  0 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_FAILURE				  =  1 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_CAP_MISMATCH			 = 10 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_REASSOC_FAIL			 = 11 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_FAIL			   = 12 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_AUTH_MISMATCH			= 13 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_AUTH_SEQ				 = 14 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_AUTH_CHALLENGE_FAIL	  = 15 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_AUTH_TIMEOUT			 = 16 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_BUSY_FAIL		  = 17 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_RATE_MISMATCH	  = 18 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_SHORT_REQUIRED	 = 19 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_PBCC_REQUIRED	  = 20 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_AGILITY_REQUIRED   = 21 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_SPECTRUM_REQUIRED  = 22 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_BAD_POWER_CAP	  = 23 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_BAD_SUP_CHANNELS   = 24 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_SHORTSLOT_REQUIRED = 25 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_ERPBCC_REQUIRED	= 26 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_ASSOC_DSSOFDM_REQUIRED   = 27 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_DECLINED				 = 37 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_INVALID_PARAMS		   = 38 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_INVALID_AKMP			 = 43 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_INVALID_MDID			 = 54 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_DOT11_SC_INVALID_FTIE			 = 55 + WLC_DOT11_SC_STATUS_OFFSET,
	WLC_E_STATUS_FORCE_32_BIT			 = 0x7FFFFFFE
} mhd_event_status_t;

#define WLC_E_PRUNE_REASON_OFFSET	256
#define WLC_E_SUP_REASON_OFFSET	  512
#define WLC_E_DOT11_RC_REASON_OFFSET 768

#define MHD_EVENT_MASK_MAX_SIZE  32
#define MHD_EVENT_DESC_MAX_COUNT 8

typedef enum {
	EVENT_DESC_NONE  = -1,
	EVENT_DESC_FIRST =  0,
	EVENT_DESC_LAST  = MHD_EVENT_DESC_MAX_COUNT - 1
} mhd_event_desc_t;

typedef struct mhd_event_mask_s {
	uint8_t mask[MHD_EVENT_MASK_MAX_SIZE];
} mhd_event_mask_t;

typedef enum {
	/* Roam reason codes */
	WLC_E_REASON_INITIAL_ASSOC	= 0,
	WLC_E_REASON_LOW_RSSI		 = 1,
	WLC_E_REASON_DEAUTH		   = 2,
	WLC_E_REASON_DISASSOC		 = 3,
	WLC_E_REASON_BCNS_LOST		= 4,
	WLC_E_REASON_FAST_ROAM_FAILED = 5,
	WLC_E_REASON_DIRECTED_ROAM	= 6,
	WLC_E_REASON_TSPEC_REJECTED   = 7,
	WLC_E_REASON_BETTER_AP		= 8,
	/* Prune reason codes */
	WLC_E_PRUNE_ENCR_MISMATCH	= 1  + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_BCAST_BSSID	  = 2  + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_MAC_DENY		 = 3  + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_MAC_NA		   = 4  + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_REG_PASSV		= 5  + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_SPCT_MGMT		= 6  + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_RADAR			= 7  + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_RSN_MISMATCH		   = 8  + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_NO_COMMON_RATES  = 9  + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_BASIC_RATES	  = 10 + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_CCXFAST_PREVAP   = 11 + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_CIPHER_NA		= 12 + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_KNOWN_STA		= 13 + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_CCXFAST_DROAM	= 14 + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_WDS_PEER		 = 15 + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_QBSS_LOAD		= 16 + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_HOME_AP		  = 17 + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_AP_BLOCKED	   = 18 + WLC_E_PRUNE_REASON_OFFSET,
	WLC_E_PRUNE_NO_DIAG_SUPPORT  = 19 + WLC_E_PRUNE_REASON_OFFSET,
	/* WPA supplicant failure reason codes */
	WLC_E_SUP_OTHER			  = 0  + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_DECRYPT_KEY_DATA   = 1  + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_BAD_UCAST_WEP128  = 2  + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_BAD_UCAST_WEP40   = 3  + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_UNSUP_KEY_LEN	 = 4  + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_PW_KEY_CIPHER	 = 5  + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_MSG3_TOO_MANY_IE  = 6  + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_MSG3_IE_MISMATCH  = 7  + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_NO_INSTALL_FLAG   = 8  + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_MSG3_NO_GTK	   = 9  + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_GRP_KEY_CIPHER	= 10 + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_GRP_MSG1_NO_GTK   = 11 + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_GTK_DECRYPT_FAIL  = 12 + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_SEND_FAIL		 = 13 + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_DEAUTH			= 14 + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_WPA_PSK_TMO	   = 15 + WLC_E_SUP_REASON_OFFSET,
	WLC_E_SUP_GTK_UPDATE			 = 23,
	WLC_E_SUP_KDK_UPDATE_FAIL		= 24,
	WLC_E_SUP_NO_MLO_GTK			 = 25,
	WLC_E_SUP_NO_IGTK				= 26,
	WLC_E_SUP_NO_BIGTK			   = 27,
	WLC_E_SUP_NO_MLO_IGTK			= 28,
	WLC_E_SUP_NO_MLO_BIGTK		   = 29,
	WLC_E_SUP_IGTK_DECRYPT_FAIL	  = 30,
	WLC_E_SUP_IGTK_UPDATE_FAIL	   = 31,
	WLC_E_SUP_BIGTK_DECRYPT_FAIL	 = 32,
	WLC_E_SUP_BIGTK_UPDATE_FAIL	  = 33,
	WLC_E_SUP_GTK_BAD_KEY_IDX		= 34,
	WLC_E_SUP_IGTK_BAD_KEY_IDX	   = 35,
	WLC_E_SUP_BIGTK_BAD_KEY_IDX	  = 36,
	WLC_E_SUP_BCN_PROT_DISABLED_AP   = 37,
	WLC_E_SUP_GTK_BAD_LINK_ID		= 38,
	WLC_E_SUP_IGTK_BAD_LINK_ID	   = 39,
	WLC_E_SUP_BIGTK_BAD_LINK_ID	  = 40,
	WLC_E_SUP_FT_ELEM_CNT_MISMATCH   = 41,
	WLC_E_SUP_M4_TX_NOACK			= 42,
	/* 802.11 reason codes */
	DOT11_RC_RESERVED			=  0 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_UNSPECIFIED		 =  1 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_AUTH_INVAL		  =  2 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_DEAUTH_LEAVING	  =  3 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_INACTIVITY		  =  4 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_BUSY				=  5 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_INVAL_CLASS_2	   =  6 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_INVAL_CLASS_3	   =  7 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_DISASSOC_LEAVING	=  8 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_NOT_AUTH			=  9 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_BAD_PC			  = 10 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_BAD_CHANNELS		= 11 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_UNSPECIFIED_QOS	 = 32 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_INSUFFCIENT_BW	  = 33 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_EXCESSIVE_FRAMES	= 34 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_TX_OUTSIDE_TXOP	 = 35 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_LEAVING_QBSS		= 36 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_BAD_MECHANISM	   = 37 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_SETUP_NEEDED		= 38 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_TIMEOUT			 = 39 + WLC_E_DOT11_RC_REASON_OFFSET,
	DOT11_RC_MAX				 = 23 + WLC_E_DOT11_RC_REASON_OFFSET,
	WLC_E_REASON_FORCE_32_BIT	= 0x7FFFFFFE
} mhd_event_reason_t;

/* Event header */
struct host_event_header_struct {
	uint16_t		   reserved1;
	uint16_t		   flags;
	mhd_event_num_t	event_type;
	mhd_event_status_t status;
	mhd_event_reason_t reason;
	uint32_t		   auth_type;
	uint32_t		   datalen;
	mhd_mac_t		  addr;
	mhd_ifidx_t	interface;
};
typedef struct host_event_header_struct mhd_event_header_t;
typedef void (*mhd_event_handler_t)(const mhd_event_header_t *event_header, const uint8_t *event_data, void *user_data);

/*
 * Association manager
 */

#define WL_ASSOC_MGR_CMD_PAUSE_ON_EVT        0
#define WL_ASSOC_MGR_CMD_ABORT_ASSOC         1
#define WL_ASSOC_MGR_CMD_SET_SAE_FRAME       2
#define WL_ASSOC_MGR_CMD_SEND_AUTH           3

#define WL_ASSOC_MGR_PARAMS_EVENT_NONE                0
#define WL_ASSOC_MGR_PARAMS_PAUSE_EVENT_AUTH_RESP     1

/*
 * STA flags
 */

#define MHD_STA_AUTHORIZED    BIT(0)
#define MHD_STA_WMM           BIT(1)
#define MHD_STA_SHORT_PREAMBLE BIT(2)
#define MHD_STA_MFP           BIT(3)
#define MHD_STA_TDLS_PEER     BIT(4)
#define MHD_STA_AUTHENTICATED BIT(5)
#define MHD_STA_ASSOCIATED    BIT(6)
#define MHD_STA_SPP_AMSDU     BIT(7)

#define MHD_CHAN_DISABLED BIT(0)
#define MHD_CHAN_NO_IR	 BIT(1)
#define MHD_CHAN_RADAR	 BIT(3)
#define MHD_CHAN_NO_HT40PLUS   BIT(4)
#define MHD_CHAN_NO_HT40MINUS  BIT(5)
#define MHD_CHAN_NO_80MHZ BIT(7)
#define MHD_CHAN_NO_160MHZ BIT(8)
#define MHD_CHAN_NO_20MHZ BIT(11)

/*
 * Memory allocation helpers
 */

#ifndef MALLOC
#define MALLOC(s) malloc(s)
#endif

#ifndef FREE
#define FREE(p)   free(p)
#endif

/*
 * Forward declarations for opaque internal types
 */

typedef struct wl_event_rx_frame_data wl_event_rx_frame_data_t;
typedef struct wl_action_frame wl_action_frame_t;
typedef struct wl_wsec_key wl_wsec_key_t;
typedef struct host_scan_extended_params host_scan_extended_params_t;
typedef struct mhd_pub mhd_pub_t;

/*
 * BSS info (wl_bss_info_t) -- needed by scan callbacks
 */

#define MCSSET_LEN  16

typedef struct wl_bss_info {
	uint32_t version;
	uint32_t length;
	struct ether_addr BSSID;
	uint16_t beacon_period;
	uint16_t capability;
	uint8_t  SSID_len;
	uint8_t  SSID[32];
	uint8_t  bcnflags;
	struct {
		uint32_t count;
		uint8_t  rates[16];
	} rateset;
	wl_chanspec_t chanspec;
	uint16_t atim_window;
	uint8_t  dtim_period;
	uint8_t  accessnet;
	int16_t  RSSI;
	int8_t   phy_noise;
	uint8_t  n_cap;
	uint8_t  he_cap;
	uint8_t  load;
	uint32_t nbss_cap;
	uint8_t  ctl_ch;
	uint8_t  padding1[3];
	uint16_t vht_rxmcsmap;
	uint16_t vht_txmcsmap;
	uint8_t  flags;
	uint8_t  vht_cap;
	uint8_t  reserved[2];
	uint8_t  basic_mcs[MCSSET_LEN];
	uint16_t ie_offset;
	uint16_t freespace2;
	uint32_t ie_length;
	int16_t  SNR;
	uint16_t vht_mcsmap;
	uint16_t vht_mcsmap_prop;
	uint16_t vht_txmcsmap_prop;
	uint32_t he_mcsmap;
	uint32_t he_rxmcsmap;
	uint32_t he_txmcsmap;
	uint32_t timestamp[2];
} wl_bss_info_t;

/* Chanspec helper declared in libmhd_lib.a */
uint8 mhd_chspec_primary20_chan(wl_chanspec_t chspec);

/*
 * Channel definition types (CSA event support)
 */

typedef enum
{
	MHD_CHAN_NO_HT,
	MHD_CHAN_HT20,
	MHD_CHAN_HT40MINUS,
	MHD_CHAN_HT40PLUS
} mhd_channel_type_t;

typedef struct mhd_chandef_s
{
	uint8_t primary_chan;
	chanspec_bw_t bw;
	chanspec_band_t band;
	mhd_channel_type_t chan_type;
	uint32_t center_freq1;
	uint32_t center_freq2;
} mhd_chandef_t;

typedef struct csa_event_data_s
{
	mhd_chandef_t chandef;
	uint8_t link_id;
	uint32_t punct_bitmap;
} csa_event_data_t;

int mhd_channel_to_frequency(int chan, chanspec_band_t band);

/*
 * Utility macros
 */

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) ((void)(x))
#endif

/*
 * WLU command types -- needed by counters API in libmhd_test.a
 */

typedef struct mhd_wlu_cmd_s cmd_t;
typedef int (cmd_func_t)(void *wl, cmd_t *cmd, char **argv);

struct mhd_wlu_cmd_s
{
	const char   *name;
	cmd_func_t   *func;
	int           get;
	int           set;
	const char   *help;
};

#define WLC_GET_VAR  ((uint32_t)262)

int wl_counters(void *wl, cmd_t *cmd, char **argv);
int wl_clear_counters(void *wl, cmd_t *cmd, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* SYNA_INTERNAL_H */
