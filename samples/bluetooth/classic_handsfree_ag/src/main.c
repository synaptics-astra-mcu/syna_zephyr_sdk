/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/classic.h>
#include <zephyr/bluetooth/classic/hfp_ag.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/console/console.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(handsfree_ag);

#define MAX_SCAN_RESULTS 10
#define INQUIRY_LENGTH   8   /* 8 × 1.28 s ≈ 10 s */

/* ── HFP AG callbacks ────────────────────────────────────────────────────── */

static struct bt_hfp_ag *current_ag;
static bool slc_connected;
static bool sco_connected;
static K_SEM_DEFINE(slc_event_sem, 0, 1);

static void ag_connected(struct bt_conn *conn, struct bt_hfp_ag *ag)
{
	char addr[BT_ADDR_STR_LEN];

	bt_addr_to_str(bt_conn_get_dst_br(conn), addr, sizeof(addr));
	LOG_INF("HFP AG SLC connected: %s", addr);
	current_ag = ag;
	slc_connected = true;
	k_sem_give(&slc_event_sem);
}

static void ag_disconnected(struct bt_hfp_ag *ag)
{
	LOG_INF("HFP AG SLC disconnected");
	current_ag = NULL;
	slc_connected = false;
	sco_connected = false;
	k_sem_give(&slc_event_sem);
}

static void ag_sco_connected(struct bt_hfp_ag *ag, struct bt_conn *sco_conn)
{
	LOG_INF("SCO connected");
	sco_connected = true;
}

static void ag_sco_disconnected(struct bt_conn *sco_conn, uint8_t reason)
{
	LOG_INF("SCO disconnected (reason 0x%02x)", reason);
	sco_connected = false;
}

static int ag_get_indicator_value(struct bt_hfp_ag *ag, uint8_t *service,
				  uint8_t *strength, uint8_t *roam, uint8_t *battery)
{
	*service  = 1; /* service available */
	*strength = 3;
	*roam     = 0;
	*battery  = 5;
	return 0;
}

static struct bt_hfp_ag_cb ag_cb = {
	.connected           = ag_connected,
	.disconnected        = ag_disconnected,
	.sco_connected       = ag_sco_connected,
	.sco_disconnected    = ag_sco_disconnected,
	.get_indicator_value = ag_get_indicator_value,
};

/* ── Auth callbacks (SSP DisplayYesNo / Numeric Comparison auto-confirm) ── */

static void passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	LOG_INF("SSP passkey: %06u", passkey);
}

static void passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	LOG_INF("SSP numeric comparison: %06u — auto-confirming", passkey);
	bt_conn_auth_passkey_confirm(conn);
}

static void pairing_confirm(struct bt_conn *conn)
{
	bt_conn_auth_pairing_confirm(conn);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_STR_LEN];

	bt_addr_to_str(bt_conn_get_dst_br(conn), addr, sizeof(addr));
	LOG_WRN("SSP cancelled for %s", addr);
}

static const struct bt_conn_auth_cb auth_cb = {
	.passkey_display = passkey_display,
	.passkey_confirm = passkey_confirm,
	.pairing_confirm = pairing_confirm,
	.cancel          = auth_cancel,
};

/* ── Scan state ──────────────────────────────────────────────────────────── */

static struct bt_br_discovery_result scan_results[MAX_SCAN_RESULTS];
static size_t scan_count;
static K_SEM_DEFINE(scan_done_sem, 0, 1);

/* ── Connection state ────────────────────────────────────────────────────── */

static bool is_connected;
static K_SEM_DEFINE(conn_event_sem, 0, 1);

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static const char *eir_name(const uint8_t *eir, char *buf, size_t len)
{
	size_t i = 0;

	while (i < BT_BR_EIR_SIZE_MAX) {
		uint8_t elen = eir[i];

		if (elen == 0 || i + elen >= BT_BR_EIR_SIZE_MAX) {
			break;
		}
		uint8_t type = eir[i + 1];

		if (type == 0x08 || type == 0x09) {
			size_t nlen = elen - 1;

			if (nlen >= len) {
				nlen = len - 1;
			}
			memcpy(buf, &eir[i + 2], nlen);
			buf[nlen] = '\0';
			return buf;
		}
		i += elen + 1;
	}
	return NULL;
}

static void print_device(int idx, const struct bt_br_discovery_result *r)
{
	char addr[BT_ADDR_STR_LEN];
	char name[32];

	bt_addr_to_str(&r->addr, addr, sizeof(addr));
	const char *n = eir_name(r->eir, name, sizeof(name));

	printk("  [%d] %s  rssi=%d  %s\n",
	       idx, addr, r->rssi, n ? n : "(no name)");
}

/* ── Inquiry callbacks ───────────────────────────────────────────────────── */

static void on_inquiry_recv(const struct bt_br_discovery_result *result)
{
	char addr[BT_ADDR_STR_LEN];
	char name[32];

	bt_addr_to_str(&result->addr, addr, sizeof(addr));
	const char *n = eir_name(result->eir, name, sizeof(name));

	printk("  Found: %s  rssi=%d  %s\n",
	       addr, result->rssi, n ? n : "(no name)");
}

static void on_inquiry_timeout(const struct bt_br_discovery_result *results,
			       size_t count)
{
	scan_count = count;
	printk("Scan complete: %d device(s)\n", (int)count);
	k_sem_give(&scan_done_sem);
}

static struct bt_br_discovery_cb discovery_cb = {
	.recv    = on_inquiry_recv,
	.timeout = on_inquiry_timeout,
};

/* ── Connection callbacks ────────────────────────────────────────────────── */

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_hfp_ag *ag;
	char addr[BT_ADDR_STR_LEN];
	int ag_err;

	if (!bt_conn_is_type(conn, BT_CONN_TYPE_BR)) {
		return;
	}

	if (err) {
		LOG_WRN("ACL connect failed (err %u)", err);
		k_sem_give(&conn_event_sem);
		return;
	}

	bt_addr_to_str(bt_conn_get_dst_br(conn), addr, sizeof(addr));
	LOG_INF("ACL connected: %s — connecting HFP AG to HF channel %u...",
		addr, BT_RFCOMM_CHAN_HFP_HF);

	is_connected = true;
	bt_br_discovery_stop();
	k_sem_give(&scan_done_sem);
	k_sem_give(&conn_event_sem);

	ag_err = bt_hfp_ag_connect(conn, &ag, BT_RFCOMM_CHAN_HFP_HF);
	if (ag_err) {
		LOG_ERR("bt_hfp_ag_connect failed (%d)", ag_err);
	} else {
		LOG_INF("HFP AG SLC connecting...");
	}
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_STR_LEN];

	if (!bt_conn_is_type(conn, BT_CONN_TYPE_BR)) {
		return;
	}

	bt_addr_to_str(bt_conn_get_dst_br(conn), addr, sizeof(addr));
	LOG_INF("ACL disconnected: %s (reason 0x%02x)", addr, reason);

	is_connected = false;
	slc_connected = false;
	sco_connected = false;
	current_ag = NULL;
	/* unblock main if it is waiting for SLC */
	k_sem_give(&slc_event_sem);
	k_sem_give(&conn_event_sem);
}

static struct bt_conn_cb conn_cb = {
	.connected    = on_connected,
	.disconnected = on_disconnected,
};

/* ── Connected-state SCO menu ────────────────────────────────────────────── */

static void show_connected_menu(void)
{
	printk("\n=== HFP SLC connected ===\n");
	printk("[a] Create SCO audio\n[d] Disconnect HFP\n");
	printk("(If remote disconnects, press any key to return)\n");

	while (is_connected && slc_connected) {
		printk("Choice: ");

		int ch = console_getchar();

		if (ch == '\r' || ch == '\n') {
			continue;
		}

		printk("%c\n", ch);

		if (!is_connected || !slc_connected) {
			break;
		}

		if (ch == 'a' || ch == 'A') {
			if (sco_connected) {
				printk("SCO already connected.\n");
				continue;
			}
			int err = bt_hfp_ag_audio_connect(current_ag,
							  BT_HFP_AG_CODEC_CVSD);
			if (err) {
				LOG_ERR("bt_hfp_ag_audio_connect failed (%d)", err);
			} else {
				printk("SCO connecting...\n");
			}
		} else if (ch == 'd' || ch == 'D') {
			int err = bt_hfp_ag_disconnect(current_ag);

			if (err) {
				LOG_ERR("bt_hfp_ag_disconnect failed (%d)", err);
			}
			break;
		} else {
			printk("Invalid — [a] SCO  [d] Disconnect\n");
		}
	}
}

/* ── Interactive scan + connect loop ─────────────────────────────────────── */

static void do_scan(void)
{
	const struct bt_br_discovery_param param = {
		.length  = INQUIRY_LENGTH,
		.limited = false,
	};
	int err;

	scan_count = 0;
	memset(scan_results, 0, sizeof(scan_results));

	printk("\n--- Scanning for BT devices (%d s) ---\n",
	       INQUIRY_LENGTH * 128 / 100);

	err = bt_br_discovery_start(&param, scan_results, MAX_SCAN_RESULTS);
	if (err) {
		LOG_ERR("Inquiry start failed (err %d)", err);
		return;
	}

	k_sem_take(&scan_done_sem, K_FOREVER);
}

static bool show_menu_and_connect(void)
{
	printk("\n--- Device list (%d found) ---\n", (int)scan_count);
	for (size_t i = 0; i < scan_count; i++) {
		print_device((int)i, &scan_results[i]);
	}
	printk("  [r] Rescan\n");

	for (;;) {
		printk("Choice: ");

		int ch = console_getchar();

		if (ch == '\r' || ch == '\n') {
			continue;
		}

		printk("%c\n", ch);

		if (ch == 'r' || ch == 'R') {
			return true;
		}

		if (ch < '0' || ch > '9') {
			printk("Invalid — enter 0-%d or r.\n",
			       scan_count > 0 ? (int)scan_count - 1 : 0);
			continue;
		}

		int idx = ch - '0';

		if (scan_count == 0 || (size_t)idx >= scan_count) {
			printk("Out of range (0-%d).\n",
			       scan_count > 0 ? (int)scan_count - 1 : 0);
			continue;
		}

		char addr[BT_ADDR_STR_LEN];

		bt_addr_to_str(&scan_results[idx].addr, addr, sizeof(addr));
		printk("Connecting to %s...\n", addr);

		struct bt_conn *conn = bt_conn_create_br(&scan_results[idx].addr,
							  BT_BR_CONN_PARAM_DEFAULT);

		if (!conn) {
			LOG_ERR("bt_conn_create_br failed");
			continue;
		}

		bt_conn_unref(conn);
		return false;
	}
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void)
{
	int err;

	printk("Starting HFP AG - Synaptics SR110\n");

	console_init();

	err = bt_conn_auth_cb_register(&auth_cb);
	if (err) {
		LOG_ERR("bt_conn_auth_cb_register failed (%d)", err);
		return err;
	}

	err = bt_hfp_ag_register(&ag_cb);
	if (err) {
		LOG_ERR("bt_hfp_ag_register failed (%d)", err);
		return err;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable failed (%d)", err);
		return err;
	}

	bt_conn_cb_register(&conn_cb);
	bt_br_discovery_cb_register(&discovery_cb);
	bt_br_set_connectable(false, NULL);

	do_scan();

	while (1) {
		if (is_connected) {
			/* wait for SLC to come up (or ACL to drop) */
			printk("ACL up, waiting for HFP SLC...\n");
			k_sem_take(&slc_event_sem, K_FOREVER);
			/* drain any extra count from on_disconnected */
			k_sem_take(&slc_event_sem, K_NO_WAIT);

			if (slc_connected) {
				show_connected_menu();
			}

			/* wait for ACL disconnect */
			if (is_connected) {
				k_sem_take(&conn_event_sem, K_FOREVER);
			}
			/* drain extra conn_event_sem counts */
			k_sem_take(&conn_event_sem, K_NO_WAIT);
			printk("Disconnected. Back to device list.\n");
		}

		/* drain stale slc_event_sem counts left by previous session
		 * before attempting a new connection */
		while (k_sem_take(&slc_event_sem, K_NO_WAIT) == 0) {
		}

		bool rescan = show_menu_and_connect();

		if (rescan) {
			do_scan();
			continue;
		}

		k_sem_take(&conn_event_sem, K_FOREVER);

		if (!is_connected) {
			printk("Connection failed. Back to menu.\n");
		}
	}

	return 0;
}
