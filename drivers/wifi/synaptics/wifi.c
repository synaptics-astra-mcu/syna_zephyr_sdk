/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi_hal.h"

syna_wifi_data_t syna_wifi_data;
syna_wifi_data_t syna_wifi_data_ap;

/********* wifi net_if for zephyr *********/
void host_network_process_buffer(host_buffer_t buffer)
{
	host_network_process_ethernet_data(buffer, 0);
}

void host_network_process_ethernet_data(host_buffer_t buffer, int interface)
{
	syna_wifi_data_t *dev_data = &syna_wifi_data;
	uint8_t *data = host_buffer_get_current_piece_data_pointer(buffer);
	uint32_t len = host_buffer_get_current_piece_size(buffer);
	struct net_pkt *pkt;
	int ret = 0;

	if (interface == MHD_AP_INTERFACE) {
		dev_data = &syna_wifi_data_ap;
	}

	if (dev_data->state != WIFI_STATE_COMPLETED) {
		rtos_printf("%s err state %d\n", __func__, dev_data->state);
		host_buffer_release(buffer, 0);
		return;
	}

	if (len > HOST_LINK_MTU + 14) {
		rtos_printf("%s, len err %d\n", __func__, len);
		host_buffer_release(buffer, 0);
		return;
	}

	/* check iface NET_IF_UP and NET_IF_LOWER_UP */
	if ((dev_data->iface != NULL) && net_if_flag_is_set(dev_data->iface, NET_IF_UP)
		&& net_if_is_carrier_ok(dev_data->iface)) {
		pkt = net_pkt_rx_alloc_with_buffer(dev_data->iface, len, AF_UNSPEC, 0, K_NO_WAIT);

		if (pkt != NULL) {
			ret = net_pkt_write(pkt, data, len);
			if (ret < 0) {
				net_pkt_unref(pkt);
				rtos_printf("%s, len %d, write err  %d\n", __func__, len, ret);
			} else {
				ret = net_recv_data(dev_data->iface, pkt);

				if (ret < 0) {
					net_pkt_unref(pkt);
					rtos_printf("%s, len %d, recv err %d\n", __func__, len, ret);
				}
			}
		} else {
			rtos_printf("%s, len %d, alloc err \n", __func__, len);
		}
	}

	/* Release */
	host_buffer_release(buffer, 0);
}

static int syna_mgmt_scan(const struct device *dev, struct wifi_scan_params *params,
			scan_result_cb_t cb)
{
	static mhd_ap_info_t res[30], *record = NULL;
	struct wifi_scan_result bss = {0};
	int num = sizeof(res)/sizeof(mhd_ap_info_t), k = 0;
	syna_wifi_data_t *data = dev->data;

	ARG_UNUSED(dev);
	ARG_UNUSED(params);
	ARG_UNUSED(cb);

	if (!syna_wifi_data.mhd_init) {
		return -EOPNOTSUPP;
	}

	rtos_printf("%s\n", __func__);

	/* check iface NET_IF_UP and NET_IF_LOWER_UP */
	if ((data->iface != NULL) && net_if_flag_is_set(data->iface, NET_IF_UP)
		&& net_if_is_carrier_ok(data->iface)) {
		mhd_start_scan();
	} else {
		return -EOPNOTSUPP;
	}
	/* reduce count to avoid function size too big */

	mhd_get_scan_results(res, &num);
	rtos_printf("%s, %d: %d\n", __func__, __LINE__, num);

	record = &res[0];
	for (k = 0; k < num; k++) {
	//	rtos_printf("ap list %d\n", k+1);
	//	rtos_printf(" SSID    : %s\n", record->ssid);
	//	rtos_printf(" RSSI    : %ddBm \n", record->rssi);
	//	rtos_printf(" Security: %d\n", record->security);
	//	rtos_printf(" Channel : %d\n", record->channel);
	//	rtos_printf(" band    : %d\n", record->band);
	//	rtos_printf(" BSSID   : %02X:%02X:%02X:%02X:%02X:%02X\n\n",
	//		record->bssid[0], record->bssid[1], record->bssid[2],
	//		record->bssid[3], record->bssid[4], record->bssid[5]);

		if (cb) {
			memset(&bss, 0, sizeof(bss));
			memcpy(bss.ssid, record->ssid, strlen(record->ssid));
			bss.ssid_length = strlen(record->ssid);
			bss.channel = record->channel;
			bss.band = record->band;
			bss.security = record->security;
			bss.band = bss.channel > 14 ? WIFI_FREQ_BAND_5_GHZ : WIFI_FREQ_BAND_2_4_GHZ;

			bss.security = WIFI_SECURITY_TYPE_NONE;
			switch (record->security) {
				case MHD_SECURE_OPEN:
					bss.security = WIFI_SECURITY_TYPE_NONE;
					break;
				case MHD_WPA_PSK_AES:
				case MHD_WPA2_PSK_AES:
				case MHD_WPA_PSK_TKIP:
				case MHD_WPA_PSK_MIXED:
				case MHD_WPA2_PSK_MIXED:
					bss.security = WIFI_SECURITY_TYPE_PSK;
					break;
				case MHD_WPA2_PSK_SHA256:
					bss.security = WIFI_SECURITY_TYPE_PSK_SHA256;
					break;
				case MHD_WPA3_PSK_SAE:
					bss.security = WIFI_SECURITY_TYPE_SAE;
					break;
				case MHD_WPA_ENT_AES:
				case MHD_WPA_ENT_TKIP:
				case MHD_WPA_ENT_MIXED:
				case MHD_WPA2_ENT_AES:
				case MHD_WPA2_ENT_TKIP:
				case MHD_WPA2_ENT_MIXED:
					bss.security = WIFI_SECURITY_TYPE_EAP;
					break;
			}

			bss.rssi = record->rssi;
			bss.mac_length = WIFI_MAC_ADDR_LEN;
			memcpy(bss.mac, record->bssid, WIFI_MAC_ADDR_LEN);
			cb(data->iface, 0, &bss);
		}
		record++;
	}
	cb(data->iface, 0, NULL);
	return 0;
}

static int syna_mgmt_connect(const struct device *dev, struct wifi_connect_req_params *params)
{
	char *psk = NULL;
	syna_wifi_data_t *data = dev->data;
	int ret;

	if (!syna_wifi_data.mhd_init) {
		return -EOPNOTSUPP;
	}

	/* check iface NET_IF_UP and NET_IF_LOWER_UP */
	if ((data->iface != NULL) && net_if_flag_is_set(data->iface, NET_IF_UP)
		&& net_if_is_carrier_ok(data->iface)) {
		if (!params)
			return -EINVAL;
	} else {
		return -EOPNOTSUPP;
	}

	if (data->state == WIFI_STATE_COMPLETED) {
		rtos_printf("%s, call leave ap first\n", __func__);
		return -EOPNOTSUPP;
	}
	psk = params->psk ? params->psk: (params->sae_password ? params->sae_password : NULL);

	rtos_printf("%s\n", __func__);
	ret = mhd_join_ap(params->ssid, psk);

	if (ret == 0) {
		data->state = WIFI_STATE_COMPLETED;
	}
	wifi_mgmt_raise_connect_result_event(data->iface, ret ? WIFI_STATUS_CONN_FAIL : 0);

	return ret;
}

static int syna_mgmt_disconnect(const struct device *dev)
{
	syna_wifi_data_t *data = dev->data;

	if (!syna_wifi_data.mhd_init) {
		return -EOPNOTSUPP;
	}

	data->state = WIFI_STATE_DISCONNECTED;

	wifi_mgmt_raise_disconnect_result_event(data->iface, 0);
	rtos_printf("%s\n", __func__);
	return mhd_leave_ap();
}

#if defined(CONFIG_NET_STATISTICS_WIFI)
static int syna_mgmt_wifi_stats(const struct device *dev, struct net_stats_wifi *stats)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(stats);

	if (!syna_wifi_data.mhd_init) {
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}
#endif

int syna_sta_status(const struct device *dev, struct wifi_iface_status *status)
{
	char ssid[32] = {0}, mac[6] = {0};
	uint32_t ch;
	int rssi, rate, sec;

	if (!syna_wifi_data.mhd_init) {
		rtos_printf("wifi not up\n");
		return -EOPNOTSUPP;
	}

	memset(ssid, 0, sizeof(ssid));
	memset(mac, 0, sizeof(mac));
	mhd_sta_get_ssid(ssid);
	mhd_sta_get_bssid(mac);

	memset(status->ssid, 0, WIFI_SSID_MAX_LEN);
	memset(status->bssid, 0, WIFI_MAC_ADDR_LEN);
	status->iface_mode = WIFI_MODE_INFRA;

	if (strlen(ssid) && (mac[0] + mac[1] + mac[2])) {
		mhd_wifi_get_channel(MHD_STA_INTERFACE, &ch);
		rate = mhd_sta_get_rate();
		rssi = mhd_sta_get_rssi();
		mhd_wifi_get_security(MHD_STA_INTERFACE, &sec);

		rtos_printf("sta connection info ......\n");
		rtos_printf("ssid : %s\n", ssid);
		rtos_printf("bssid: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		rtos_printf("ch   : %d\n", ch);
		rtos_printf("sec  : %08x\n", sec);
		rtos_printf("rate : %d\n", rate/2);
		rtos_printf("rssi : %ddbm\n\n", rssi);

		memcpy(status->ssid, ssid, strlen(ssid));
		memcpy(status->bssid, mac, WIFI_MAC_ADDR_LEN);
		status->channel = ch;
		status->rssi = rssi;
		status->band = ch >= 36 ? WIFI_FREQ_BAND_5_GHZ : WIFI_FREQ_BAND_2_4_GHZ;

		switch (sec) {
			case 0:
				status->security = WIFI_SECURITY_TYPE_NONE;
			break;
			case 0x80:
			case 0x84:
				status->security = WIFI_SECURITY_TYPE_PSK;
			break;
			case 0x40000:
				status->security = WIFI_SECURITY_TYPE_SAE;
			break;
		}

		status->state = WIFI_STATE_COMPLETED;
	} else {
		status->state = WIFI_STATE_DISCONNECTED;
		rtos_printf("sta not connected\n");
	}

	return 0;
}

static int syna_mgmt_send(const struct device *dev, struct net_pkt *pkt)
{
	syna_wifi_data_t *dev_data = dev->data;
	host_buffer_t *buf = NULL;
	int res;
	size_t pkt_len;
	int ret;

	if (!syna_wifi_data.mhd_init) {
		return -EOPNOTSUPP;
	}

	if (dev_data->state != WIFI_STATE_COMPLETED) {
		rtos_printf("%s err state %d\n", __func__, dev_data->state);
		return -EOPNOTSUPP;
	}

	pkt_len = net_pkt_get_len(pkt);

	if (pkt_len > HOST_LINK_MTU + 14) {
		rtos_printf("%s, len err %d\n", __func__, pkt_len);
		return -EIO;
	}

	res = host_buffer_get((host_buffer_t *)&buf, 0, pkt_len + 128, 0);
	if ((res != 0) || (buf == NULL)) {
		rtos_printf("%s, %d\n", __func__, __LINE__);
		return -EIO;
	}
	host_buffer_add_remove_at_front(&buf, 128);

	ret = net_pkt_read(pkt, host_buffer_get_current_piece_data_pointer(buf), pkt_len);
	if (ret < 0) {
		host_buffer_release(buf, 0);
		rtos_printf("%s, %d\n", __func__, __LINE__);
		return -EIO;
	}

	return mhd_network_send_ethernet_data(buf, MHD_STA_INTERFACE);
}

static enum ethernet_hw_caps syna_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (!syna_wifi_data.mhd_init) {
		return -EOPNOTSUPP;
	}

	return 0;
}

static int syna_set_config(const struct device *dev, enum ethernet_config_type type,
			const struct ethernet_config *config)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(type);
	ARG_UNUSED(config);

	if (!syna_wifi_data.mhd_init) {
		return -EOPNOTSUPP;
	}

	rtos_printf("%s\n", __func__);
	return -ENOTSUP;
}

int wifi_up(void)
{
	syna_wifi_data_t *data = &syna_wifi_data;
	struct net_if *iface = data->iface;
	uint8_t mac_addr[6];

	if (!iface) {
		rtos_printf("iface is null\n");
		return -EINVAL;
	}

	if (mhd_module_init() == 0) {
		rtos_printf("load wifi driver success\n");
	} else {
		rtos_printf("load wifi driver fail\n");
		return -EIO;
	}

	if (mhd_wifi_get_mac_address(&mac_addr[0], 0) == 0) {
		if (net_if_set_link_addr(iface, mac_addr, 6, NET_LINK_ETHERNET)) {
			rtos_printf("Failed to set link addr");
		}
	}

	//mhd_show_versions();
	net_if_carrier_on(iface);
	syna_wifi_data.mhd_init = 1;

	rtos_printf("%s, %d\n", __func__, __LINE__);
	return 0;
}

static void syna_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	syna_wifi_data_t *data = dev->data;
	struct ethernet_context *eth_ctx = net_if_l2_data(iface);

	eth_ctx->eth_if_type = L2_ETH_IF_TYPE_WIFI;

	data->iface = iface;

	/* TODO: See if we need to make this depend on dts / Kconfig */
	net_if_flag_set(iface, NET_IF_NO_AUTO_START);

	/* Initialize Ethernet L2 stack */
	ethernet_init(iface);

	/* Not currently connected to a network */
	net_if_dormant_on(iface);

	/* L1 network layer (physical layer) is up */
	net_if_carrier_off(iface);

	rtos_printf("%s, %d, %p, %p\n", __func__, __LINE__, iface, data->iface);
}

static int syna_mgmt_iface_start(const struct device *dev)
{
	syna_wifi_data_t *data = dev->data;
	struct net_if *iface = data->iface;

	rtos_printf("%s, %d\n", __func__, __LINE__);

	if (!iface) {
		rtos_printf("iface is null\n");
		return -EINVAL;
	}

	if (syna_wifi_data.mhd_init == 0) {
		if (sdio_init() == 0) {
			if (wifi_up() != 0) {
				rtos_printf("wifiup fail\n");
				return -EIO;
			}
		} else {
			rtos_printf("wifi sdio init fail\n");
			return -EIO;
		}
	}

	net_if_carrier_on(iface);
	net_if_dormant_off(iface);
	net_if_set_default(iface);
	return 0;
}

static int syna_mgmt_iface_stop(const struct device *dev)
{
	syna_wifi_data_t *data = dev->data;
	struct net_if *iface = data->iface;

	if (!iface) {
		rtos_printf("iface is null\n");
		return -EINVAL;
	}

	net_if_dormant_on(iface);
	net_if_carrier_off(iface);
	rtos_printf("%s, %d\n", __func__, __LINE__);

	return 0;
}

static int syna_init(const struct device *dev)
{
	rtos_printf("%s, %d, %s:%s\n", __func__, __LINE__, __DATE__, __TIME__);

	syna_wifi_data.mhd_init = 0;
	return 0;
}

static const struct wifi_mgmt_ops syna_wifi_mgmt = {
	.scan = syna_mgmt_scan,
	.connect = syna_mgmt_connect,
	.disconnect = syna_mgmt_disconnect,
	//.set_power_save = syna_mgmt_set_power_save,
	.iface_status = syna_sta_status,
#if defined(CONFIG_NET_STATISTICS_WIFI)
	.get_stats = syna_mgmt_wifi_stats,
#endif
};

static const struct net_wifi_mgmt_offload syna_api = {
	.wifi_iface.iface_api.init = syna_iface_init,
	.wifi_iface.start = syna_mgmt_iface_start,
	.wifi_iface.stop = syna_mgmt_iface_stop,
	.wifi_iface.send = syna_mgmt_send,
	.wifi_iface.get_capabilities = syna_get_capabilities,
	.wifi_iface.set_config = syna_set_config,
	.wifi_mgmt_api = &syna_wifi_mgmt,
};


/* mhd ap mode interface and config */
static void syna_iface_init_ap(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	syna_wifi_data_t *data = dev->data;
	struct ethernet_context *eth_ctx = net_if_l2_data(iface);

	eth_ctx->eth_if_type = L2_ETH_IF_TYPE_WIFI;

	data->iface = iface;

	/* TODO: See if we need to make this depend on dts / Kconfig */
	net_if_flag_set(iface, NET_IF_NO_AUTO_START);

	/* Initialize Ethernet L2 stack */
	ethernet_init(iface);

	/* Not currently connected to a network */
	net_if_dormant_on(iface);

	/* L1 network layer (physical layer) is up */
	net_if_carrier_off(iface);
	rtos_printf("%s, %d, %p, %p\n", __func__, __LINE__, iface, data->iface);
}

static int syna_mgmt_iface_start_ap(const struct device *dev)
{
	syna_wifi_data_t *data = dev->data;
	struct net_if *iface = data->iface;
	uint8_t mac_addr[6];

	rtos_printf("%s\n", __func__);
	if (!iface) {
		rtos_printf("iface is null\n");
		return -EINVAL;
	}

	if (syna_wifi_data.mhd_init == 0) {
		if (sdio_init() == 0) {
			if (wifi_up() != 0) {
				rtos_printf("wifiup fail\n");
				return -EIO;
			}
		} else {
			rtos_printf("wifi sdio init fail\n");
			return -EIO;
		}
	}

	if (mhd_wifi_get_mac_address(&mac_addr[0], 0) == 0) {
		if (net_if_set_link_addr(iface, mac_addr, 6, NET_LINK_ETHERNET)) {
			rtos_printf("Failed to set link addr");
		}
	}

	net_if_carrier_on(iface);
	net_if_dormant_off(iface);

	enable_dhcpv4_server("192.168.3.1");
	return 0;
}

static int syna_mgmt_iface_stop_ap(const struct device *dev)
{
	syna_wifi_data_t *data = dev->data;
	struct net_if *iface = data->iface;

	rtos_printf("%s\n", __func__);
	if (!iface) {
		rtos_printf("iface is null\n");
		return -EINVAL;
	}

	if (syna_wifi_data.mhd_init == 0) {
		rtos_printf("wifi not init\n");
		return -EIO;
	}

	net_if_dormant_on(iface);
	net_if_carrier_off(iface);
	rtos_printf("%s, %d\n", __func__, __LINE__);

	return 0;
}

int syna_ap_enable(const struct device *dev,
	struct wifi_connect_req_params *params)
{
	char *psk = NULL;
	syna_wifi_data_t *data = dev->data;
	int ret;

	if (!syna_wifi_data.mhd_init)
		return -EOPNOTSUPP;

	/* check iface NET_IF_UP and NET_IF_LOWER_UP */
	if ((data->iface != NULL) && net_if_flag_is_set(data->iface, NET_IF_UP)
		&& net_if_is_carrier_ok(data->iface)) {
		if (!params)
			return -EINVAL;
	} else {
		return -EOPNOTSUPP;
	}

	if (data->state == WIFI_STATE_COMPLETED) {
		rtos_printf("%s, call disable ap first\n", __func__);
		return -EOPNOTSUPP;
	}

	psk = params->psk ? params->psk: (params->sae_password ? params->sae_password : NULL);

	rtos_printf("%s\n", __func__);
	ret = mhd_softap_start(params->ssid, psk, psk ? 2 : 0, params->channel);

	if (ret == 0) {
		data->state = WIFI_STATE_COMPLETED;
		wifi_mgmt_raise_ap_enable_result_event(data->iface, WIFI_STATUS_AP_SUCCESS);
	} else
		wifi_mgmt_raise_ap_enable_result_event(data->iface, WIFI_STATUS_AP_FAIL);

	return ret;
}

int syna_ap_disable(const struct device *dev)
{
	syna_wifi_data_t *data = dev->data;
	int ret;

	if (!syna_wifi_data.mhd_init) {
		return -EOPNOTSUPP;
	}

	rtos_printf("%s\n", __func__);
	ret = mhd_softap_stop(1);

	data->state = WIFI_STATE_DISCONNECTED;
	wifi_mgmt_raise_ap_disable_result_event(data->iface, WIFI_STATUS_AP_SUCCESS);
	return ret;
}

int syna_ap_status(const struct device *dev, struct wifi_iface_status *status)
{
	int sec;
	char ssid[32] = {0}, mac[6] = {0};
	uint32_t ch;
	if (!syna_wifi_data.mhd_init)  {
		rtos_printf("wifi not up\n");
		return -EOPNOTSUPP;
	}

	rtos_printf("%s\n", __func__);
	memset(ssid, 0, sizeof(ssid));
	memset(mac, 0, sizeof(mac));

	memset(status->ssid, 0, WIFI_SSID_MAX_LEN);
	memset(status->bssid, 0, WIFI_MAC_ADDR_LEN);
	status->iface_mode = WIFI_MODE_AP;

	mhd_ap_get_ssid(ssid);
	if (strlen(ssid)) {
		mhd_wifi_get_channel(MHD_AP_INTERFACE, &ch);
		mhd_wifi_get_mac_address(mac, 0);
		mhd_wifi_get_security(MHD_AP_INTERFACE, &sec);

		rtos_printf("softap config info ...... \n");
		rtos_printf("ssid : %s\n", ssid);
		rtos_printf("bssid: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		rtos_printf("ch   : %d\n", ch);
		rtos_printf("sec  : %08x\n", sec);

		memcpy(status->ssid, ssid, strlen(ssid));
		memcpy(status->bssid, mac, WIFI_MAC_ADDR_LEN);
		status->channel = ch;
		status->rssi = 0;
		status->band = ch >= 36 ? WIFI_FREQ_BAND_5_GHZ : WIFI_FREQ_BAND_2_4_GHZ;

		switch (sec) {
			case 0:
				status->security = WIFI_SECURITY_TYPE_NONE;
			break;
			case 0x80:
			case 0x84:
				status->security = WIFI_SECURITY_TYPE_PSK;
			break;
			case 0x40000:
				status->security = WIFI_SECURITY_TYPE_SAE;
			break;
		}
		status->state = WIFI_STATE_COMPLETED;
	} else {
		status->state = WIFI_STATE_DISCONNECTED;
		rtos_printf("softap not up\n");
	}

	return 0;
}

int syna_ap_config_params(const struct device *dev, struct wifi_ap_config_params *params)
{
	if (!syna_wifi_data.mhd_init) {
		return -EOPNOTSUPP;
	}

	rtos_printf("%s\n", __func__);
	return 0;
}

static int syna_mgmt_send_ap(const struct device *dev, struct net_pkt *pkt)
{
	syna_wifi_data_t *dev_data = dev->data;
	host_buffer_t *buf = NULL;
	int res;
	size_t pkt_len;
	int ret;

	if (!syna_wifi_data.mhd_init) {
		return -EOPNOTSUPP;
	}

	if (dev_data->state != WIFI_STATE_COMPLETED) {
		rtos_printf("%s err state %d\n", __func__, dev_data->state);
		return -EOPNOTSUPP;
	}

	pkt_len = net_pkt_get_len(pkt);

	if (pkt_len > HOST_LINK_MTU + 14) {
		rtos_printf("%s, len err %d\n", __func__, pkt_len);
		return -EIO;
	}

	res = host_buffer_get((host_buffer_t *)&buf, 0, pkt_len + 128, 0);
	if ((res != 0) || (buf == NULL)) {
		rtos_printf("%s, %d\n", __func__, __LINE__);
		return -EIO;
	}
	host_buffer_add_remove_at_front(&buf, 128);

	ret = net_pkt_read(pkt, host_buffer_get_current_piece_data_pointer(buf), pkt_len);
	if (ret < 0) {
		host_buffer_release(buf, 0);
		rtos_printf("%s, %d\n", __func__, __LINE__);
		return -EIO;
	}

	return mhd_network_send_ethernet_data(buf, MHD_AP_INTERFACE);
}

static const struct wifi_mgmt_ops syna_wifi_mgmt_ap = {
	.ap_enable = syna_ap_enable,
	.ap_disable = syna_ap_disable,
	.iface_status = syna_ap_status,
	.ap_config_params = syna_ap_config_params,
#if defined(CONFIG_NET_STATISTICS_WIFI)
	.get_stats = syna_mgmt_wifi_stats,
#endif
};

static const struct net_wifi_mgmt_offload syna_api_ap = {
	.wifi_iface.iface_api.init = syna_iface_init_ap,
	.wifi_iface.start = syna_mgmt_iface_start_ap,
	.wifi_iface.stop = syna_mgmt_iface_stop_ap,
	.wifi_iface.send = syna_mgmt_send_ap,
	.wifi_mgmt_api = &syna_wifi_mgmt_ap,
};

void enable_dhcpv4_server(char *ip)
{
	static struct net_in_addr addr;
	static struct net_in_addr netmaskAddr;

	if (!ip) {
		return;
	}

	if (net_addr_pton(NET_AF_INET, ip, &addr)) {
		rtos_printf("Invalid address: %s", ip);
		return;
	}

	if (net_addr_pton(NET_AF_INET, "255.255.255.0", &netmaskAddr)) {
		rtos_printf("Invalid netmask");
		return;
	}

	net_if_ipv4_set_gw(syna_wifi_data_ap.iface, &addr);

	if (net_if_ipv4_addr_add(syna_wifi_data_ap.iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
		rtos_printf("unable to set IP address for AP interface");
	}

	if (!net_if_ipv4_set_netmask_by_addr(syna_wifi_data_ap.iface, &addr, &netmaskAddr)) {
		rtos_printf("Unable to set netmask for AP interface: ");
	}

	addr.s4_addr[3] += 10; /* Starting IPv4 address for DHCPv4 address pool. */

	if (net_dhcpv4_server_start(syna_wifi_data_ap.iface, &addr) != 0) {
		rtos_printf("DHCP server is not started for desired IP");
		return;
	}

	rtos_printf("DHCPv4 server started...\n");
}

NET_DEVICE_INIT_INSTANCE(net_ap, "wifi_ap", wifi_ap, NULL, NULL, &syna_wifi_data_ap, NULL,
	CONFIG_SYNA_WIFI_INIT_PRIORITY, &syna_api_ap, ETHERNET_L2,
	NET_L2_GET_CTX_TYPE(ETHERNET_L2), HOST_LINK_MTU);

NET_DEVICE_INIT_INSTANCE(net_sta, "wifi_sta", wifi_sta, syna_init, NULL, &syna_wifi_data, NULL,
			  CONFIG_SYNA_WIFI_INIT_PRIORITY, &syna_api, ETHERNET_L2,
			  NET_L2_GET_CTX_TYPE(ETHERNET_L2), HOST_LINK_MTU);


struct net_if *mhd_get_sta_netif(void)
{
	return syna_wifi_data.iface;
}

int mhd_get_sta_netif_idx(void)
{
	return net_if_get_by_iface(syna_wifi_data.iface);
}

struct net_if *mhd_get_ap_netif(void)
{
	return syna_wifi_data_ap.iface;
}

int mhd_get_ap_netif_idx(void)
{
	return net_if_get_by_iface(syna_wifi_data_ap.iface);
}

