/**
 * Copyright (c) 2026 Synaptics Incorporated
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file syna_wifi_zephyr.c
 * Zephyr wifi related linking with synaptics.
 */

#ifdef ZEPHYR_OS
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_nm.h>

#include "includes.h"
#include "drivers/driver_zephyr.h"
#include "driver_syna_platform.h"
#include "supplicant_mhd_transport_zephyr.h"
#include "syna_wifi.h"

LOG_MODULE_REGISTER(driver_syna_zephyr, CONFIG_WIFI_LOG_LEVEL);

#define WIFI_PRIMARY_IF MHD_STA_INTERFACE
#define MHD_STARTUP_TIMEOUT_MS 100000U
typedef struct net_buf *host_buffer_t;

typedef enum {
	HOST_NETWORK_TX,
	HOST_NETWORK_RX
} host_buffer_dir_t;

struct syna_zephyr_device_data
{
	struct net_if *iface;
	u8 mac_addr[ETH_ALEN];
};

extern mhd_bool_t mhd_interface_is_ready_to_transceive(mhd_ifidx_t ifidx);
extern void mhd_network_send_ethernet_data(host_buffer_t buffer, mhd_ifidx_t interface);
extern mhd_result_t host_buffer_get(host_buffer_t *buffer, host_buffer_dir_t direction,
				    unsigned short size, mhd_bool_t wait);
extern void host_buffer_release(host_buffer_t buffer, host_buffer_dir_t direction);
extern uint8_t *host_buffer_get_current_piece_data_pointer(host_buffer_t buffer);
extern size_t host_buffer_head_room_size(void);
extern mhd_result_t mhd_wifi_get_rate(mhd_ifidx_t interface, uint32_t *rate);

int drv_syna_store_platform_init(driver_syna_data_t *drv,
				 const void *platform_data)
{
	const struct zep_wpa_supp_dev_callbk_fns *cbs = platform_data;

	if (!cbs)
		return -EINVAL;

	os_memcpy(&drv->cbs, cbs, sizeof(drv->cbs));
	return 0;
}

void *driver_syna_zephyr_init(void *ctx, const char *ifname,
		       struct zep_wpa_supp_dev_callbk_fns *cbs)
{
	return driver_syna_init_common(ctx, ifname, cbs);
}

int drv_syna_register_transport(driver_syna_data_t *drv)
{
	return sup_adap_register_mhd_event_handler(drv, drv->mhd_int);
}

int drv_syna_post_init(driver_syna_data_t *drv)
{
	ARG_UNUSED(drv);
	return 0;
}

void drv_syna_pre_deinit(driver_syna_data_t *drv)
{
	(void)sup_adap_unregister_mhd_event_handler(drv, drv->mhd_int);
}

void drv_syna_notify_scan_started(driver_syna_data_t *drv)
{
	if (drv->cbs.scan_start)
		drv->cbs.scan_start(drv->ctx);
}

void drv_syna_prepare_assoc_bandwidth(struct wpa_supplicant *wpa_s,
					      const u8 *target_bssid,
					      struct wpa_driver_associate_params *params)
{
	ARG_UNUSED(wpa_s);
	ARG_UNUSED(target_bssid);
	ARG_UNUSED(params);
}

int drv_syna_validate_key_install(int deleting, int is_pairwise,
					  struct wpa_driver_set_key_params *params)
{
	ARG_UNUSED(deleting);
	ARG_UNUSED(is_pairwise);
	ARG_UNUSED(params);
	return 0;
}

static int zep_syna_get_scan_results(void *priv)
{
	driver_syna_data_t *drv = priv;
	struct wpa_scan_results *results = drv_syna_get_scan_results(priv);
	size_t index;

	if (!results)
		return -ENOMEM;

	if (results->num == 0) {
		wpa_printf(MSG_INFO,
			   "DRV_SYNA: zep_syna_get_scan_results: zero results, completing scan fetch");
		if (drv->ctx) {
			struct zep_drv_if_ctx *if_ctx = drv->ctx;

			if_ctx->scan_res2_get_in_prog = false;
			k_sem_give(&if_ctx->drv_resp_sem);
		}
	}

	for (index = 0; index < results->num; index++)
	{
		if (drv->cbs.scan_res)
			drv->cbs.scan_res(drv->ctx, results->res[index], index + 1 < results->num);
		os_free(results->res[index]);
	}
	os_free(results->res);
	os_free(results);
	return 0;
}

static int zep_syna_set_key(void *priv, const unsigned char *ifname,
							enum wpa_alg alg, const unsigned char *addr,
							int key_idx, int set_tx, const unsigned char *seq,
							size_t seq_len, const unsigned char *key,
							size_t key_len, enum key_flag key_flag)
{
	struct wpa_driver_set_key_params params = {
		.ifname = ifname,
		.alg = alg,
		.addr = addr,
		.key_idx = key_idx,
		.set_tx = set_tx,
		.seq = seq,
		.seq_len = seq_len,
		.key = key,
		.key_len = key_len,
		.key_flag = key_flag,
	};
	return drv_syna_set_key(priv, &params);
}

static int zep_syna_deauthenticate(void *priv, const char *addr,
								   unsigned short reason_code)
{
	return drv_syna_deauth(priv, (const u8 *)addr, reason_code);
}

static int zep_syna_set_supp_port(void *priv, int authorized, char *bssid)
{
	driver_syna_data_t *drv = priv;
	if (bssid)
		os_memcpy(drv->bssid, bssid, ETH_ALEN);
	return drv_syna_set_supp_port(priv, authorized);
}

static int zep_syna_signal_poll(void *priv, struct wpa_signal_info *si,
				unsigned char *bssid)
{
	int32_t rssi;
	uint32_t rate;
	mhd_result_t result;

	ARG_UNUSED(bssid);
	if (!priv || !si)
		return -EINVAL;

	result = mhd_wifi_get_rssi(WIFI_PRIMARY_IF, &rssi);
	if (result != MHD_SUCCESS)
		return -EIO;

	os_memset(si, 0, sizeof(*si));
	si->data.signal = rssi;
	si->data.avg_signal = (s8)rssi;
	si->data.avg_beacon_signal = (s8)rssi;
	si->current_noise = WPA_INVALID_NOISE;
	si->frequency = ((driver_syna_data_t *)priv)->assoc_freq;
	if (mhd_wifi_get_rate(((driver_syna_data_t *)priv)->mhd_int, &rate) == MHD_SUCCESS)
		si->data.current_tx_rate = rate * 500U;
	return 0;
}

static int zep_syna_get_conn_info(void *priv, struct wpa_conn_info *info)
{
	uint8_t ie_buf[1536];
	mhd_wifi_bss_info_update_t bss_info;
	mhd_result_t result;
	uint32_t offset;

	ARG_UNUSED(priv);
	if (!info)
		return -EINVAL;

	os_memset(info, 0, sizeof(*info));
	os_memset(&bss_info, 0, sizeof(bss_info));
	bss_info.ies = ie_buf;
	bss_info.ies_buf_len = sizeof(ie_buf);
	result = mhd_wifi_api_update_bss_info(WIFI_PRIMARY_IF, &bss_info);
	if (result != MHD_SUCCESS)
		return -EIO;

	info->beacon_interval = bss_info.beacon_period;
	for (offset = 0; offset + 2U <= bss_info.ies_len;) {
		uint8_t element_id = ie_buf[offset];
		uint8_t element_len = ie_buf[offset + 1U];

		if (offset + 2U + element_len > bss_info.ies_len)
			break;
		if (element_id == WLAN_EID_TIM && element_len >= 2U) {
			info->dtim_period = ie_buf[offset + 3U];
			break;
		}
		offset += 2U + element_len;
	}

	return 0;
}

static int zep_syna_authenticate(void *priv, struct wpa_driver_auth_params *params,
								 struct wpa_bss *curr_bss)
{
	/* SAE uses firmware JOIN_START / EVENT_EXTERNAL_AUTH, not this OPEN path. */
	driver_syna_data_t *drv = priv;
	union wpa_event_data event;
	ARG_UNUSED(curr_bss);
	os_memset(&event, 0, sizeof(event));
	os_memcpy(event.auth.peer, params->bssid, ETH_ALEN);
	os_memcpy(event.auth.bssid, params->bssid, ETH_ALEN);
	event.auth.auth_type = WLAN_AUTH_OPEN;
	event.auth.auth_transaction = 2;
	event.auth.status_code = WLAN_STATUS_SUCCESS;
	if (drv->cbs.auth_resp)
		drv->cbs.auth_resp(drv->ctx, &event);
	return 0;
}

static int zep_syna_send_mlme(void *priv, const u8 *data, size_t data_len,
			      int noack, unsigned int freq, int no_cck,
			      int offchanok, unsigned int wait_time, int cookie)
{
	ARG_UNUSED(no_cck);
	ARG_UNUSED(offchanok);
	ARG_UNUSED(cookie);
	return drv_syna_send_mlme(priv, data, data_len, noack, freq, NULL, 0,
				  0, wait_time, -1);
}

static int zep_syna_register_frame(void *priv, u16 type, const u8 *match,
								   size_t match_len, bool multicast)
{
	ARG_UNUSED(priv);
	ARG_UNUSED(type);
	ARG_UNUSED(match);
	ARG_UNUSED(match_len);
	ARG_UNUSED(multicast);
	return 0;
}

static void *zep_syna_hapd_init(void *ctx, const char *ifname,
								struct zep_hostapd_dev_callbk_fns *cbs)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(ifname);
	ARG_UNUSED(cbs);
	return NULL;
}

static int zep_syna_not_supported(void *priv)
{
	ARG_UNUSED(priv);
	return -ENOTSUP;
}

static const struct zep_wpa_supp_dev_ops syna_wifi_drv_ops = {
	.init = driver_syna_zephyr_init,
	.deinit = driver_syna_deinit,
	.scan2 = drv_syna_scan_req,
	.get_scan_results2 = zep_syna_get_scan_results,
	.deauthenticate = zep_syna_deauthenticate,
	.authenticate = zep_syna_authenticate,
	.associate = drv_syna_assoc,
	.signal_poll = zep_syna_signal_poll,
	.get_conn_info = zep_syna_get_conn_info,
	.send_mlme = zep_syna_send_mlme,
	.send_external_auth_status = drv_syna_sta_external_auth,
	.set_key = zep_syna_set_key,
	.set_supp_port = zep_syna_set_supp_port,
	.register_frame = zep_syna_register_frame,
	.get_capa = drv_syna_get_capa,
	.hapd_init = zep_syna_hapd_init,
	.stop_ap = zep_syna_not_supported,
};

static int syna_wifi_version(const struct device *dev, struct wifi_version *version)
{
	ARG_UNUSED(dev);
	version->drv_version = "1.0";
	version->fw_version = "1.0";
	return 0;
}

static int syna_wifi_scan(const struct device *dev, struct wifi_scan_params *params,
			  scan_result_cb_t cb)
{
	struct syna_zephyr_device_data *data = dev->data;

	return syna_adapt_zephyr_scan(data->iface, params, cb);
}

static const struct wifi_mgmt_ops syna_wifi_mgmt_ops = {
	.scan = syna_wifi_scan,
	.get_version = syna_wifi_version,
};

static void syna_wifi_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct syna_zephyr_device_data *data = dev->data;
	struct ethernet_context *eth_ctx = net_if_l2_data(iface);
	data->iface = iface;
	eth_ctx->eth_if_type = L2_ETH_IF_TYPE_WIFI;
	wifi_nm_register_mgd_type_iface(wifi_nm_get_instance("wifi_supplicant"),
									WIFI_TYPE_STA, iface);
	net_if_set_link_addr(iface, data->mac_addr, sizeof(data->mac_addr), NET_LINK_ETHERNET);
	mhd_wifi_api_bind_netif(WIFI_PRIMARY_IF, iface);
	ethernet_init(iface);
	LOG_INF("SYNA Wi-Fi iface initialized, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
		data->mac_addr[0], data->mac_addr[1], data->mac_addr[2],
		data->mac_addr[3], data->mac_addr[4], data->mac_addr[5]);
}

static int syna_wifi_get_config(const struct device *dev, enum ethernet_config_type type,
								struct ethernet_config *config)
{
	struct syna_zephyr_device_data *data = dev->data;
	if (type == ETHERNET_CONFIG_TYPE_MAC_ADDRESS)
	{
		memcpy(config->mac_address.addr, data->mac_addr, sizeof(data->mac_addr));
		return 0;
	}
	if (type == ETHERNET_CONFIG_TYPE_EXTRA_TX_PKT_HEADROOM)
	{
		config->extra_tx_pkt_headroom = host_buffer_head_room_size();
		return 0;
	}
	return -ENOTSUP;
}

static int syna_wifi_set_config(const struct device *dev, enum ethernet_config_type type,
								const struct ethernet_config *config)
{
	struct syna_zephyr_device_data *data = dev->data;
	if (type != ETHERNET_CONFIG_TYPE_MAC_ADDRESS)
		return -ENOTSUP;
	memcpy(data->mac_addr, config->mac_address.addr, sizeof(data->mac_addr));
	return 0;
}

static int syna_wifi_send(const struct device *dev, struct net_pkt *pkt)
{
	host_buffer_t buffer;
	size_t packet_length;
	mhd_result_t result;

	ARG_UNUSED(dev);
	if (!mhd_interface_is_ready_to_transceive(WIFI_PRIMARY_IF))
		return -ENETDOWN;

	packet_length = net_pkt_get_len(pkt);
	if (packet_length == 0 || packet_length > UINT16_MAX)
		return -EINVAL;

	result = host_buffer_get(&buffer, HOST_NETWORK_TX,
				 (unsigned short)packet_length, MHD_FALSE);
	if (result != MHD_SUCCESS)
		return -ENOBUFS;

	net_pkt_cursor_init(pkt);
	if (net_pkt_read(pkt, host_buffer_get_current_piece_data_pointer(buffer),
			 packet_length) < 0) {
		host_buffer_release(buffer, HOST_NETWORK_TX);
		return -EIO;
	}

	mhd_network_send_ethernet_data(buffer, WIFI_PRIMARY_IF);
	return 0;
}

static const struct net_wifi_mgmt_offload syna_wifi_api = {
	.wifi_iface.iface_api.init = syna_wifi_iface_init,
	.wifi_iface.set_config = syna_wifi_set_config,
	.wifi_iface.get_config = syna_wifi_get_config,
	.wifi_iface.send = syna_wifi_send,
	.wifi_mgmt_api = &syna_wifi_mgmt_ops,
	.wifi_drv_ops = &syna_wifi_drv_ops,
};

static struct syna_zephyr_device_data syna_wifi_data;

static int syna_wifi_init(const struct device *dev)
{
	struct syna_zephyr_device_data *data = dev->data;
	mhd_init_status_t status;
	mhd_result_t result = mhd_wifi_api_wifi_on();
	if (result != MHD_SUCCESS) {
		LOG_ERR("MHD startup dispatch failed: %d", result);
		return result;
	}
	result = mhd_wifi_api_wait_for_ready(&status, MHD_STARTUP_TIMEOUT_MS);
	if (result != MHD_SUCCESS || status != MHD_INIT_SUCCESS) {
		LOG_ERR("MHD startup did not complete: result=%d status=%d", result, status);
		return -EIO;
	}
	return mhd_wifi_api_get_mac_address(WIFI_PRIMARY_IF, (mhd_mac_t *)data->mac_addr);
}

NET_DEVICE_INIT_INSTANCE(WIFI_PRIMARY_IF, "wlan0", 0, syna_wifi_init, NULL,
						 &syna_wifi_data, NULL, CONFIG_WIFI_INIT_PRIORITY, &syna_wifi_api,
						 ETHERNET_L2, NET_L2_GET_CTX_TYPE(ETHERNET_L2), NET_ETH_MTU);
#endif
