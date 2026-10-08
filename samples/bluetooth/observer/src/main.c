/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* main.c - BLE Observer sample for SR110 RDK + SYNA
 *
 * Scans for nearby BLE advertisers and prints address, RSSI, and AD data.
 * Pairs with the broadcaster sample.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("Found: %s RSSI=%d type=%u adlen=%u\n",
	       addr_str, rssi, type, ad->len);
}

/* Semaphore signalled from bt_ready() once the BT stack is up */
static K_SEM_DEFINE(ble_init_ok, 0, 1);

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}
	printk("Bluetooth initialized\n");
	k_sem_give(&ble_init_ok);
}

int main(void)
{
	int err;

	printk("BLE Observer starting (SYNA on SR110)\n");

	/*
	 * Use the async callback form so that bt_init() (which downloads
	 * patchram) runs in the BT work-queue thread with its own large
	 * stack rather than on the 1 KB main stack.
	 */
	err = bt_enable(bt_ready);
	if (err) {
		printk("bt_enable failed (err %d)\n", err);
		return 0;
	}

	/* Wait for the BT work-queue to finish initialising the controller */
	k_sem_take(&ble_init_ok, K_FOREVER);

	/* Continuous passive scan, 30 ms window, duplicate filtering */
	struct bt_le_scan_param scan_param = {
		.type    = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL_MIN,
		.window   = BT_GAP_SCAN_FAST_WINDOW,
	};

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		printk("Scan start failed (err %d)\n", err);
		return 0;
	}

	printk("Scanning started\n");

	/* Scan indefinitely */
	while (1) {
		k_msleep(1000);
	}

	return 0;
}
