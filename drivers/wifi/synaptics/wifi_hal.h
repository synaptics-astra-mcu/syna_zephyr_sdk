/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"

#include <stdio.h>
#include <stdlib.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/wifi.h>
#include <zephyr/sd/sd.h>
#include <zephyr/sd/sdio.h>

#define WIFI_HAL_VER "V9.26.07.23"

#define CONFIG_MHD_WORKER_PRIO 3
#if defined(CONFIG_NET_TC_THREAD_COOPERATIVE)
#define CONFIG_WIFI_SYNA_TASK_PRIO  K_PRIO_COOP(CONFIG_MHD_WORKER_PRIO)
#else
#define CONFIG_WIFI_SYNA_TASK_PRIO  K_PRIO_PREEMPT(CONFIG_MHD_WORKER_PRIO)
#endif

#define CONFIG_WIFI_SYNA_TASK_STACK_SIZE (2048 + 1024)
#define HOST_LINK_MTU   (1500)
typedef void*  host_buffer_t;
typedef struct k_mutex  *host_mutex_type_t;
typedef struct k_sem    *host_semaphore_type_t;
typedef struct k_thread *host_thread_type_t;
typedef struct k_queue  *host_queue_type_t;
typedef struct k_event  *host_event_type_t;

typedef enum
{
	mhd_idle = 0,
	mhd_scan_start,
	mhd_scan_finish,
	mhd_conect_start,
	mhd_connect_finish,
	mhd_link_up,
	mhd_link_down,
	mhd_dhcp_start,
	mhd_dhcp_finish,
} mhd_iface_state;

typedef struct syna_wifi_data_ {
	struct sd_card card;
	struct sdio_func sdio_func1;
	struct sdio_func sdio_func2;

	struct net_if *iface;
	struct k_sem sema;
	enum wifi_iface_state state;

	struct gpio_callback gpio_cb;
	bool sta_connected;
	bool match_found;
	bool mhd_init;
	bool mhd_up;
} syna_wifi_data_t;

extern int mhd_wlan_initialised;
extern syna_wifi_data_t syna_wifi_data;

int host_buffer_get(host_buffer_t *buffer, int direction, unsigned short size, int wait);
uint8_t *host_buffer_get_current_piece_data_pointer(host_buffer_t buffer);
uint16_t host_buffer_get_current_piece_size(host_buffer_t buffer);
void host_buffer_release(host_buffer_t buffer, int direction);

typedef enum
{
	MHD_STA_INTERFACE = 0, /**< STA or Client Interface */
	MHD_AP_INTERFACE  = 1, /**< softAP Interface */
	MHD_P2P_INTERFACE = 2, /**< P2P Interface */
} mhd_interface_t;

typedef struct
{
	char ssid[32];
	char bssid[6];
	uint32_t channel;
	uint32_t security;
	uint32_t rssi;
	char ccode[4];
	uint8_t band;
} mhd_ap_info_t;

typedef enum
{
	MHD_SECURE_OPEN,
	MHD_WPA_PSK_AES,     // WPA-PSK AES
	MHD_WPA2_PSK_AES,    // WPA2-PSK AES
	MHD_WEP_OPEN,        // WEP+OPEN
	MHD_WEP_SHARED,      // WEP+SHARE
	MHD_WPA_PSK_TKIP,    // WPA-PSK TKIP
	MHD_WPA_PSK_MIXED,   // WPA-PSK AES & TKIP MIXED
	MHD_WPA2_PSK_TKIP,   // WPA2-PSK TKIP
	MHD_WPA2_PSK_MIXED,  // WPA2-PSK AES & TKIP MIXED
	MHD_WPA2_PSK_SHA256, // WPA2-PSK-SHA256
	MHD_WPA3_PSK_SAE,    // WPA3-PSK SAE
	MHD_WPS_OPEN,        // WPS OPEN, NOT supported
	MHD_WPS_AES,         // WPS AES, NOT supported
	MHD_IBSS_OPEN,       // ADHOC, NOT supported
	MHD_WPA_ENT_AES,     // WPA-ENT AES, NOT supported
	MHD_WPA_ENT_TKIP,    // WPA-ENT TKIP, NOT supported
	MHD_WPA_ENT_MIXED,   // WPA-ENT AES & TKIP MIXED, NOT supported
	MHD_WPA2_ENT_AES,    // WPA2-ENT AES, NOT supported
	MHD_WPA2_ENT_TKIP,   // WPA2-ENT TKIP, NOT supported
	MHD_WPA2_ENT_MIXED,  // WPA2-ENT AES & TKIP MIXED, NOT supported
} mhd_sta_security_t;

extern void mhd_thread_notify_irq( void );
extern void mhd_iperf_tcptx( char *ip_addr, int time, int port, int wnd );
extern void mhd_iperf_tcprx( int time, int port, int wnd);
extern void mhd_iperf_udptx( char *ip_addr, int time, int port, int wnd );
extern void mhd_iperf_udprx( int time, int port, int wnd );

// join ap with ssid/pass
extern int mhd_join_ap( const char *ssid, const char *password );
// disconnect from ap
extern int mhd_leave_ap( void );
// get sta/ap interface ip/mask/gw
extern int wlu_iovar_get(void *wl, const char *iovar, void *outbuf, int len);

//set wifi task priority
extern void set_mhdtask_settings( uint32_t priority );
// download fw/nvram image, driver init. 0: success, 1: failed
extern int mhd_module_init(void);
// driver deinit. 0: success, 1: failed
extern int mhd_module_exit(void);

// get fw version. 0: success, 1: failed
extern int mhd_get_firmware_version( char *version, int length );

// scan in station mode
// before mhd_start_scan, set scan_ssid if ssid is NOT NULL
// if ssid is NULL, will scan all ssid
extern int mhd_set_scan_ssid(char *ssid);
extern int mhd_start_scan(void);
// get scan result in station mode
extern int mhd_get_scan_results(mhd_ap_info_t *results, int *num);

// ssid:  less than 32 bytes
// password: less than 32 bytes
// security: 0-open, 1-wpa_psk_aes, 2-wpa2_psk_aes
// channel: 1~13
// ip_address: 0x0100a8c0 means 192.168.0.1
extern int mhd_softap_start(const char *ssid, const char *password, uint8_t security, uint8_t channel);

//config ap interface ip/mask/gw address
extern int mhd_softap_ipconfig(uint32_t ip, uint32_t mask, uint32_t gw);
// stop softap.
// input 0:not force, 1:force to stop
// return 0:success, 1:failed
extern int mhd_softap_stop(uint8_t force);

// station connects to ap. 0:success, 1:failed
// security: 0-open, 1-wpa_psk_aes, 2-wpa2_psk_aes
extern int mhd_sta_connect( const char *ssid, char *bssid, uint8_t security, const char *password, uint8_t channel );
// station reassociate with ap. 0:success, 1:failed
extern int mhd_sta_reassociate( void );
// station leaves ap and network down
// input 0:not force, 1:force to disconnect
// return 0:success, 1:failed
extern int mhd_sta_disconnect( uint8_t force );
// get station state. 0:disabled, 1:enabled, -1:error
extern int mhd_sta_get_state( void );
// station get connection state. 0:disconnected, 1:connected
extern int mhd_sta_get_connection( void );
// set station interface up, and config static ip if necessary
extern int mhd_sta_network_up( uint32_t ip, uint32_t gateway, uint32_t netmask );
// set station interface down
extern int mhd_sta_network_down( void );

// set country code. This function should be called before mhd_module_init()
// return 0:success, 1:failed
extern int mhd_set_country_code(uint32_t country);

extern int mhd_sta_set_powersave( uint8_t mode, uint8_t time_ms );
extern void mhd_set_scansuppress( int state );
extern int mhd_sta_set_dtim_interval( int dtim_interval_ms );
extern int mhd_sta_set_bcn_li_dtim( uint8_t dtim );

extern int mhd_ap_get_ssid( char ssid_data[] );
extern int mhd_sta_get_ssid( char ssid_data[] );
extern int mhd_sta_get_bssid( char mac_addr[] );
extern int mhd_sta_get_rate( void );
extern int mhd_sta_get_rssi( void );
extern int mhd_wifi_get_channel( mhd_interface_t interface, uint32_t* channel );

extern void rtos_printf(const char *format, ...);

int host_buffer_get(host_buffer_t *buffer, int direction, unsigned short size, int wait);
uint8_t *host_buffer_get_current_piece_data_pointer(host_buffer_t buffer);
uint16_t host_buffer_get_current_piece_size(host_buffer_t buffer);
void host_buffer_release(host_buffer_t buffer, int direction);

extern int mhd_wifi_get_mac_address(char* mac, mhd_interface_t interface);
extern int mhd_config_fwlog(uint8_t enable, uint32_t interval);
extern void host_network_process_ethernet_data(host_buffer_t buffer, int interface);
extern int mhd_wifi_get_security(mhd_interface_t interface, uint32_t *sec);
extern int host_buffer_add_remove_at_front(host_buffer_t * buffer, int32_t add_remove_amount);

extern void enable_dhcpv4_server(char *ip);
extern int sdio_init(void);
extern int wifi_up(void);
extern int mhd_is_initialized(void);
struct net_if *mhd_get_sta_netif(void);
int mhd_get_sta_netif_idx(void);
struct net_if *mhd_get_ap_netif(void);
int mhd_get_ap_netif_idx(void);


#if 0
mhd_wlan_err.txt
MHD_PENDING,                         1),   /**< Pending */                           \
MHD_TIMEOUT,                         2),   /**< Timeout */                           \
MHD_ERROR,                           4),   /**< Error */                          \
MHD_BADARG,                          5),   /**< Bad Arguments */                  \
MHD_BADOPTION,                       6),   /**< Mode not supported */             \
MHD_PARTIAL_RESULTS,              1003),   /**< Partial results */                   \
MHD_INVALID_KEY,                  1004),   /**< Invalid key */                       \
MHD_DOES_NOT_EXIST,               1005),   /**< Does not exist */                    \
MHD_NOT_AUTHENTICATED,            1006),   /**< Not authenticated */                 \
MHD_NOT_KEYED,                    1007),   /**< Not keyed */                         \
MHD_IOCTL_FAIL,                   1008),   /**< IOCTL fail */                        \
MHD_BUFFER_UNAVAILABLE_TEMPORARY, 1009),   /**< Buffer unavailable temporarily */    \
MHD_BUFFER_UNAVAILABLE_PERMANENT, 1010),   /**< Buffer unavailable permanently */    \
MHD_WPS_PBC_OVERLAP,              1011),   /**< WPS PBC overlap */                   \
MHD_CONNECTION_LOST,              1012),   /**< Connection lost */                   \
MHD_OUT_OF_EVENT_HANDLER_SPACE,   1013),   /**< Cannot add extra event handler */    \
MHD_SEMAPHORE_ERROR,              1014),   /**< Error manipulating a semaphore */    \
MHD_FLOW_CONTROLLED,              1015),   /**< Packet retrieval cancelled due to flow control */ \
MHD_NO_CREDITS,                   1016),   /**< Packet retrieval cancelled due to lack of bus credits */ \
MHD_NO_PACKET_TO_SEND,            1017),   /**< Packet retrieval cancelled due to no pending packets */ \
MHD_CORE_CLOCK_NOT_ENABLED,       1018),   /**< Core disabled due to no clock */    \
MHD_CORE_IN_RESET,                1019),   /**< Core disabled - in reset */         \
MHD_UNSUPPORTED,                  1020),   /**< Unsupported function */             \
MHD_BUS_WRITE_REGISTER_ERROR,     1021),   /**< Error writing to WLAN register */   \
MHD_SDIO_BUS_UP_FAIL,             1022),   /**< SDIO bus failed to come up */       \
MHD_JOIN_IN_PROGRESS,             1023),   /**< Join not finished yet */   \
MHD_NETWORK_NOT_FOUND,            1024),   /**< Specified network was not found */   \
MHD_INVALID_JOIN_STATUS,          1025),   /**< Join status error */   \
MHD_UNKNOWN_INTERFACE,            1026),   /**< Unknown interface specified */ \
MHD_SDIO_RX_FAIL,                 1027),   /**< Error during SDIO receive */   \
MHD_HWTAG_MISMATCH,               1028),   /**< Hardware tag header corrupt */   \
MHD_RX_BUFFER_ALLOC_FAIL,         1029),   /**< Failed to allocate a buffer to receive into */   \
MHD_BUS_READ_REGISTER_ERROR,      1030),   /**< Error reading a bus hardware register */   \
MHD_THREAD_CREATE_FAILED,         1031),   /**< Failed to create a new thread */   \
MHD_QUEUE_ERROR,                  1032),   /**< Error manipulating a queue */   \
MHD_BUFFER_POINTER_MOVE_ERROR,    1033),   /**< Error moving the current pointer of a packet buffer  */   \
MHD_BUFFER_SIZE_SET_ERROR,        1034),   /**< Error setting size of packet buffer */   \
MHD_THREAD_STACK_NULL,            1035),   /**< Null stack pointer passed when non null was reqired */   \
MHD_THREAD_DELETE_FAIL,           1036),   /**< Error deleting a thread */   \
MHD_SLEEP_ERROR,                  1037),   /**< Error sleeping a thread */ \
MHD_BUFFER_ALLOC_FAIL,            1038),   /**< Failed to allocate a packet buffer */ \
MHD_NO_PACKET_TO_RECEIVE,         1039),   /**< No Packets waiting to be received */ \
MHD_INTERFACE_NOT_UP,             1040),   /**< Requested interface is not active */ \
MHD_DELAY_TOO_LONG,               1041),   /**< Requested delay is too long */ \
MHD_INVALID_DUTY_CYCLE,           1042),   /**< Duty cycle is outside limit 0 to 100 */ \
MHD_PMK_WRONG_LENGTH,             1043),   /**< Returned pmk was the wrong length */ \
MHD_UNKNOWN_SECURITY_TYPE,        1044),   /**< AP security type was unknown */ \
MHD_WEP_NOT_ALLOWED,              1045),   /**< AP not allowed to use WEP - it is not secure - use Open instead */ \
MHD_WPA_KEYLEN_BAD,               1046),   /**< WPA / WPA2 key length must be between 8 & 64 bytes */ \
MHD_FILTER_NOT_FOUND,             1047),   /**< Specified filter id not found */ \
MHD_SPI_ID_READ_FAIL,             1048),   /**< Failed to read 0xfeedbead SPI id from chip */ \
MHD_SPI_SIZE_MISMATCH,            1049),   /**< Mismatch in sizes between SPI header and SDPCM header */ \
MHD_ADDRESS_ALREADY_REGISTERED,   1050),   /**< Attempt to register a multicast address twice */ \
MHD_SDIO_RETRIES_EXCEEDED,        1051),   /**< SDIO transfer failed too many times. */ \
MHD_NULL_PTR_ARG,                 1052),   /**< Null Pointer argument passed to function. */ \
MHD_THREAD_FINISH_FAIL,           1053),   /**< Error deleting a thread */ \
MHD_WAIT_ABORTED,                 1054),   /**< Semaphore/mutex wait has been aborted */ \
MHD_SET_BLOCK_ACK_WINDOW_FAIL,    1055),   /**< Failed to set block ack window */ \
MHD_DELAY_TOO_SHORT,              1056),   /**< Requested delay is too short */ \
MHD_INVALID_INTERFACE,            1057),   /**< Invalid interface provided */ \
MHD_WEP_KEYLEN_BAD,               1058),   /**< WEP / WEP_SHARED key length must be 5 or 13 bytes */ \
MHD_HANDLER_ALREADY_REGISTERED,   1059),   /**< EAPOL handler already registered */ \
MHD_AP_ALREADY_UP,                1060),   /**< Soft AP or P2P group owner already up */ \
MHD_EAPOL_KEY_PACKET_M1_TIMEOUT,  1061),   /**< Timeout occurred while waiting for EAPOL packet M1 from AP */ \
MHD_EAPOL_KEY_PACKET_M3_TIMEOUT,  1062),   /**< Timeout occurred while waiting for EAPOL packet M3 from AP, which may indicate incorrect WPA2
MHD_EAPOL_KEY_PACKET_G1_TIMEOUT,  1063),   /**< Timeout occurred while waiting for EAPOL packet G1 from AP */ \
MHD_EAPOL_KEY_FAILURE,            1064),   /**< Unknown failure occurred during the EAPOL key handshake */ \
MHD_MALLOC_FAILURE,               1065),   /**< Memory allocation failure */ \
MHD_ACCESS_POINT_NOT_FOUND,       1066),   /**< Access point not found */


MHD_WLAN_ERROR,                       2001),  /**< Generic Error */                     \
MHD_WLAN_BADARG,                      2002),  /**< Bad Argument */                      \
MHD_WLAN_BADOPTION,                   2003),  /**< Bad option */                        \
MHD_WLAN_NOTUP,                       2004),  /**< Not up */                            \
MHD_WLAN_NOTDOWN,                     2005),  /**< Not down */                          \
MHD_WLAN_NOTAP,                       2006),  /**< Not AP */                            \
MHD_WLAN_NOTSTA,                      2007),  /**< Not STA  */                          \
MHD_WLAN_BADKEYIDX,                   2008),  /**< BAD Key Index */                     \
MHD_WLAN_RADIOOFF,                    2009),  /**< Radio Off */                         \
MHD_WLAN_NOTBANDLOCKED,               2010),  /**< Not  band locked */                  \
MHD_WLAN_NOCLK,                       2011),  /**< No Clock */                          \
MHD_WLAN_BADRATESET,                  2012),  /**< BAD Rate valueset */                 \
MHD_WLAN_BADBAND,                     2013),  /**< BAD Band */                          \
MHD_WLAN_BUFTOOSHORT,                 2014),  /**< Buffer too short */                  \
MHD_WLAN_BUFTOOLONG,                  2015),  /**< Buffer too long */                   \
MHD_WLAN_BUSY,                        2016),  /**< Busy */                              \
MHD_WLAN_NOTASSOCIATED,               2017),  /**< Not Associated */                    \
MHD_WLAN_BADSSIDLEN,                  2018),  /**< Bad SSID len */                      \
MHD_WLAN_OUTOFRANGECHAN,              2019),  /**< Out of Range Channel */              \
MHD_WLAN_BADCHAN,                     2020),  /**< Bad Channel */                       \
MHD_WLAN_BADADDR,                     2021),  /**< Bad Address */                       \
MHD_WLAN_NORESOURCE,                  2022),  /**< Not Enough Resources */              \
MHD_WLAN_UNSUPPORTED,                 2023),  /**< Unsupported */                       \
MHD_WLAN_BADLEN,                      2024),  /**< Bad length */                        \
MHD_WLAN_NOTREADY,                    2025),  /**< Not Ready */                         \
MHD_WLAN_EPERM,                       2026),  /**< Not Permitted */                     \
MHD_WLAN_NOMEM,                       2027),  /**< No Memory */                         \
MHD_WLAN_ASSOCIATED,                  2028),  /**< Associated */                        \
MHD_WLAN_RANGE,                       2029),  /**< Not In Range */                      \
MHD_WLAN_NOTFOUND,                    2030),  /**< Not Found */                         \
MHD_WLAN_WME_NOT_ENABLED,             2031),  /**< WME Not Enabled */                   \
MHD_WLAN_TSPEC_NOTFOUND,              2032),  /**< TSPEC Not Found */                   \
MHD_WLAN_ACM_NOTSUPPORTED,            2033),  /**< ACM Not Supported */                 \
MHD_WLAN_NOT_WME_ASSOCIATION,         2034),  /**< Not WME Association */               \
MHD_WLAN_SDIO_ERROR,                  2035),  /**< SDIO Bus Error */                    \
MHD_WLAN_WLAN_DOWN,                   2036),  /**< WLAN Not Accessible */               \
MHD_WLAN_BAD_VERSION,                 2037),  /**< Incorrect version */                 \
MHD_WLAN_TXFAIL,                      2038),  /**< TX failure */                        \
MHD_WLAN_RXFAIL,                      2039),  /**< RX failure */                        \
MHD_WLAN_NODEVICE,                    2040),  /**< Device not present */                \
MHD_WLAN_UNFINISHED,                  2041),  /**< To be finished */                    \
MHD_WLAN_NONRESIDENT,                 2042),  /**< access to nonresident overlay */     \
MHD_WLAN_DISABLED,                    2043),  /**< Disabled in this build */
#endif
