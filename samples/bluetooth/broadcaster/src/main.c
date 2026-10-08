/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* main.c - BLE Broadcaster sample for SR110 RDK + SYNA
 *
 * Continuously advertises a scannable beacon with the device name in the
 * scan response.  The device name is set via CONFIG_BT_DEVICE_NAME in
 * prj.conf.  Pairs with the observer sample.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static uint8_t mfg_data[] = {0xff, 0xff, 0x00};

/* Advertising data: flags + manufacturer data */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
};

/* Scan response: device name (visible to any scanner) */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* Scannable non-connectable advertising */
static const struct bt_le_adv_param adv_param = {
	.options      = BT_LE_ADV_OPT_SCANNABLE,
	.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
	.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
	.peer         = NULL,
};

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

	printk("BLE Broadcaster starting (SYNA on SR110)\n");
	printk("Device name: \"%s\"\n", DEVICE_NAME);

	/*
	 * Use the async callback form so that bt_init() (which downloads
	 * patchram) runs in the BT work-queue thread with its own large
	 * stack rather than on the main thread stack.
	 */
	err = bt_enable(bt_ready);
	if (err) {
		printk("bt_enable failed (err %d)\n", err);
		return 0;
	}

	/* Wait for the BT work-queue to finish initialising the controller */
	k_sem_take(&ble_init_ok, K_FOREVER);

	while (1) {
		err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad),
				      sd, ARRAY_SIZE(sd));
		if (err) {
			printk("Advertising start failed (err %d)\n", err);
			k_msleep(1000);
			continue;
		}

		printk("Advertising as \"%s\" [seq=0x%02x]\n",
		       DEVICE_NAME, mfg_data[2]);

		k_msleep(2000);

		err = bt_le_adv_stop();
		if (err) {
			printk("Advertising stop failed (err %d)\n", err);
		}

		mfg_data[2]++;
	}

	return 0;
}
