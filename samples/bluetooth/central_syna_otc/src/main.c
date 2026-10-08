/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/ots.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/console/console.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>

LOG_MODULE_REGISTER(central_otc);

#define MAX_SCAN_RESULTS    20
#define OBJ_MAX_SIZE        1024
#define STR_HELPER(x)       #x
#define STR(x)              STR_HELPER(x)

/* Device scanner state */
static struct bt_le_scan_param scan_param = {
	.type       = BT_LE_SCAN_TYPE_ACTIVE,
	.options    = BT_LE_SCAN_OPT_NONE,
	.interval   = BT_GAP_SCAN_FAST_INTERVAL,
	.window     = BT_GAP_SCAN_FAST_WINDOW,
};

static struct {
	bt_addr_le_t addr;
	int8_t rssi;
	char name[32];
} scan_results[MAX_SCAN_RESULTS];
static size_t scan_count;
static K_SEM_DEFINE(scan_done_sem, 0, 1);

/* OTS client state */
static struct bt_ots_client otc;
static struct bt_ots_client_cb otc_cb;
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params *oacp_sub_params;
static struct bt_gatt_subscribe_params *olcp_sub_params;
static struct bt_gatt_subscribe_params *obj_changed_sub_params;
static struct bt_uuid_16 discover_uuid = BT_UUID_INIT_16(0);
static struct bt_conn *default_conn;
static atomic_t discovery_state;

/* GATT write buffer for OACP CREATE */
static uint8_t oacp_create_buf[21]; /* 1 byte opcode + 4 bytes size + 16 bytes UUID */

/* Connection state */
static bool is_connected;
static K_SEM_DEFINE(conn_event_sem, 0, 1);

/* MTU exchange params */
static struct bt_gatt_exchange_params mtu_exchange_params;

/* Track newly created object for name assignment */
static bool newly_created_object;
static char new_obj_name[64];
static uint64_t last_created_obj_id;
static uint64_t obj_id_before_name_write;  /* Save object ID when setting name */

enum OTS_SERVICE_DISCOVERY_STATE_BIT {
	DISC_OTS_FEATURE,
	DISC_OTS_NAME,
	DISC_OTS_TYPE,
	DISC_OTS_SIZE,
	DISC_OTS_ID,
	DISC_OTS_PROPERTIES,
	DISC_OTS_ACTION_CP,
	DISC_OTS_LIST_CP,
	DISC_OTS_CHANGED,
};

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void parse_adv_name(struct net_buf_simple *ad, char *buf, size_t len)
{
	while (ad->len > 1) {
		uint8_t alen = net_buf_simple_pull_u8(ad);
		uint8_t type = net_buf_simple_pull_u8(ad);

		if (type == BT_DATA_NAME_COMPLETE || type == BT_DATA_NAME_SHORTENED) {
			size_t name_len = MIN(alen - 1, len - 1);

			memcpy(buf, ad->data, name_len);
			buf[name_len] = '\0';
			return;
		}
		net_buf_simple_pull_mem(ad, alen - 1);
	}
	buf[0] = '\0';
}

static void print_device(int idx, const bt_addr_le_t *addr, const char *name, int8_t rssi)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("  [%d] %s  rssi=%d  %s\n", idx, addr_str, rssi,
	       (name && name[0]) ? name : "(no name)");
}

/* ── Scan callbacks ──────────────────────────────────────────────────────── */

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char name[32];

	if (scan_count >= MAX_SCAN_RESULTS) {
		return;
	}

	parse_adv_name(ad, name, sizeof(name));

	memcpy(&scan_results[scan_count].addr, addr, sizeof(bt_addr_le_t));
	scan_results[scan_count].rssi = rssi;
	strncpy(scan_results[scan_count].name, name, sizeof(scan_results[scan_count].name) - 1);
	scan_results[scan_count].name[sizeof(scan_results[scan_count].name) - 1] = '\0';

	scan_count++;
}

/* ── Scan management ─────────────────────────────────────────────────────── */

static void do_scan(void)
{
	int err;

	scan_count = 0;
	memset(scan_results, 0, sizeof(scan_results));

	printk("\n--- Scanning for BLE devices (5 sec) ---\n");

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		LOG_ERR("LE Scan start failed (err %d)", err);
		return;
	}

	/* Let scan run for 5 seconds, then stop */
	k_sleep(K_SECONDS(5));
	bt_le_scan_stop();

	printk("Scan complete: %d device(s) found\n", (int)scan_count);
}

static bool show_menu_and_connect(void)
{
	printk("\n--- Device list (%d found) ---\n", (int)scan_count);

	for (size_t i = 0; i < scan_count; i++) {
		print_device((int)i, &scan_results[i].addr, scan_results[i].name,
			     scan_results[i].rssi);
	}
	printk("  [r] Rescan\n");

	for (;;) {
		printk("Choice: ");

		/* Read input number (support multi-digit like 10, 15, etc.) */
		char input[16];
		int pos = 0;

		while (pos < (int)sizeof(input) - 1) {
			int ch = console_getchar();

			if (ch == '\r' || ch == '\n') {
				break;
			}

			if (ch == 'r' || ch == 'R') {
				printk("r\n");
				return true;
			}

			if (ch >= '0' && ch <= '9') {
				printk("%c", ch);
				input[pos++] = ch;
			}
		}
		printk("\n");

		input[pos] = '\0';

		if (pos == 0) {
			continue;
		}

		if (input[0] == 'r' || input[0] == 'R') {
			return true;
		}

		int idx = atoi(input);

		if (scan_count == 0 || (size_t)idx >= scan_count) {
			printk("Out of range (0-%d).\n",
			       scan_count > 0 ? (int)scan_count - 1 : 0);
			continue;
		}

		char addr[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(&scan_results[idx].addr, addr, sizeof(addr));
		printk("Connecting to %s...\n", addr);

		struct bt_le_conn_param param = {
			.interval_min = 24,  /* 30ms (1.25ms per unit) */
			.interval_max = 24,
			.latency      = 0,
			.timeout      = 400,
		};

		int err = bt_conn_le_create(&scan_results[idx].addr, BT_CONN_LE_CREATE_CONN,
					    &param, &default_conn);
		if (err) {
			LOG_ERR("bt_conn_le_create failed (err %d)", err);
			continue;
		}

		return false;
	}
}

/* ── GATT discovery & OTS initialization ──────────────────────────────────── */

static bool is_discovery_complete(void)
{
	return (atomic_test_bit(&discovery_state, DISC_OTS_FEATURE) &&
		atomic_test_bit(&discovery_state, DISC_OTS_NAME) &&
		atomic_test_bit(&discovery_state, DISC_OTS_TYPE) &&
		atomic_test_bit(&discovery_state, DISC_OTS_SIZE) &&
		atomic_test_bit(&discovery_state, DISC_OTS_ID) &&
		atomic_test_bit(&discovery_state, DISC_OTS_PROPERTIES) &&
		atomic_test_bit(&discovery_state, DISC_OTS_ACTION_CP) &&
		atomic_test_bit(&discovery_state, DISC_OTS_LIST_CP) &&
		atomic_test_bit(&discovery_state, DISC_OTS_CHANGED));
}

static int subscribe_func(void)
{
	int ret;

	LOG_INF("Subscribing to OACP...");
	oacp_sub_params = &otc.oacp_sub_params;
	oacp_sub_params->disc_params = &otc.oacp_sub_disc_params;
	oacp_sub_params->ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
	oacp_sub_params->end_handle = otc.end_handle;
	oacp_sub_params->value = BT_GATT_CCC_INDICATE;
	oacp_sub_params->value_handle = otc.oacp_handle;
	oacp_sub_params->notify = bt_ots_client_indicate_handler;

	ret = bt_gatt_subscribe(default_conn, oacp_sub_params);
	if (ret) {
		LOG_ERR("Subscribe OACP failed (err %d)", ret);
		return ret;
	}
	LOG_INF("OACP subscribed OK");

	LOG_INF("Subscribing to OLCP...");
	olcp_sub_params = &otc.olcp_sub_params;
	olcp_sub_params->disc_params = &otc.olcp_sub_disc_params;
	olcp_sub_params->ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
	olcp_sub_params->end_handle = otc.end_handle;
	olcp_sub_params->value = BT_GATT_CCC_INDICATE;
	olcp_sub_params->value_handle = otc.olcp_handle;
	olcp_sub_params->notify = bt_ots_client_indicate_handler;

	ret = bt_gatt_subscribe(default_conn, olcp_sub_params);
	if (ret) {
		LOG_ERR("Subscribe OLCP failed (err %d)", ret);
		return ret;
	}
	LOG_INF("OLCP subscribed OK");

	if (otc.obj_changed_handle) {
		LOG_INF("Subscribing to Object Changed...");
		obj_changed_sub_params = &otc.obj_changed_sub_params;
		obj_changed_sub_params->disc_params = &otc.obj_changed_sub_disc_params;
		obj_changed_sub_params->ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
		obj_changed_sub_params->end_handle = otc.end_handle;
		obj_changed_sub_params->value = BT_GATT_CCC_INDICATE;
		obj_changed_sub_params->value_handle = otc.obj_changed_handle;
		obj_changed_sub_params->notify = bt_ots_client_indicate_handler;

		ret = bt_gatt_subscribe(default_conn, obj_changed_sub_params);
		if (ret) {
			LOG_ERR("Subscribe Object Changed failed (err %d)", ret);
			return ret;
		}
		LOG_INF("Object Changed subscribed OK");
	}

	return 0;
}

static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;

	LOG_INF("discover_func called: attr=%p", (void *)attr);

	if (!attr) {
		LOG_INF("Discovery iteration complete");
		memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	if (bt_uuid_cmp(discover_params.uuid, BT_UUID_OTS) == 0) {
		LOG_INF("Found OTS service");
		memcpy(&discover_uuid, BT_UUID_OTS_FEATURE, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discovery failed (err %d)", err);
		}

	} else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_OTS_FEATURE) == 0) {
		atomic_set_bit(&discovery_state, DISC_OTS_FEATURE);
		otc.feature_handle = bt_gatt_attr_value_handle(attr);

		memcpy(&discover_uuid, BT_UUID_OTS_NAME, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discovery failed (err %d)", err);
		}

	} else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_OTS_NAME) == 0) {
		atomic_set_bit(&discovery_state, DISC_OTS_NAME);
		otc.obj_name_handle = bt_gatt_attr_value_handle(attr);

		memcpy(&discover_uuid, BT_UUID_OTS_TYPE, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discovery failed (err %d)", err);
		}

	} else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_OTS_TYPE) == 0) {
		atomic_set_bit(&discovery_state, DISC_OTS_TYPE);
		otc.obj_type_handle = bt_gatt_attr_value_handle(attr);

		memcpy(&discover_uuid, BT_UUID_OTS_SIZE, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discovery failed (err %d)", err);
		}

	} else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_OTS_SIZE) == 0) {
		atomic_set_bit(&discovery_state, DISC_OTS_SIZE);
		otc.obj_size_handle = bt_gatt_attr_value_handle(attr);

		memcpy(&discover_uuid, BT_UUID_OTS_ID, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discovery failed (err %d)", err);
		}

	} else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_OTS_ID) == 0) {
		atomic_set_bit(&discovery_state, DISC_OTS_ID);
		otc.obj_id_handle = bt_gatt_attr_value_handle(attr);

		memcpy(&discover_uuid, BT_UUID_OTS_PROPERTIES, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discovery failed (err %d)", err);
		}

	} else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_OTS_PROPERTIES) == 0) {
		atomic_set_bit(&discovery_state, DISC_OTS_PROPERTIES);
		otc.obj_properties_handle = bt_gatt_attr_value_handle(attr);

		memcpy(&discover_uuid, BT_UUID_OTS_ACTION_CP, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discovery failed (err %d)", err);
		}

	} else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_OTS_ACTION_CP) == 0) {
		atomic_set_bit(&discovery_state, DISC_OTS_ACTION_CP);
		otc.oacp_handle = bt_gatt_attr_value_handle(attr);

		memcpy(&discover_uuid, BT_UUID_OTS_LIST_CP, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discovery failed (err %d)", err);
		}

	} else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_OTS_LIST_CP) == 0) {
		atomic_set_bit(&discovery_state, DISC_OTS_LIST_CP);
		otc.olcp_handle = bt_gatt_attr_value_handle(attr);

		memcpy(&discover_uuid, BT_UUID_OTS_CHANGED, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discovery failed (err %d)", err);
		}

	} else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_OTS_CHANGED) == 0) {
		atomic_set_bit(&discovery_state, DISC_OTS_CHANGED);
		otc.obj_changed_handle = bt_gatt_attr_value_handle(attr);

		/* Print all discovered handles for debugging */
		LOG_INF("OTS Discovery Complete!");
		LOG_INF("  feature_handle: 0x%04x", otc.feature_handle);
		LOG_INF("  obj_name_handle: 0x%04x", otc.obj_name_handle);
		LOG_INF("  obj_type_handle: 0x%04x", otc.obj_type_handle);
		LOG_INF("  obj_size_handle: 0x%04x", otc.obj_size_handle);
		LOG_INF("  obj_id_handle: 0x%04x", otc.obj_id_handle);
		LOG_INF("  obj_properties_handle: 0x%04x", otc.obj_properties_handle);
		LOG_INF("  oacp_handle (decl): 0x%04x, value: 0x%04x", otc.oacp_handle, otc.oacp_handle + 1);
		LOG_INF("  olcp_handle (decl): 0x%04x, value: 0x%04x", otc.olcp_handle, otc.olcp_handle + 1);
		LOG_INF("  obj_changed_handle: 0x%04x", otc.obj_changed_handle);
	} else {
		return BT_GATT_ITER_STOP;
	}

	if (is_discovery_complete()) {
		LOG_INF("OTS discovery complete, subscribing...");
		int err = subscribe_func();
		if (err) {
			LOG_ERR("Subscribe failed (err %d) - disconnecting", err);
			if (default_conn) {
				bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			}
			return BT_GATT_ITER_STOP;
		}

		LOG_INF("Reading OTS feature...");
		err = bt_ots_client_read_feature(&otc, default_conn);
		if (err) {
			LOG_ERR("bt_ots_client_read_feature failed (err %d)", err);
		} else {
			LOG_INF("OTS feature read initiated, waiting for callback...");
		}

		/* Signal to main that discovery is complete */
		k_sem_give(&conn_event_sem);
	}

	return BT_GATT_ITER_STOP;
}

/* ── MTU Exchange callback ───────────────────────────────────────────────── */

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			     struct bt_gatt_exchange_params *params)
{
	if (err) {
		LOG_WRN("MTU exchange failed (err %d)", err);
	} else {
		uint16_t mtu = bt_gatt_get_mtu(conn);
		LOG_INF("MTU exchange successful, negotiated MTU: %u bytes", mtu);
	}

	/* Start GATT discovery after MTU exchange completes */
	LOG_INF("Starting GATT discovery for OTS service...");
	memcpy(&discover_uuid, BT_UUID_OTS, sizeof(discover_uuid));
	discover_params.uuid = &discover_uuid.uuid;
	discover_params.func = discover_func;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_PRIMARY;

	int disc_err = bt_gatt_discover(conn, &discover_params);
	if (disc_err) {
		LOG_ERR("Discovery start failed (err %d)", disc_err);
	}
}

/* ── Connection callbacks ────────────────────────────────────────────────── */

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_WRN("ACL connect failed (err %u)", err);
		if (default_conn) {
			bt_conn_unref(default_conn);
			default_conn = NULL;
		}
		k_sem_give(&conn_event_sem);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected: %s", addr);

	is_connected = true;

	/* Request MTU exchange to 255 bytes for better GATT write performance */
	mtu_exchange_params.func = mtu_exchange_cb;
	int mtu_err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
	if (mtu_err) {
		LOG_WRN("MTU exchange request failed (err %d), starting discovery anyway", mtu_err);

		/* Start discovery immediately if MTU exchange fails */
		LOG_INF("Starting GATT discovery for OTS service...");
		memcpy(&discover_uuid, BT_UUID_OTS, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.func = discover_func;
		discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		discover_params.type = BT_GATT_DISCOVER_PRIMARY;

		int err2 = bt_gatt_discover(default_conn, &discover_params);
		if (err2) {
			LOG_ERR("Discovery start failed (err %d)", err2);
		}
	}
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}

	is_connected = false;
	discovery_state = ATOMIC_INIT(0);
	k_sem_give(&conn_event_sem);
}

static struct bt_conn_cb conn_cb = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

/* ── Write Object Name ───────────────────────────────────────────────────── */

static void on_name_write_complete(struct bt_conn *conn, uint8_t err,
				    struct bt_gatt_write_params *params)
{
	if (err) {
		LOG_ERR("Write Object Name failed (err %d)", err);
	} else {
		LOG_INF("Object Name written successfully");

		/* Re-select the object to ensure state is synchronized */
		if (obj_id_before_name_write != 0) {
			LOG_INF("Re-selecting object 0x%012llx after name write", obj_id_before_name_write);
			int select_err = bt_ots_client_select_id(&otc, conn, obj_id_before_name_write);
			if (select_err) {
				LOG_ERR("Failed to re-select object after name write (err %d)", select_err);
			}
		}
	}

	newly_created_object = false;
	obj_id_before_name_write = 0;
	memset(new_obj_name, 0, sizeof(new_obj_name));
}

static int write_object_name(struct bt_conn *conn, const char *name)
{
	int err;
	size_t name_len = strlen(name);

	if (!conn || otc.obj_name_handle == 0) {
		LOG_ERR("Connection or Object Name handle invalid");
		return -EINVAL;
	}

	if (name_len > 63) {
		LOG_ERR("Object name too long (%u > 63 bytes)", name_len);
		return -EINVAL;
	}

	/* Save current object ID before writing name (to re-select after write) */
	obj_id_before_name_write = otc.cur_object.id;

	/* Copy name for write operation */
	memcpy(new_obj_name, name, name_len);

	LOG_INF("Writing Object Name '%s' to handle 0x%04x (%u bytes)",
		name, otc.obj_name_handle, name_len);

	/* Write the name (as is, no null terminator) */
	err = bt_gatt_write_without_response(conn, otc.obj_name_handle,
					    (uint8_t *)new_obj_name, name_len, false);

	if (err) {
		LOG_ERR("bt_gatt_write_without_response for name failed (err %d)", err);
		obj_id_before_name_write = 0;
		return err;
	}

	return 0;
}

/* ── OTS client callbacks ────────────────────────────────────────────────── */

static void on_obj_selected(struct bt_ots_client *ots_inst, struct bt_conn *conn, int err)
{
	if (err == 0x01) { /* BT_GATT_OTS_OLCP_RES_SUCCESS */
		LOG_INF("Object selected successfully (ID: 0x%012llx)", ots_inst->cur_object.id);

		/* If this is a newly created object, just display it and read metadata */
		if (newly_created_object) {
			printk("\n✓ New object created successfully!\n");
			printk("Object ID: 0x%012llx\n", ots_inst->cur_object.id);
			last_created_obj_id = ots_inst->cur_object.id;
			newly_created_object = false;

			/* Immediately read metadata for the new object */
			LOG_INF("Reading metadata for newly created object...");
			int read_err = bt_ots_client_read_object_metadata(ots_inst, conn,
									 BT_OTS_METADATA_REQ_ALL);
			if (read_err) {
				LOG_ERR("Failed to read metadata for new object (err %d)", read_err);
			}
			return;
		}

		/* Immediately trigger metadata read to get actual name/size/props */
		int read_err = bt_ots_client_read_object_metadata(ots_inst, conn,
								 BT_OTS_METADATA_REQ_ALL);
		if (read_err) {
			LOG_ERR("Failed to read metadata after select (err %d)", read_err);
		}
	} else if (err == 0x05) { /* BT_GATT_OTS_OLCP_RES_OUT_OF_BONDS */
		printk("OUT_OF_BONDS: wrapping to first object...\n");
		bt_ots_client_select_id(ots_inst, conn, BT_OTS_OBJ_ID_MIN);
	} else {
		LOG_ERR("Object selected failed (err 0x%02x)", err);
	}
}

static void on_obj_metadata_read(struct bt_ots_client *ots_inst, struct bt_conn *conn, int err,
				 uint8_t metadata_read)
{
	if (err) {
		LOG_ERR("Object metadata read failed (err %d)", err);
		return;
	}

	printk("\n=== Object Metadata ===\n");
	printk("Object ID: 0x%012llx\n", ots_inst->cur_object.id);
	printk("Object Name: %s\n", ots_inst->cur_object.name_c);
	printk("Current size: %u bytes\n", ots_inst->cur_object.size.cur);
	printk("Allocated size: %u bytes\n", ots_inst->cur_object.size.alloc);

	uint32_t props = ots_inst->cur_object.props;
	printk("Properties: 0x%04x (", props);
	if (props & (1 << BT_OTS_OBJ_PROP_READ)) printk("READ ");
	if (props & (1 << BT_OTS_OBJ_PROP_WRITE)) printk("WRITE ");
	if (props & (1 << BT_OTS_OBJ_PROP_PATCH)) printk("PATCH ");
	printk(")\n");

	/* Now check for actual issues */
	if (strlen(ots_inst->cur_object.name_c) == 0) {
		LOG_WRN("  WARNING: Object name is empty (server may not have initialized object)");
	}
	if (ots_inst->cur_object.size.alloc == 0) {
		LOG_WRN("  WARNING: Allocated size is 0 (object may not be ready)");
	}
}

/* ── CREATE object operation ────────────────────────────────────────────── */

static int ots_client_create_object(struct bt_conn *conn, uint32_t obj_size)
{
	int err;

	if (!conn || otc.oacp_handle == 0) {
		LOG_ERR("Connection or OACP handle invalid");
		return -EINVAL;
	}

	/* Build CREATE request: opcode (1) + size (4) + type UUID (16) */
	memset(oacp_create_buf, 0, sizeof(oacp_create_buf));
	oacp_create_buf[0] = 0x01; /* CREATE opcode */
	sys_put_le32(obj_size, &oacp_create_buf[1]);
	/* Type UUID bytes 2-17 remain 0x00 (unspecified type) */

	/* Write directly without response using Write Command (not Request with response) */
	LOG_INF("Writing CREATE to handle 0x%04x (21 bytes) using Write Command", otc.oacp_handle);
	err = bt_gatt_write_without_response(conn, otc.oacp_handle, oacp_create_buf, 21, false);

	if (err) {
		LOG_ERR("bt_gatt_write_without_response failed (err %d)", err);
		return err;
	}

	printk("CREATE object request sent (size=%u bytes, handle=0x%04x)\n", obj_size, otc.oacp_handle);
	printk("Waiting for server to generate object ID...\n");

	/* Mark that a new object is being created */
	newly_created_object = true;

	/* After CREATE completes, OTS stack will auto-select the new object.
	 * We need to explicitly select the last object to trigger on_obj_selected().
	 * Add a small delay to allow the CREATE response to complete first.
	 */
	k_msleep(100);

	/* Select the last object (which should be the newly created one) */
	err = bt_ots_client_select_last(&otc, conn);
	if (err) {
		LOG_ERR("Failed to select last object after CREATE (err %d)", err);
		newly_created_object = false;
		return err;
	}

	return 0;
}

/* ── DELETE object operation ────────────────────────────────────────────── */

static int ots_client_delete_object(struct bt_conn *conn)
{
	int err;

	if (!conn || otc.oacp_handle == 0) {
		LOG_ERR("Connection or OACP handle invalid");
		return -EINVAL;
	}

	/* Build DELETE request: opcode (1 byte, 0x02) - no additional parameters */
	uint8_t delete_buf[1];
	delete_buf[0] = 0x02; /* DELETE opcode */

	LOG_INF("Writing DELETE to handle 0x%04x for object 0x%012llx (1 byte)",
		otc.oacp_handle, otc.cur_object.id);
	err = bt_gatt_write_without_response(conn, otc.oacp_handle, delete_buf, 1, false);

	if (err) {
		LOG_ERR("bt_gatt_write_without_response failed (err %d)", err);
		return err;
	}

	printk("DELETE object request sent for 0x%012llx\n", otc.cur_object.id);

	return 0;
}

static int on_obj_data_read(struct bt_ots_client *ots_inst, struct bt_conn *conn, uint32_t offset,
			    uint32_t len, uint8_t *data_p, bool is_complete)
{
	LOG_INF("Object data read: %u bytes at offset %u %s", len, offset,
		is_complete ? "(complete)" : "(partial)");

	/* Display data efficiently with buffering */
	char buf[256];
	int buf_idx = 0;

	buf_idx += snprintf(buf + buf_idx, sizeof(buf) - buf_idx, "Data (offset %u): ", offset);

	for (uint32_t i = 0; i < len && buf_idx < sizeof(buf) - 10; i++) {
		if (data_p[i] >= 32 && data_p[i] < 127) {
			buf[buf_idx++] = data_p[i];
		} else {
			buf_idx += snprintf(buf + buf_idx, sizeof(buf) - buf_idx, "[%02x]", data_p[i]);
		}
	}

	buf[buf_idx] = '\0';
	printk("%s\n", buf);

	if (is_complete) {
		printk("==> Object data transfer complete!\n");
	}

	return BT_OTS_CONTINUE;
}

static void on_obj_data_written(struct bt_ots_client *ots_inst, struct bt_conn *conn, size_t len)
{
	LOG_INF("Object data written: %u bytes", len);
	printk("✓ Write successful! Press [m] to refresh metadata.\n");
}

static void on_obj_checksum_calculated(struct bt_ots_client *ots_inst,
				       struct bt_conn *conn, int err, uint32_t checksum)
{
	LOG_INF("Object checksum: 0x%08x (err %d)", checksum, err);
}

static void on_obj_changed(struct bt_ots_client *ots_inst, struct bt_conn *conn,
			   uint8_t flags, uint64_t id)
{
	char id_str[BT_OTS_OBJ_ID_STR_LEN];

	bt_ots_obj_id_to_str(id, id_str, sizeof(id_str));
	printk("\n=== Object Changed Indication ===\n");
	printk("Object ID: %s\n", id_str);
	printk("Flags: 0x%02x (", flags);
	if (flags & BT_OTS_OBJECT_CHANGED_FLAG_CONTENT) printk("CONTENT ");
	if (flags & BT_OTS_OBJECT_CHANGED_FLAG_METADATA) printk("METADATA ");
	if (flags & BT_OTS_OBJECT_CHANGED_FLAG_DELETED) printk("DELETED ");
	printk(")\n");

	if (flags & BT_OTS_OBJECT_CHANGED_FLAG_DELETED) {
		printk("Object deleted by server\n");
	} else {
		printk("Auto-selecting changed object...\n");
		int err = bt_ots_client_select_id(ots_inst, conn, id);
		if (err) {
			LOG_ERR("Failed to select changed object (err %d)", err);
		}
	}
}

/* ── Interactive menu after connection ───────────────────────────────────── */

static void display_menu(void)
{
	printk("\n=== Connected to OTS server ===\n");
	printk("\n[QUICK TEST GUIDE]\n");
	printk("Read File:  Press [s] then [m] then [r]\n");
	printk("Write File: Press [c] then [t] then [w]\n\n");
	printk("[s] Select first object\n");
	printk("[n] Select next object\n");
	printk("[m] Read metadata\n");
	printk("[r] Read object data\n");
	printk("[w] Write object data\n");
	printk("[t] Set object name\n");
	printk("[c] Create new object\n");
	printk("[d] Delete current object\n");
	printk("[h] Help menu\n");
	printk("[q] Quit/Disconnect\n");
}

static void show_connected_menu(void)
{
	display_menu();

	while (is_connected) {
		printk("\nChoice: ");

		int ch = console_getchar();

		if (ch == '\r' || ch == '\n') {
			continue;
		}

		printk("%c\n", ch);

		if (!is_connected) {
			break;
		}

		int err = 0;

		if (ch == 's' || ch == 'S') {
			err = bt_ots_client_select_id(&otc, default_conn, BT_OTS_OBJ_ID_MIN);
			if (err) {
				LOG_ERR("Select first failed (err %d)", err);
			} else {
				printk("Selecting first object...\n");
			}

		} else if (ch == 'n' || ch == 'N') {
			err = bt_ots_client_select_next(&otc, default_conn);
			if (err) {
				LOG_ERR("Select next failed (err %d)", err);
			} else {
				printk("Selecting next object...\n");
			}

		} else if (ch == 'm' || ch == 'M') {
			err = bt_ots_client_read_object_metadata(&otc, default_conn,
								 BT_OTS_METADATA_REQ_ALL);
			if (err) {
				LOG_ERR("Read metadata failed (err %d)", err);
			} else {
				printk("Reading object metadata...\n");
			}

		} else if (ch == 'r' || ch == 'R') {
			printk("\n--- Reading Object Data ---\n");
			printk("Object ID: 0x%012llx\n", otc.cur_object.id);
			printk("Object Name: %s\n", otc.cur_object.name_c);
			printk("Object Size: %u / %u bytes\n",
				otc.cur_object.size.cur, otc.cur_object.size.alloc);
			printk("\nData:\n");
			err = bt_ots_client_read_object_data(&otc, default_conn);
			if (err) {
				LOG_ERR("Read object failed (err %d)", err);
			} else {
				printk("Reading object data...\n");
			}

		} else if (ch == 'w' || ch == 'W') {
			printk("\n=== Write Mode ===\n");
			printk("Use [s]/[n] to select objects, [r] to read, [w] to write, [d] to disconnect\n");
			printk("Press [w] again to write data to current object\n");

			/* Trigger automatic write data input */
			if (otc.cur_object.size.alloc == 0) {
				printk("ERROR: Object not ready (allocated size = 0)\n");
				printk("Try [s] or [n] to select an object first\n");
			} else {
				/* Get user input for write data */
				char write_buf[256];
				int buf_idx = 0;

				printk("Enter data to write (max 255 chars, press Enter to finish):\n");
				printk("> ");

				while (buf_idx < sizeof(write_buf) - 1) {
					int ch_input = console_getchar();

					if (ch_input == '\r' || ch_input == '\n') {
						printk("\n");
						break;
					}

					if (ch_input == '\b' || ch_input == 0x7f) { /* Backspace */
						if (buf_idx > 0) {
							buf_idx--;
							printk("\b \b");
						}
					} else if (ch_input >= 32 && ch_input < 127) {
						write_buf[buf_idx++] = ch_input;
						printk("%c", ch_input);
					}
				}
				write_buf[buf_idx] = '\0';

				if (buf_idx == 0) {
					printk("No data to write. Aborting.\n");
				} else {
					printk("\n--- Writing Object Data ---\n");
					printk("Object ID: 0x%012llx\n", otc.cur_object.id);
					printk("Object Name: %s\n", otc.cur_object.name_c);
					printk("Object Size: %u / %u bytes\n",
						otc.cur_object.size.cur, otc.cur_object.size.alloc);
					printk("Writing %u bytes: %s\n", buf_idx, write_buf);

					err = bt_ots_client_write_object_data(&otc, default_conn,
									write_buf, buf_idx, 0,
									BT_OTS_OACP_WRITE_OP_MODE_NONE);
					if (err) {
						LOG_ERR("Write object failed (err %d)", err);
					} else {
						printk("Write operation initiated...\n");
					}
				}
			}

		} else if (ch == 'c' || ch == 'C') {
			/* Create new object */
			printk("\n--- Create New Object ---\n");
			printk("Enter object size in bytes (default 1024, press Enter to confirm): \n");
			printk("> ");

			char size_buf[16];
			int size_idx = 0;

			while (size_idx < sizeof(size_buf) - 1) {
				int ch_input = console_getchar();

				if (ch_input == '\r' || ch_input == '\n') {
					printk("\n");
					break;
				}

				if (ch_input == '\b' || ch_input == 0x7f) {
					if (size_idx > 0) {
						size_idx--;
						printk("\b \b");
					}
				} else if (ch_input >= '0' && ch_input <= '9') {
					size_buf[size_idx++] = ch_input;
					printk("%c", ch_input);
				}
			}
			size_buf[size_idx] = '\0';

			uint32_t obj_size = 1024; /* default */
			if (size_idx > 0) {
				obj_size = atoi(size_buf);
			}

			err = ots_client_create_object(default_conn, obj_size);
			if (err) {
				LOG_ERR("Create object failed (err %d)", err);
			}

		} else if (ch == 't' || ch == 'T') {
			/* Set object name */
			printk("\n--- Set Object Name ---\n");
			printk("Enter object name (max 63 chars, press Enter to confirm): \n");
			printk("> ");

			char name_buf[64];
			int name_idx = 0;

			while (name_idx < 63) {
				int ch_input = console_getchar();

				if (ch_input == '\r' || ch_input == '\n') {
					name_buf[name_idx] = '\0';
					printk("\n");
					break;
				}

				if (ch_input == '\b' || ch_input == 0x7F) {
					if (name_idx > 0) {
						name_idx--;
						printk("\b \b");
					}
				} else if (ch_input >= 32 && ch_input <= 126) {
					name_buf[name_idx++] = ch_input;
					printk("%c", ch_input);
				}
			}
			name_buf[name_idx] = '\0';

			if (name_idx > 0) {
				err = write_object_name(default_conn, name_buf);
				if (err) {
					LOG_ERR("Set name failed (err %d)", err);
				} else {
					printk("Object name set to: %s\n", name_buf);
				}
			} else {
				printk("Name not set (empty input)\n");
			}

		} else if (ch == 'd' || ch == 'D') {
			/* Delete current object */
			printk("\n--- Delete Object ---\n");
			printk("Delete object 0x%012llx? (y/n): ", otc.cur_object.id);

			int confirm = console_getchar();
			printk("%c\n", confirm);

			if (confirm == 'y' || confirm == 'Y') {
				err = ots_client_delete_object(default_conn);
				if (err) {
					LOG_ERR("Delete object failed (err %d)", err);
				}
			} else {
				printk("Delete cancelled.\n");
			}

		} else if (ch == 'h' || ch == 'H') {
			/* Display help menu */
			display_menu();

		} else if (ch == 'q' || ch == 'Q') {
			LOG_INF("Disconnecting...");
			if (default_conn) {
				bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			}
			break;

		} else {
			printk("Invalid command!\n");
			display_menu();
		}
	}
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void)
{
	int err;

	printk("Build: %s %s\n", __DATE__, __TIME__);
	printk("Board: %s\n", CONFIG_BOARD);
	printk("Configured BT HCI baudrate: %s\n", STR(CONFIG_BT_HCI_SYNA_BAUD_RATE));
	printk("\n=== BLE OTS Central Client - %s ===\n", CONFIG_BOARD);

	console_init();

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth enable failed (err %d)", err);
		return err;
	}

	/* Initialize OTS client callbacks */
	otc_cb.obj_selected = on_obj_selected;
	otc_cb.obj_metadata_read = on_obj_metadata_read;
	otc_cb.obj_data_read = on_obj_data_read;
	otc_cb.obj_data_written = on_obj_data_written;
	otc_cb.obj_checksum_calculated = on_obj_checksum_calculated;
	otc_cb.obj_changed = on_obj_changed;

	otc.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	otc.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	otc.cb = &otc_cb;

	err = bt_ots_client_register(&otc);
	if (err) {
		LOG_ERR("OTS client register failed (err %d)", err);
		return err;
	}

	bt_conn_cb_register(&conn_cb);

	/* Main loop: scan, select device, connect, use OTS, repeat */
	while (1) {
		do_scan();

		bool rescan = show_menu_and_connect();

		if (rescan) {
			continue;
		}

		/* Wait for connection or selection to complete */
		k_sem_take(&conn_event_sem, K_FOREVER);
		k_sem_take(&conn_event_sem, K_NO_WAIT);

		if (!is_connected) {
			printk("Connection failed. Back to scan.\n");
			continue;
		}

		show_connected_menu();

		/* Wait for disconnect */
		if (is_connected) {
			k_sem_take(&conn_event_sem, K_FOREVER);
		}
		k_sem_take(&conn_event_sem, K_NO_WAIT);

		printk("Disconnected. Back to scan.\n");
	}

	return 0;
}
