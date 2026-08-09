/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_hal.h"

/******************** rtos shell process ***********************/
typedef int (uart_shell_cmd_t)(uint32_t argc, char** argv);
typedef struct uart_cmd_list {
	char *name;
	uart_shell_cmd_t *func_cb;
	char *help;
	void *next;
} uart_cmd_list_t;

uart_shell_cmd_t uart_cmd_help;
uart_shell_cmd_t shell_CommandWifiUp;
uart_shell_cmd_t shell_CommandIfUp;
uart_shell_cmd_t shell_CommandIfdn;
uart_shell_cmd_t shell_CommandIfconfig;
uart_shell_cmd_t shell_CommandIperf;
uart_shell_cmd_t shell_CommandScan;

uart_cmd_list_t shell_cmd_list[20] = {
	{"help",         uart_cmd_help,        "cmd help"},
	{"wifiup",       shell_CommandWifiUp,  "wifiup"},
	{"ifup_wifi",    shell_CommandIfUp,    "ifup_wifi"},
	{"ifdn_wifi",    shell_CommandIfdn,    "ifdn_wifi"},
	{"ifconfig",     shell_CommandIfconfig,"ifconfig"},
	{"scan",         shell_CommandScan,    "scan"},
	{"iperf",        shell_CommandIperf,   "iperf"},
	{NULL,           NULL,                  NULL},
};

int uart_cmd_handler(int argc, char *argv[])
{
	uart_cmd_list_t *func = &shell_cmd_list[0];

	if (argc) {
		argc--;
		argv++;
	}
	if (argv[0] == NULL) {
		uart_cmd_help(0, NULL);
		return -1;
	}

	while (func->name) {
		if ((strlen(argv[0]) == strlen(func->name)) &&
			(strncmp(func->name, argv[0], strlen(func->name)) == 0))	{
			rtos_printf("find cmd, %s, %p, %p, %d\n", argv[0], func->func_cb, argv, argc);
			func->func_cb(argc, argv);
			return 0;
		}
		func++;
	}

	rtos_printf("no cmd found, cmd:%s\n", argv[0]);
	return -2;
}

int uart_cmd_help(uint32_t argc, char* argv[])
{
	uart_cmd_list_t *func = &shell_cmd_list[0];

	rtos_printf("\n");
	while (func->name) {
		rtos_printf("cmd %s: %s\n", func->name, func->help);
		func++;
	}

	return 0;
}

int shell_CommandIperf(uint32_t argc, char** argv)
{
	argv++;
	argc--;
	if (argc < 4) {
		rtos_printf("iperf usage: iperf tcp_s/tcp_c/udp_s/udp_c x.x.x.x dur_time port\n");
		return 0;
	}

	if (!syna_wifi_data.mhd_init)  {
		rtos_printf("wifi not up\n");
		return 0;
	}

	rtos_printf("mode:   %s\n", argv[0]);
	rtos_printf("dst_ip: %s\n", argv[1]);
	rtos_printf("time:   %d\n", atoi(argv[2]));
	rtos_printf("port:   %d\n", atoi(argv[3]));

	if (strstr(argv[0], "tcp_s"))  {
		rtos_printf("run iperf tcp server\n");
		mhd_iperf_tcprx(atoi(argv[2]), atoi(argv[3]), argv[4] ? atoi(argv[4]) : 4096);
	}else if (strstr(argv[0], "tcp_c")) {
		rtos_printf("run iperf tcp client\n");
		mhd_iperf_tcptx(argv[1], atoi(argv[2]), atoi(argv[3]), argv[4] ? atoi(argv[4]) : 4096);
	} else if (strstr(argv[0], "udp_s")) {
		rtos_printf("run iperf udp server\n");
		mhd_iperf_udprx(atoi(argv[2]), atoi(argv[3]), argv[4] ? atoi(argv[4]) : 4096);
	} else if (strstr(argv[0], "udp_c")) {
		rtos_printf("run iperf udp client\n");
		mhd_iperf_udptx(argv[1], atoi(argv[2]), atoi(argv[3]), argv[4] ? atoi(argv[4]) : 1460);
	} else {
		rtos_printf("iperf mode error\n");
		return 0;
	}

	return 0;
}

/*
*
* cmd for wifi config sta/ap
*
*/
int shell_CommandIfUp(uint32_t argc, char** argv)
{
	argv++;
	argc--;

	if (argc < 3) {
		rtos_printf("ifup usage: [sta/ap] ssid channel password \n");
		rtos_printf("    [ap ch 252 will auto select 2.4G channel] \n");
		rtos_printf("    [ap ch 255 will auto select 5G channel] \n");
		rtos_printf("    [dhcp server start] \n");
		return 0;
	}

	if (!syna_wifi_data.mhd_init)  {
		rtos_printf("wifi not up\n");
		return 0;
	}

	rtos_printf("%s, %s, %s, %s, %s\n", __func__, argv[0], argv[1], argv[2], argv[3] ? argv[3] :"null");
	if (strncmp(argv[0], "sta", strlen("sta")) == 0) {
		mhd_leave_ap();
		mhd_join_ap(argv[1], argv[3] ? argv[3] : "");
	} else if (strncmp(argv[0], "ap", strlen("ap")) == 0) {
		mhd_softap_start(argv[1], argv[3], argv[3] ? 2 : 0, atoi(argv[2]));
		rtos_printf("%s, %d\n", __func__, __LINE__);
	} else {
		rtos_printf("%s , %d: unsupport mode(%s)", __func__, __LINE__, argv[0U]);
	}

	return 0;
}

/*
*
* cmd for wifi disconnect sta/ap
*
*/
int shell_CommandIfdn(uint32_t argc, char** argv)
{
	argv++;
	argc--;

	if (argc < 1) {
		rtos_printf("ifdn usage: sta/ap \n");
		return 0;
	}

	if (!syna_wifi_data.mhd_init)  {
		rtos_printf("wifi not up\n");
		return 0;
	}

	rtos_printf("%s , %d: \n", __func__, __LINE__);
	if (strncmp(argv[0], "sta", strlen("sta")) == 0) {
		rtos_printf("%s , %d: \n", __func__, __LINE__);
		mhd_leave_ap();
	} else if (strncmp(argv[0], "ap", strlen("ap")) == 0) {
		mhd_softap_stop(1);
		rtos_printf("%s , %d: ap mode", __func__, __LINE__);
	}

	return 0;
}

/*
*
* cmd for wifi scan/scanr
*
*/
int shell_CommandScan(uint32_t argc, char** argv)
{
	argv++;
	argc--;

	if (argc < 1) {
		rtos_printf("scan usage: scan/results \n");
		return 0;
	}
	if (!syna_wifi_data.mhd_init)  {
		rtos_printf("wifi not up\n");
		return 0;
	}

	if (strncmp(argv[0], "scan", strlen("scan")) == 0) {
		rtos_printf("%s, %d: \n", __func__, __LINE__);
		mhd_start_scan();
	} else if (strncmp(argv[0], "results", strlen("results")) == 0) {
		/* reduce count to avoid function size too big */
		static mhd_ap_info_t res[30], *record = NULL;
		int num = sizeof(res)/sizeof(mhd_ap_info_t), k = 0;

		mhd_get_scan_results(res, &num);
		rtos_printf("%s, %d: %d\n", __func__, __LINE__, num);

		record = &res[0];
		for (k = 0; k < num; k++) {
			rtos_printf("ap list %d\n", k+1);
			rtos_printf(" SSID    : %s\n", record->ssid);
			rtos_printf(" RSSI    : %ddBm \n", record->rssi);
			rtos_printf(" Security: %d\n", record->security);
			rtos_printf(" Channel : %d\n", record->channel);
			rtos_printf(" band    : %d\n", record->band);
			rtos_printf(" BSSID   : %02X:%02X:%02X:%02X:%02X:%02X\n\n",
				record->bssid[0], record->bssid[1], record->bssid[2],
				record->bssid[3], record->bssid[4], record->bssid[5]);

			record++;
		}
	}

	return 0;
}

/*
*
* cmd for wifi get mac/ip/dns address
*
*/
int shell_CommandIfconfig(uint32_t argc, char** argv)
{
	uint8_t mac[8];
	argv++;
	argc--;

	if (argc < 1) {
		rtos_printf("ifconfig usage: sta/ap   [get sta/ap ip/mask/gw/dns address]\n");
		rtos_printf("ifconfig usage: sta down [set sta netif down]\n");
		rtos_printf("ifconfig usage: sta 0    [config sta use dhcp]\n");
		rtos_printf("ifconfig usage: sta 192.168.43.3 255.255.255.0 192.168.43.1 [config sta static ip]\n");
		rtos_printf("ifconfig usage: ap  192.168.2.1  255.255.255.0 192.168.2.1  [config ap  static ip]\n");
		return 0;
	}

	if (!syna_wifi_data.mhd_init)  {
		rtos_printf("wifi not up\n");
		return 0;
	}
	mhd_wifi_get_mac_address(mac, 0);

	return 0;
}

/*
*
* cmd for wifi init, driver load/unload
*
*/
int shell_CommandWifiUp(uint32_t argc, char** argv)
{
	argv++;
	argc--;

	if (argc < 1) {
		rtos_printf("wifiup usage: wifiup init/insmod/rmmod/status/prio/help\n");
		return 0;
	}

	if (strncmp(argv[0], "status", strlen("status")) == 0) {
		char ssid[32] = {0}, mac[6] = {0};
		uint32_t ch;
		int rssi, rate;
		if (!syna_wifi_data.mhd_init)  {
			rtos_printf("wifi not up\n");
			return 0;
		}
		rtos_printf("sta info %p, %d\n", mhd_get_sta_netif(), mhd_get_sta_netif_idx());
		rtos_printf("ap  info %p, %d\n", mhd_get_ap_netif(), mhd_get_ap_netif_idx());

		memset(ssid, 0, sizeof(ssid));
		memset(mac, 0, sizeof(mac));

		mhd_ap_get_ssid(ssid);
		if (strlen(ssid)) {
			mhd_wifi_get_channel(MHD_AP_INTERFACE, &ch);
			mhd_wifi_get_mac_address(mac, 0);

			rtos_printf("softap config info ...... \n");
			rtos_printf("ssid : %s\n", ssid);
			rtos_printf("bssid: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
			rtos_printf("ch   : %d\n\n", ch);
		} else {
			rtos_printf("softap not up\n");
		}

		memset(ssid, 0, sizeof(ssid));
		memset(mac, 0, sizeof(mac));
		mhd_sta_get_ssid(ssid);
		mhd_sta_get_bssid(mac);

		if (strlen(ssid) && (mac[0] + mac[1] + mac[2])) {
			mhd_wifi_get_channel(MHD_STA_INTERFACE, &ch);
			rate = mhd_sta_get_rate();
			rssi = mhd_sta_get_rssi();

			rtos_printf("sta connection info ......\n");
			rtos_printf("ssid : %s\n", ssid);
			rtos_printf("bssid: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
			rtos_printf("ch   : %d\n", ch);
			rtos_printf("rate : %d\n", rate/2);
			rtos_printf("rssi : %ddbm\n\n", rssi);
		} else {
			rtos_printf("sta not connected\n");
		}
	} else if (strncmp(argv[0], "mhd_init", strlen("mhd_init")) == 0) {
		rtos_printf("init %d\n", mhd_is_initialized());
	} else if (strncmp(argv[0], "hal_ver", strlen("hal_ver")) == 0) {
		rtos_printf("hal_ver %s\n", WIFI_HAL_VER);
	} else if (strncmp(argv[0], "cons", strlen("cons")) == 0) {
		if (argv[1]) {
			mhd_config_fwlog(atoi(argv[1]), atoi(argv[2]));
		}
	} else if (strncmp(argv[0], "up", strlen("ping")) == 0) {
		if (syna_wifi_data.mhd_init)  {
			rtos_printf("wifi isup\n");
			return 0;
		}
		if (sdio_init() == 0) {
			wifi_up();
		} else {
			rtos_printf("wifi init fail\n");
			return 0;
		}
	} else if (strncmp(argv[0], "help", strlen("help")) == 0) {
		rtos_printf("\n@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");

		rtos_printf("ifconfig usage: sta/ap   [get sta/ap ip/mask/gw/dns address]\n");
		rtos_printf("ifconfig usage: sta down [set sta netif down]\n");
		rtos_printf("ifconfig usage: sta 0    [config sta use dhcp]\n");
		rtos_printf("ifconfig usage: sta 192.168.43.3 255.255.255.0 192.168.43.1 [config sta static ip]\n");
		rtos_printf("ifconfig usage: ap  192.168.2.1  255.255.255.0 192.168.2.1  [config ap  static ip]\n");

		rtos_printf("\n");
		rtos_printf("scan usage: scan/results \n");

		rtos_printf("\n");
		rtos_printf("ifup usage: [sta/ap] ssid channel password \n");
		rtos_printf("    [ap ch 252 will auto select 2.4G channel] \n");
		rtos_printf("    [ap ch 255 will auto select 5G channel] \n");
		rtos_printf("ifdn usage: sta/ap \n");
		rtos_printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n\n");
	} else {
		rtos_printf("%s, unknown action \n", __func__);
	}

	return 0;
}

