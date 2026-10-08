/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/audio/tbs.h>
#include <zephyr/bluetooth/audio/ccp.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>

LOG_MODULE_REGISTER(ccp_call_control_client, CONFIG_LOG_DEFAULT_LEVEL);

#define SEM_TIMEOUT K_SECONDS(10)

enum start_mode {
	START_MODE_SCAN = 0,
	START_MODE_ADV = 1,
};

static struct bt_conn *peer_conn;
static bool is_advertising;
/* call_control_client is not static as it is used for testing purposes */
struct bt_ccp_call_control_client *call_control_client;
static struct bt_ccp_call_control_client_bearers client_bearers;

static K_SEM_DEFINE(sem_conn_state_change, 0, 1);
static K_SEM_DEFINE(sem_security_updated, 0, 1);
static K_SEM_DEFINE(sem_ccp_action_completed, 0, 1);

static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err != 0) {
		LOG_ERR("Connection failed (err 0x%02x)", err);
		return;
	}

	(void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected: %s", addr);

	if (peer_conn == NULL) {
		peer_conn = bt_conn_ref(conn);
	}

	k_sem_give(&sem_conn_state_change);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != peer_conn) {
		return;
	}

	(void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

	bt_conn_unref(peer_conn);
	peer_conn = NULL;
	call_control_client = NULL;
	memset(&client_bearers, 0, sizeof(client_bearers));
	k_sem_give(&sem_conn_state_change);
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(level);

	if (err == 0) {
		k_sem_give(&sem_security_updated);
	} else {
		LOG_ERR("Failed to set security level: %s(%u)", bt_security_err_to_str(err), err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.security_changed = security_changed_cb,
};

static bool check_gtbs_support(struct bt_data *data, void *user_data)
{
	struct net_buf_simple svc_data;
	bool *connect = user_data;
	const struct bt_uuid *uuid;
	uint16_t uuid_val;

	if (data->type != BT_DATA_SVC_DATA16) {
		return true;
	}

	if (data->data_len < sizeof(uuid_val)) {
		LOG_WRN("AD invalid size %u", data->data_len);
		return true;
	}

	net_buf_simple_init_with_data(&svc_data, (void *)data->data, data->data_len);

	uuid_val = net_buf_simple_pull_le16(&svc_data);
	uuid = BT_UUID_DECLARE_16(sys_le16_to_cpu(uuid_val));
	if (bt_uuid_cmp(uuid, BT_UUID_GTBS) != 0) {
		return true;
	}

	*connect = true;

	return false;
}

static void scan_recv_cb(const struct bt_le_scan_recv_info *info, struct net_buf_simple *ad_data)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	bool connect = false;

	if (peer_conn != NULL) {
		return;
	}

	/* Accept both extended and legacy connectable advertising */
	if ((info->adv_props & BT_GAP_ADV_PROP_CONNECTABLE) == 0 || info->rssi < -70) {
		return;
	}

	(void)bt_addr_le_to_str(info->addr, addr_str, sizeof(addr_str));
	LOG_INF("Connectable device found: %s (RSSI %d)", addr_str, info->rssi);

	bt_data_parse(ad_data, check_gtbs_support, &connect);

	if (connect) {
		int err;

		err = bt_le_scan_stop();
		if (err != 0) {
			LOG_ERR("Scanning failed to stop (err %d)", err);
			return;
		}

		LOG_INF("Connecting to found CCP server");
		err = bt_conn_le_create(info->addr, BT_CONN_LE_CREATE_CONN,
					BT_LE_CONN_PARAM_DEFAULT, &peer_conn);
		if (err != 0) {
			LOG_ERR("Conn create failed: %d", err);
		}
	}
}

static int wait_and_secure_peer(void)
{
	int err;

	err = k_sem_take(&sem_conn_state_change, K_FOREVER);
	if (err != 0) {
		LOG_ERR("Failed waiting for connection (err %d)", err);
		return err;
	}

	err = bt_conn_set_security(peer_conn, BT_SECURITY_L2);
	if (err != 0) {
		LOG_ERR("Failed to set security (err %d)", err);
		return err;
	}

	err = k_sem_take(&sem_security_updated, SEM_TIMEOUT);
	if (err != 0) {
		LOG_ERR("Failed waiting for security update (err %d)", err);
		return err;
	}

	LOG_INF("Security successfully updated");

	return 0;
}

static int scan_and_connect(void)
{
	int err;

	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, NULL);
	if (err != 0) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return err;
	}

	LOG_INF("Scanning started");

	return wait_and_secure_peer();
}

static int advertise_and_wait_connection(void)
{
	int err;

	/* Use legacy connectable advertising so Android BT Settings can find SR110 */
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err != 0) {
		LOG_ERR("Failed to start advertising: %d", err);
		return err;
	}

	is_advertising = true;
	LOG_INF("Advertising started as \"%s\", waiting for peer connection",
		CONFIG_BT_DEVICE_NAME);

	return wait_and_secure_peer();
}

static void ccp_call_control_client_discover_cb(struct bt_ccp_call_control_client *client, int err,
						struct bt_ccp_call_control_client_bearers *bearers,
						void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (err != 0) {
		LOG_ERR("Discovery failed: %d", err);
		return;
	}

	LOG_INF("Discovery completed with %s%u TBS bearers",
		bearers->gtbs_bearer != NULL ? "GTBS and " : "", bearers->tbs_count);

	memcpy(&client_bearers, bearers, sizeof(client_bearers));

	k_sem_give(&sem_ccp_action_completed);
}

#if defined(CONFIG_BT_TBS_CLIENT_BEARER_PROVIDER_NAME)
static void ccp_call_control_client_read_bearer_provider_name_cb(
	struct bt_ccp_call_control_client_bearer *bearer, int err, const char *name,
	void *user_data)
{
	ARG_UNUSED(user_data);

	if (err != 0) {
		LOG_ERR("Failed to read bearer %p provider name: %d", (void *)bearer, err);
		return;
	}

	LOG_INF("Bearer %p provider name: %s", (void *)bearer, name);

	k_sem_give(&sem_ccp_action_completed);
}
#endif

static int reset_ccp_call_control_client(void)
{
	int err;

	LOG_INF("Resetting");

	if (peer_conn != NULL) {
		err = bt_conn_disconnect(peer_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (err != 0) {
			return err;
		}

		err = k_sem_take(&sem_conn_state_change, K_FOREVER);
		if (err != 0) {
			LOG_ERR("Failed to take sem_conn_state_change: %d", err);
			return err;
		}
	}

	err = bt_le_scan_stop();
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("Scanning failed to stop (err %d)", err);
		return err;
	}

	if (is_advertising) {
		err = bt_le_adv_stop();
		if (err != 0 && err != -EALREADY) {
			LOG_ERR("Failed to stop advertising (err %d)", err);
			return err;
		}
		is_advertising = false;
	}

	k_sem_reset(&sem_conn_state_change);
	k_sem_reset(&sem_security_updated);

	return 0;
}

static int discover_services(void)
{
	int err;

	LOG_INF("Discovering GTBS and TBS");

	err = bt_ccp_call_control_client_discover(peer_conn, &call_control_client);
	if (err != 0) {
		LOG_ERR("Failed to discover: %d", err);
		return err;
	}

	err = k_sem_take(&sem_ccp_action_completed, SEM_TIMEOUT);
	if (err != 0) {
		LOG_ERR("Failed to take sem_ccp_action_completed: %d", err);
		return err;
	}

	return 0;
}

static int read_bearer_name(struct bt_ccp_call_control_client_bearer *bearer)
{
	int err;

	err = bt_ccp_call_control_client_read_bearer_provider_name(bearer);
	if (err != 0) {
		return err;
	}

	err = k_sem_take(&sem_ccp_action_completed, SEM_TIMEOUT);
	if (err != 0) {
		LOG_ERR("Failed to take sem_ccp_action_completed: %d", err);
		return err;
	}

	return 0;
}

static int read_bearer_names(void)
{
	int err;

#if defined(CONFIG_BT_TBS_CLIENT_GTBS)
	err = read_bearer_name(client_bearers.gtbs_bearer);
	if (err != 0) {
		LOG_ERR("Failed to read name for GTBS bearer: %d", err);
		return err;
	}
#endif

#if defined(CONFIG_BT_TBS_CLIENT_TBS)
	for (size_t i = 0; i < client_bearers.tbs_count; i++) {
		err = read_bearer_name(client_bearers.tbs_bearers[i]);
		if (err != 0) {
			LOG_ERR("Failed to read name for bearer[%zu]: %d", i, err);
			return err;
		}
	}
#endif

	return 0;
}

static int init_ccp_call_control_client(void)
{
	static struct bt_ccp_call_control_client_cb ccp_call_control_client_cbs = {
		.discover = ccp_call_control_client_discover_cb,
#if defined(CONFIG_BT_TBS_CLIENT_BEARER_PROVIDER_NAME)
		.bearer_provider_name = ccp_call_control_client_read_bearer_provider_name_cb
#endif
	};
	static struct bt_le_scan_cb scan_cbs = {
		.recv = scan_recv_cb,
	};
	int err;

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("Bluetooth enable failed (err %d)", err);
		return err;
	}

	LOG_DBG("Bluetooth initialized");

	err = bt_le_scan_cb_register(&scan_cbs);
	if (err != 0) {
		LOG_ERR("Scan callback registration failed (err %d)", err);
		return err;
	}

	err = bt_ccp_call_control_client_register_cb(&ccp_call_control_client_cbs);
	if (err != 0) {
		LOG_ERR("CCP callback registration failed (err %d)", err);
		return err;
	}

	return 0;
}

static enum start_mode choose_start_mode(void)
{
	while (true) {
		char *line;

		printk("\n=== CCP client start mode ===\n");
		printk("1) Scan and connect to CCP server\n");
		printk("2) Start connectable advertising (wait phone connect)\n");
		printk("Select [1/2]: ");

		line = console_getline();
		if (line == NULL) {
			continue;
		}

		if (line[0] == '1') {
			return START_MODE_SCAN;
		}

		if (line[0] == '2') {
			return START_MODE_ADV;
		}

		printk("Invalid input: %s\n", line);
	}
}

int main(void)
{
	enum start_mode mode;
	int err;

	console_getline_init();

	err = init_ccp_call_control_client();
	if (err != 0) {
		return 0;
	}

	LOG_INF("CCP Call Control Client initialized");

	mode = choose_start_mode();
	if (mode == START_MODE_SCAN) {
		LOG_INF("Selected mode: scan and connect");
	} else {
		LOG_INF("Selected mode: connectable advertising");
	}

	while (true) {
		err = reset_ccp_call_control_client();
		if (err != 0) {
			break;
		}

		if (mode == START_MODE_SCAN) {
			err = scan_and_connect();
		} else {
			err = advertise_and_wait_connection();
		}

		if (err != 0) {
			continue;
		}

		err = discover_services();
		if (err != 0) {
			continue;
		}

		if (IS_ENABLED(CONFIG_BT_TBS_CLIENT_BEARER_PROVIDER_NAME)) {
			err = read_bearer_names();
			if (err != 0) {
				continue;
			}
		}

		err = k_sem_take(&sem_conn_state_change, K_FOREVER);
		if (err != 0) {
			LOG_ERR("Failed to take sem_conn_state_change: err %d", err);
			break;
		}
	}

	return 0;
}
