/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/classic.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(a2dp_sink);

#define A2DP_VERSION 0x0103

static struct bt_a2dp *default_a2dp;
BT_A2DP_SBC_SINK_EP_DEFAULT(sbc_sink_ep);
static struct bt_a2dp_stream sbc_stream;

static struct bt_sdp_attribute a2dp_sink_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST({
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_AUDIO_SINK_SVCLASS)
		}, )
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROFILE_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
		BT_SDP_DATA_ELEM_LIST({
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST({
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_ADVANCED_AUDIO_SVCLASS)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(A2DP_VERSION)
			}, )
		}, )
	),
	BT_SDP_SUPPORTED_FEATURES(0x0001U),
};

static struct bt_sdp_record a2dp_sink_rec = BT_SDP_RECORD(a2dp_sink_attrs);

static const char *br_addr_str(struct bt_conn *conn, char *buf, size_t len)
{
	const bt_addr_t *addr = bt_conn_get_dst_br(conn);

	if (addr == NULL) {
		snprintk(buf, len, "<unknown>");
		return buf;
	}

	(void)bt_addr_to_str(addr, buf, len);
	return buf;
}

static void sbc_stream_configured(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP stream configured");
}

static void sbc_stream_established(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP stream established");
}

static void sbc_stream_released(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP stream released");
}

static void sbc_stream_started(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP stream started - receiving audio data");
}

static void sbc_stream_suspended(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP stream suspended");
}

static void sbc_stream_recv(struct bt_a2dp_stream *stream, struct net_buf *buf, uint16_t seq_num,
			    uint32_t ts)
{
	ARG_UNUSED(stream);
	ARG_UNUSED(seq_num);
	ARG_UNUSED(ts);

	LOG_DBG("Received SBC frame: %d bytes, seq=%u, ts=%u", buf->len, seq_num, ts);
	net_buf_pull(buf, buf->len);
}

static struct bt_a2dp_stream_ops stream_ops = {
	.configured = sbc_stream_configured,
	.established = sbc_stream_established,
	.released = sbc_stream_released,
	.started = sbc_stream_started,
	.suspended = sbc_stream_suspended,
	.recv = sbc_stream_recv,
};

static void app_a2dp_connected(struct bt_a2dp *a2dp, int err)
{
	if (err == 0) {
		default_a2dp = a2dp;
		LOG_INF("A2DP signaling connected - waiting for Source to initiate stream");
	} else {
		LOG_WRN("A2DP signaling connect failed (%d)", err);
	}
}

static void app_a2dp_disconnected(struct bt_a2dp *a2dp)
{
	ARG_UNUSED(a2dp);
	default_a2dp = NULL;
	LOG_INF("A2DP signaling disconnected");
}

static int app_a2dp_config_req(struct bt_a2dp *a2dp, struct bt_a2dp_ep *ep,
			       struct bt_a2dp_codec_cfg *codec_cfg, struct bt_a2dp_stream **stream,
			       uint8_t *rsp_err_code)
{
	ARG_UNUSED(a2dp);
	ARG_UNUSED(ep);
	ARG_UNUSED(codec_cfg);

	bt_a2dp_stream_cb_register(&sbc_stream, &stream_ops);
	*stream = &sbc_stream;
	*rsp_err_code = 0;
	LOG_INF("A2DP config request accepted");

	return 0;
}

static int app_a2dp_establish_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	ARG_UNUSED(stream);
	*rsp_err_code = 0;
	LOG_INF("A2DP establish request accepted");
	return 0;
}

static int app_a2dp_start_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	ARG_UNUSED(stream);
	*rsp_err_code = 0;
	LOG_INF("A2DP start request accepted");
	return 0;
}

static int app_a2dp_suspend_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	ARG_UNUSED(stream);
	*rsp_err_code = 0;
	LOG_INF("A2DP suspend request accepted");
	return 0;
}

static int app_a2dp_release_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	ARG_UNUSED(stream);
	*rsp_err_code = 0;
	LOG_INF("A2DP release request accepted");
	return 0;
}

static int app_a2dp_abort_req(struct bt_a2dp_stream *stream, uint8_t *rsp_err_code)
{
	ARG_UNUSED(stream);
	*rsp_err_code = 0;
	LOG_INF("A2DP abort request accepted");
	return 0;
}

static void app_a2dp_config_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP config response err=0x%02x", rsp_err_code);
}

static void app_a2dp_establish_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP establish response err=0x%02x", rsp_err_code);
}

static void app_a2dp_start_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP start response err=0x%02x", rsp_err_code);
}

static void app_a2dp_suspend_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP suspend response err=0x%02x", rsp_err_code);
}

static void app_a2dp_release_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP release response err=0x%02x", rsp_err_code);
}

static void app_a2dp_abort_rsp(struct bt_a2dp_stream *stream, uint8_t rsp_err_code)
{
	ARG_UNUSED(stream);
	LOG_INF("A2DP abort response err=0x%02x", rsp_err_code);
}

static struct bt_a2dp_cb a2dp_cb = {
	.connected = app_a2dp_connected,
	.disconnected = app_a2dp_disconnected,
	.config_req = app_a2dp_config_req,
	.establish_req = app_a2dp_establish_req,
	.start_req = app_a2dp_start_req,
	.suspend_req = app_a2dp_suspend_req,
	.release_req = app_a2dp_release_req,
	.abort_req = app_a2dp_abort_req,
	.config_rsp = app_a2dp_config_rsp,
	.establish_rsp = app_a2dp_establish_rsp,
	.start_rsp = app_a2dp_start_rsp,
	.suspend_rsp = app_a2dp_suspend_rsp,
	.release_rsp = app_a2dp_release_rsp,
	.abort_rsp = app_a2dp_abort_rsp,
};

static void br_conn_connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_STR_LEN];

	if (!bt_conn_is_type(conn, BT_CONN_TYPE_BR)) {
		return;
	}

	if (err != 0U) {
		LOG_WRN("BR/EDR ACL connect failed (%u), peer %s", err,
			br_addr_str(conn, addr, sizeof(addr)));
		return;
	}

	LOG_INF("BR/EDR ACL connected: %s", br_addr_str(conn, addr, sizeof(addr)));
}

static void br_conn_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_STR_LEN];

	if (!bt_conn_is_type(conn, BT_CONN_TYPE_BR)) {
		return;
	}

	LOG_INF("BR/EDR ACL disconnected: %s (reason 0x%02x)",
		br_addr_str(conn, addr, sizeof(addr)), reason);
}

static void br_conn_security_changed(struct bt_conn *conn, bt_security_t level,
				     enum bt_security_err err)
{
	char addr[BT_ADDR_STR_LEN];

	if (!bt_conn_is_type(conn, BT_CONN_TYPE_BR)) {
		return;
	}

	LOG_INF("BR/EDR security changed: %s level %u err %d",
		br_addr_str(conn, addr, sizeof(addr)), level, err);

	LOG_INF("Waiting for A2DP Source to initiate AVDTP signaling");
}

static struct bt_conn_cb conn_cb = {
	.connected = br_conn_connected,
	.disconnected = br_conn_disconnected,
	.security_changed = br_conn_security_changed,
};

static void auth_pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_STR_LEN];

	if (!bt_conn_is_type(conn, BT_CONN_TYPE_BR)) {
		return;
	}

	LOG_INF("Pairing complete: %s bonded=%d", br_addr_str(conn, addr, sizeof(addr)), bonded);
}

static void auth_pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_STR_LEN];

	if (!bt_conn_is_type(conn, BT_CONN_TYPE_BR)) {
		return;
	}

	LOG_WRN("Pairing failed: %s reason=%d", br_addr_str(conn, addr, sizeof(addr)), reason);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = auth_pairing_complete,
	.pairing_failed = auth_pairing_failed,
};

static int bt_ready(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	LOG_INF("Bluetooth enabled");

	{
		bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
		size_t count = ARRAY_SIZE(addrs);
		char addr_str[BT_ADDR_LE_STR_LEN];

		bt_id_get(addrs, &count);
		if (count > 0) {
			bt_addr_le_to_str(&addrs[0], addr_str, sizeof(addr_str));
			LOG_INF("Local BD_ADDR: %s", addr_str);
		}
	}

	bt_conn_cb_register(&conn_cb);
	bt_conn_auth_info_cb_register(&auth_info_cb);
	LOG_INF("Connection diagnostic callbacks registered");

	bt_sdp_register_service(&a2dp_sink_rec);
	LOG_INF("A2DP SDP record registered");

	err = bt_a2dp_register_ep(&sbc_sink_ep, BT_AVDTP_AUDIO, BT_AVDTP_SINK);
	if (err) {
		LOG_ERR("A2DP endpoint register failed (err %d)", err);
		return err;
	}

	err = bt_a2dp_register_cb(&a2dp_cb);
	if (err) {
		LOG_ERR("A2DP callback register failed (err %d)", err);
		return err;
	}

	LOG_INF("A2DP sink endpoint registered");

	err = bt_br_set_connectable(true, NULL);
	if (err) {
		LOG_ERR("Failed to enable BR/EDR connectable (err %d)", err);
		return err;
	}

	err = bt_br_set_discoverable(true, false);
	if (err) {
		LOG_ERR("Failed to enable BR/EDR discoverable (err %d)", err);
		return err;
	}

	LOG_INF("BR/EDR inquiry/page scan enabled");
	return 0;
}

int main(void)
{
	int err;

	LOG_INF("Starting Classic A2DP Sink Sample for SR110");

	err = bt_ready();
	if (err) {
		return err;
	}

	LOG_INF("Waiting for A2DP Source connection...");
	while (1) {
		k_sleep(K_SECONDS(10));
	}

	return 0;
}
