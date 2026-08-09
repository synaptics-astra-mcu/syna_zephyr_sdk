/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/classic.h>
#include <zephyr/bluetooth/classic/hfp_hf.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(handsfree);

/* ── HFP HF callbacks ────────────────────────────────────────────────────── */

static void hf_connected(struct bt_conn *conn, struct bt_hfp_hf *hf)
{
	char addr[BT_ADDR_STR_LEN];

	bt_addr_to_str(bt_conn_get_dst_br(conn), addr, sizeof(addr));
	LOG_INF("HFP HF connected: %s", addr);
}

static void hf_disconnected(struct bt_hfp_hf *hf)
{
	LOG_INF("HFP HF disconnected");
}

static void hf_sco_connected(struct bt_hfp_hf *hf, struct bt_conn *sco_conn)
{
	LOG_INF("SCO connected");
}

static void hf_sco_disconnected(struct bt_conn *sco_conn, uint8_t reason)
{
	LOG_INF("SCO disconnected (reason 0x%02x)", reason);
}

static void hf_service(struct bt_hfp_hf *hf, uint32_t value)
{
	LOG_INF("Service indicator: %u", value);
}

static void hf_signal(struct bt_hfp_hf *hf, uint32_t value)
{
	LOG_INF("Signal: %u", value);
}

static void hf_roam(struct bt_hfp_hf *hf, uint32_t value)
{
	LOG_INF("Roaming: %u", value);
}

static void hf_battery(struct bt_hfp_hf *hf, uint32_t value)
{
	LOG_INF("Battery: %u", value);
}

static void hf_incoming(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)
{
	LOG_INF("Incoming call %p", call);
}

static void hf_outgoing(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)
{
	LOG_INF("Outgoing call %p", call);
}

static void hf_remote_ringing(struct bt_hfp_hf_call *call)
{
	LOG_INF("Remote ringing %p", call);
}

static void hf_incoming_held(struct bt_hfp_hf_call *call)
{
	LOG_INF("Incoming held %p", call);
}

static void hf_accept(struct bt_hfp_hf_call *call)
{
	LOG_INF("Call accepted %p", call);
}

static void hf_reject(struct bt_hfp_hf_call *call)
{
	LOG_INF("Call rejected %p", call);
}

static void hf_terminate(struct bt_hfp_hf_call *call)
{
	LOG_INF("Call terminated %p", call);
}

static void hf_held(struct bt_hfp_hf_call *call)
{
	LOG_INF("Call held %p", call);
}

static void hf_retrieve(struct bt_hfp_hf_call *call)
{
	LOG_INF("Call retrieved %p", call);
}

static void hf_ring_indication(struct bt_hfp_hf_call *call)
{
	LOG_INF("Ring! call=%p", call);
}

static struct bt_hfp_hf_cb hf_cb = {
	.connected        = hf_connected,
	.disconnected     = hf_disconnected,
	.sco_connected    = hf_sco_connected,
	.sco_disconnected = hf_sco_disconnected,
	.service          = hf_service,
	.signal           = hf_signal,
	.roam             = hf_roam,
	.battery          = hf_battery,
	.incoming         = hf_incoming,
	.outgoing         = hf_outgoing,
	.remote_ringing   = hf_remote_ringing,
	.incoming_held    = hf_incoming_held,
	.accept           = hf_accept,
	.reject           = hf_reject,
	.terminate        = hf_terminate,
	.held             = hf_held,
	.retrieve         = hf_retrieve,
	.ring_indication  = hf_ring_indication,
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

/* ── Connection callbacks ────────────────────────────────────────────────── */

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_hfp_hf *hf;
	char addr[BT_ADDR_STR_LEN];
	int hfp_err;

	if (!bt_conn_is_type(conn, BT_CONN_TYPE_BR)) {
		return;
	}
	if (err) {
		LOG_WRN("ACL connect failed (err %u)", err);
		return;
	}

	bt_addr_to_str(bt_conn_get_dst_br(conn), addr, sizeof(addr));
	LOG_INF("ACL connected: %s — connecting HFP HF to AG channel %u...",
		addr, BT_RFCOMM_CHAN_HFP_AG);

	/*
	 * Connect immediately before BSA can initiate pairing, which would
	 * set BT_CONN_BR_PAIRING and cause bt_conn_set_security to fail.
	 * hfp_hf_create internally starts its own SDP discovery for AG features.
	 */
	hfp_err = bt_hfp_hf_connect(conn, &hf, BT_RFCOMM_CHAN_HFP_AG);
	if (hfp_err) {
		LOG_ERR("bt_hfp_hf_connect failed (%d)", hfp_err);
	} else {
		LOG_INF("HFP HF SLC connecting (security pending)...");
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
}

static struct bt_conn_cb conn_cb = {
	.connected    = on_connected,
	.disconnected = on_disconnected,
};

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void)
{
	int err;

	LOG_INF("SR110 HFP Handsfree Unit starting");

	err = bt_conn_auth_cb_register(&auth_cb);
	if (err) {
		LOG_ERR("bt_conn_auth_cb_register failed (%d)", err);
		return err;
	}

	err = bt_hfp_hf_register(&hf_cb);
	if (err) {
		LOG_ERR("HFP HF register failed (%d)", err);
		return err;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable failed (%d)", err);
		return err;
	}

	bt_conn_cb_register(&conn_cb);

	/* Enable page scan (connectable) + inquiry scan (discoverable) */
	err = bt_br_set_connectable(true, NULL);
	if (err) {
		LOG_ERR("set_connectable failed (%d)", err);
	}

	err = bt_br_set_discoverable(true, false);
	if (err) {
		LOG_ERR("set_discoverable failed (%d)", err);
	}

	LOG_INF("Discoverable and connectable — waiting for AG...");

	while (1) {
		k_sleep(K_SECONDS(10));
	}

	return 0;
}
