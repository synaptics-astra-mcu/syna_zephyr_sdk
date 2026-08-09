/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <zephyr/autoconf.h>
#include <zephyr/device.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/assigned_numbers.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/audio/bap_lc3_preset.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/byteorder.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/crypto.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/console/console.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/types.h>

#include "stream_tx.h"


/* ═══════════════════════════════════════════════════════════════════════════
 * UNICAST CLIENT
 * ═══════════════════════════════════════════════════════════════════════════ */

static void start_scan(void);

#define MAX_FOUND_DEVICES 16

struct found_device {
	bt_addr_le_t addr;
	int8_t       rssi;
	char         name[32];
};

struct device_name_ctx {
	char *name;
	size_t len;
};

static struct found_device found_devices[MAX_FOUND_DEVICES];
static int found_count;
static bool scan_done;

#if defined(CONFIG_BT_AUDIO_RX)
uint64_t unicast_audio_recv_ctr;
#endif

static struct bt_bap_unicast_client_cb unicast_client_cbs;
static struct bt_conn *default_conn;
static struct bt_bap_unicast_group *unicast_group;
static atomic_t streams_connecting;
static atomic_t last_connect_failed;

static struct audio_sink {
	struct bt_bap_ep *ep;
	uint16_t seq_num;
} sinks[CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT];

static struct bt_bap_stream streams[CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT];
static size_t configured_sink_stream_count;
#define configured_source_stream_count 0
#define configured_stream_count (configured_sink_stream_count)

static struct bt_bap_lc3_preset sink_codec_cfg[CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT] = {
	BT_BAP_LC3_UNICAST_PRESET_16_2_1(BT_AUDIO_LOCATION_FRONT_LEFT,
					  BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED),
#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT > 1
	BT_BAP_LC3_UNICAST_PRESET_16_2_1(BT_AUDIO_LOCATION_FRONT_RIGHT,
					  BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED),
#endif
};

static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_disconnected, 0, 1);
static K_SEM_DEFINE(sem_mtu_exchanged, 0, 1);
static K_SEM_DEFINE(sem_security_updated, 0, 1);
static K_SEM_DEFINE(sem_sinks_discovered, 0, 1);
static K_SEM_DEFINE(sem_stream_configured, 0, 1);
static K_SEM_DEFINE(sem_stream_qos, 0,
		    ARRAY_SIZE(sinks));
static K_SEM_DEFINE(sem_stream_enabled, 0, 1);
static K_SEM_DEFINE(sem_stream_started, 0, CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT);
static K_SEM_DEFINE(sem_stream_connected, 0, CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT);
static K_SEM_DEFINE(sem_stream_connect_failed, 0, CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT);
static K_SEM_DEFINE(sem_broadcast_launch, 0, 1);
static const struct device *const console_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void print_hex(const uint8_t *ptr, size_t len)
{
	while (len-- != 0) {
		printk("%02x", *ptr++);
	}
}

static bool print_cb(struct bt_data *data, void *user_data)
{
	const char *str = (const char *)user_data;

	printk("%s: type 0x%02x value_len %u\n", str, data->type, data->data_len);
	print_hex(data->data, data->data_len);
	printk("\n");
	return true;
}

static void print_codec_cap(const struct bt_audio_codec_cap *codec_cap)
{
	printk("codec id 0x%02x cid 0x%04x vid 0x%04x count %u\n",
	       codec_cap->id, codec_cap->cid, codec_cap->vid, codec_cap->data_len);

	if (codec_cap->id == BT_HCI_CODING_FORMAT_LC3) {
		bt_audio_data_parse(codec_cap->data, codec_cap->data_len, print_cb, "data");
	} else {
		printk("data: ");
		print_hex(codec_cap->data, codec_cap->data_len);
		printk("\n");
	}

	bt_audio_data_parse(codec_cap->meta, codec_cap->meta_len, print_cb, "meta");
}

static bool device_name_cb(struct bt_data *data, void *user_data)
{
	struct device_name_ctx *ctx = user_data;
	size_t copy_len;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_BROADCAST_NAME:
		copy_len = MIN(data->data_len, ctx->len - 1U);
		memcpy(ctx->name, data->data, copy_len);
		ctx->name[copy_len] = '\0';
		return false;
	default:
		return true;
	}
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	struct device_name_ctx name_ctx = {
		.name = found_count < MAX_FOUND_DEVICES ? found_devices[found_count].name : NULL,
		.len = found_count < MAX_FOUND_DEVICES ? sizeof(found_devices[found_count].name) : 0U,
	};

	if (scan_done || default_conn != NULL) {
		return;
	}

	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
	    type != BT_GAP_ADV_TYPE_EXT_ADV) {
		return;
	}

	for (int i = 0; i < found_count; i++) {
		if (bt_addr_le_eq(&found_devices[i].addr, addr)) {
			found_devices[i].rssi = rssi;
			if (name_ctx.name != NULL) {
				memset(name_ctx.name, 0, name_ctx.len);
				bt_data_parse(ad, device_name_cb, &name_ctx);
				if (name_ctx.name[0] == '\0') {
					strncpy(name_ctx.name, "unknown", name_ctx.len - 1U);
				}
			}
			return;
		}
	}

	if (found_count >= MAX_FOUND_DEVICES) {
		return;
	}

	bt_addr_le_copy(&found_devices[found_count].addr, addr);
	found_devices[found_count].rssi = rssi;
	memset(found_devices[found_count].name, 0, sizeof(found_devices[found_count].name));
	bt_data_parse(ad, device_name_cb, &name_ctx);
	if (found_devices[found_count].name[0] == '\0') {
		strncpy(found_devices[found_count].name, "unknown",
			sizeof(found_devices[found_count].name) - 1U);
	}
	found_count++;
}

static void start_scan(void)
{
	int err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);

	if (err != 0) {
		printk("Scanning failed to start (err %d)\n", err);
	} else {
		printk("Scanning started\n");
	}
}

static int wait_menu_key(void)
{
	uint8_t ch;

	while (uart_poll_in(console_uart, &ch) != 0) {
		k_sleep(K_MSEC(20));
	}

	return (int)ch;
}

static int menu_choice_from_key(int ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'a' && ch <= 'f') {
		return 10 + (ch - 'a');
	}
	if (ch >= 'A' && ch <= 'F') {
		return 10 + (ch - 'A');
	}

	return -1;
}

static void stream_configured(struct bt_bap_stream *stream, const struct bt_bap_qos_cfg_pref *pref)
{
	printk("Audio stream %p configured\n", stream);
	k_sem_give(&sem_stream_configured);
}

static void stream_qos_set(struct bt_bap_stream *stream)
{
	struct bt_iso_info info;
	int err;

	err = bt_iso_chan_get_info(stream->iso, &info);
	__ASSERT(err == 0, "bt_iso_chan_get_info failed: %d", err);
	printk("Audio stream %p QoS set (CIG %u CIS %u)\n", stream,
	       info.unicast.cig_id, info.unicast.cis_id);
	k_sem_give(&sem_stream_qos);
}

static void stream_enabled(struct bt_bap_stream *stream)
{
	printk("Audio stream %p enabled\n", stream);
	k_sem_give(&sem_stream_enabled);
}

static bool stream_tx_can_send(const struct bt_bap_stream *stream)
{
	struct bt_bap_ep_info info;
	int err;

	if (stream == NULL || stream->ep == NULL) {
		return false;
	}

	err = bt_bap_ep_get_info(stream->ep, &info);
	return (err == 0) && info.can_send;
}

static void stream_connected_cb(struct bt_bap_stream *stream)
{
	printk("Audio stream %p connected\n", stream);

	for (size_t i = 0U; i < configured_sink_stream_count; i++) {
		if (stream->ep == sinks[i].ep) {
			sinks[i].seq_num = 0U;
			break;
		}
	}

	k_sem_give(&sem_stream_connected);
}

static void stream_started(struct bt_bap_stream *stream)
{
	printk("Audio stream %p started\n", stream);
#if defined(CONFIG_BT_AUDIO_RX)
	unicast_audio_recv_ctr = 0U;
#endif
	k_sem_give(&sem_stream_started);
}

static void stream_metadata_updated(struct bt_bap_stream *stream)
{
	printk("Audio stream %p metadata updated\n", stream);
}

static void stream_disabled(struct bt_bap_stream *stream)
{
	printk("Audio stream %p disabled\n", stream);
}

static void stream_stopped(struct bt_bap_stream *stream, uint8_t reason)
{
	printk("Audio stream %p stopped (reason 0x%02X)\n", stream, reason);

	if (IS_ENABLED(CONFIG_BT_AUDIO_TX) && stream_tx_can_send(stream)) {
		const int err = stream_tx_unregister(stream);

		if (err != 0) {
			printk("Failed to unregister stream %p for TX: %d\n", stream, err);
		}
	}

	if (atomic_get(&streams_connecting) > 0 && stream->ep != NULL) {
		struct bt_bap_ep_info ep_info;

		if (bt_bap_ep_get_info(stream->ep, &ep_info) == 0 &&
		    (ep_info.state == BT_BAP_EP_STATE_QOS_CONFIGURED ||
		     ep_info.state == BT_BAP_EP_STATE_CODEC_CONFIGURED ||
		     ep_info.state == BT_BAP_EP_STATE_IDLE)) {
			atomic_set(&last_connect_failed, 1);
			k_sem_give(&sem_stream_connect_failed);
			k_sem_give(&sem_stream_connected);
		}
	}
}

static void stream_released(struct bt_bap_stream *stream)
{
	printk("Audio stream %p released\n", stream);
}

#if defined(CONFIG_BT_AUDIO_RX)
static void stream_recv(struct bt_bap_stream *stream,
			const struct bt_iso_recv_info *info,
			struct net_buf *buf)
{
	if (info->flags & BT_ISO_FLAGS_VALID) {
		unicast_audio_recv_ctr++;
		if (CONFIG_INFO_REPORTING_INTERVAL > 0 &&
		    (unicast_audio_recv_ctr % CONFIG_INFO_REPORTING_INTERVAL) == 0U) {
			printk("Incoming audio on stream %p len %u (%" PRIu64 ")\n",
			       stream, buf->len, unicast_audio_recv_ctr);
		}
	}
}
#endif

static struct bt_bap_stream_ops stream_ops = {
	.configured        = stream_configured,
	.qos_set           = stream_qos_set,
	.enabled           = stream_enabled,
	.started           = stream_started,
	.metadata_updated  = stream_metadata_updated,
	.disabled          = stream_disabled,
	.stopped           = stream_stopped,
	.released          = stream_released,
#if defined(CONFIG_BT_AUDIO_RX)
	.recv              = stream_recv,
#endif
	.connected         = stream_connected_cb,
};

static void add_remote_sink(struct bt_bap_ep *ep)
{
	for (size_t i = 0U; i < ARRAY_SIZE(sinks); i++) {
		if (sinks[i].ep == NULL) {
			printk("Sink #%zu: ep %p\n", i, ep);
			sinks[i].ep = ep;
			return;
		}
	}
	printk("Could not add sink ep\n");
}

static void print_remote_codec_cap(const struct bt_audio_codec_cap *codec_cap,
				   enum bt_audio_dir dir)
{
	printk("codec_cap %p dir 0x%02x\n", codec_cap, dir);
	print_codec_cap(codec_cap);
}

static void discover_sinks_cb(struct bt_conn *conn, int err, enum bt_audio_dir dir)
{
	if (err != 0 && err != BT_ATT_ERR_ATTRIBUTE_NOT_FOUND) {
		printk("Discovery failed: %d\n", err);
		return;
	}
	printk("Discover sinks complete: err %d\n", err);
	k_sem_give(&sem_sinks_discovered);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	(void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0) {
		printk("Failed to connect to %s %u %s\n", addr, err, bt_hci_err_to_str(err));
		bt_conn_unref(default_conn);
		default_conn = NULL;
		start_scan();
		return;
	}

	if (conn != default_conn) {
		return;
	}

	printk("Connected: %s\n", addr);
	k_sem_give(&sem_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != default_conn) {
		return;
	}

	(void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Disconnected: %s, reason 0x%02x %s\n", addr, reason, bt_hci_err_to_str(reason));

	bt_conn_unref(default_conn);
	default_conn = NULL;
	k_sem_give(&sem_disconnected);
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t level,
				enum bt_security_err err)
{
	if (err == 0) {
		k_sem_give(&sem_security_updated);
	} else {
		printk("Failed to set security level: %s(%u)\n",
		       bt_security_err_to_str(err), err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected      = connected,
	.disconnected   = disconnected,
	.security_changed = security_changed_cb,
};

static void att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	printk("MTU exchanged: %u/%u\n", tx, rx);
	k_sem_give(&sem_mtu_exchanged);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = att_mtu_updated,
};

static void unicast_client_location_cb(struct bt_conn *conn, enum bt_audio_dir dir,
					enum bt_audio_location loc)
{
	printk("dir %u loc %X\n", dir, loc);
}

static void supported_contexts_cb(struct bt_conn *conn, enum bt_audio_context snk_ctx,
				   enum bt_audio_context src_ctx)
{
	printk("Supported snk ctx %u src ctx %u\n", snk_ctx, src_ctx);
}

static void available_contexts_cb(struct bt_conn *conn, enum bt_audio_context snk_ctx,
				   enum bt_audio_context src_ctx)
{
	printk("Available snk ctx %u src ctx %u\n", snk_ctx, src_ctx);
}

static void pac_record_cb(struct bt_conn *conn, enum bt_audio_dir dir,
			  const struct bt_audio_codec_cap *codec_cap)
{
	print_remote_codec_cap(codec_cap, dir);
}

static void endpoint_cb(struct bt_conn *conn, enum bt_audio_dir dir, struct bt_bap_ep *ep)
{
	if (dir == BT_AUDIO_DIR_SINK) {
		add_remote_sink(ep);
	}
}

static struct bt_bap_unicast_client_cb unicast_client_cbs = {
	.location          = unicast_client_location_cb,
	.available_contexts = available_contexts_cb,
	.pac_record        = pac_record_cb,
	.endpoint          = endpoint_cb,
};

static int scan_and_connect(void)
{
	int   choice;
	int   err;

rescan:
	found_count = 0;
	scan_done   = false;
	memset(found_devices, 0, sizeof(found_devices));

	start_scan();
	printk("Scanning for 10 seconds...\n");
	k_sleep(K_SECONDS(10));
	scan_done = true;
	(void)bt_le_scan_stop();
	k_sleep(K_MSEC(100));

	if (found_count == 0) {
		printk("No devices found. Press r to rescan or q to quit: ");
		int ch = wait_menu_key();
		printk("%c\n", ch);
		if (ch == 'q' || ch == 'Q') {
			return -ECANCELED;
		}
		goto rescan;
	}

	printk("\n--- Found %d device(s) ---\n", found_count);
	for (int i = 0; i < found_count; i++) {
		char addr_str[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(&found_devices[i].addr, addr_str, sizeof(addr_str));
		printk("  [%2d] %-24s  RSSI=%d  %s\n", i, addr_str,
		       found_devices[i].rssi, found_devices[i].name);
	}

choose:
	printk("Select [0-9,a-f], r=rescan, q=quit: ");
	int ch = wait_menu_key();
	printk("%c\n", ch);
	if (ch == 'r' || ch == 'R') {
		goto rescan;
	}
	if (ch == 'q' || ch == 'Q') {
		return -ECANCELED;
	}
	choice = menu_choice_from_key(ch);
	if (choice < 0 || choice >= found_count) {
		printk("Invalid choice, try again.\n");
		goto choose;
	}

	{
		char addr_str[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(&found_devices[choice].addr, addr_str, sizeof(addr_str));
		printk("Connecting to [%d] %s...\n", choice, addr_str);
	}

	err = bt_conn_le_create(&found_devices[choice].addr, BT_CONN_LE_CREATE_CONN,
				BT_BAP_CONN_PARAM_RELAXED, &default_conn);
	if (err != 0) {
		printk("Create conn failed (%d)\n", err);
		goto choose;
	}

	err = k_sem_take(&sem_connected, K_SECONDS(10));
	if (err != 0) {
		printk("Connection timeout\n");
		goto choose;
	}

	err = k_sem_take(&sem_mtu_exchanged, K_FOREVER);
	if (err != 0) {
		return err;
	}

	err = bt_conn_set_security(default_conn, BT_SECURITY_L2);
	if (err != 0) {
		printk("Set security failed (err %d), continuing without upgrade\n", err);
	} else {
		err = k_sem_take(&sem_security_updated, K_SECONDS(10));
		if (err != 0) {
			printk("Security update timed out, continuing\n");
		}
	}

	return 0;
}

static int discover_sinks(void)
{
	int err;

	unicast_client_cbs.discover = discover_sinks_cb;

	err = bt_bap_unicast_client_discover(default_conn, BT_AUDIO_DIR_SINK);
	if (err != 0) {
		printk("Failed to discover sinks: %d\n", err);
		return err;
	}

	return k_sem_take(&sem_sinks_discovered, K_FOREVER);
}

static int configure_stream(struct bt_bap_stream *stream, struct bt_bap_ep *ep,
			    struct bt_bap_lc3_preset *preset)
{
	int err = bt_bap_stream_config(default_conn, stream, ep, &preset->codec_cfg);

	if (err != 0) {
		return err;
	}

	return k_sem_take(&sem_stream_configured, K_FOREVER);
}

static int configure_streams(void)
{
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(sinks); i++) {
		struct bt_bap_ep *ep = sinks[i].ep;
		struct bt_bap_stream *stream = &streams[i];

		if (ep == NULL) {
			continue;
		}

		err = configure_stream(stream, ep, &sink_codec_cfg[i]);
		if (err != 0) {
			printk("Could not configure sink stream[%zu]: %d\n", i, err);
			return err;
		}

		printk("Configured sink stream[%zu]\n", i);
		configured_sink_stream_count++;
	}

	return 0;
}

static int create_group(void)
{
	const size_t params_count = configured_sink_stream_count;
	struct bt_bap_unicast_group_stream_pair_param pair_params[params_count];
	struct bt_bap_unicast_group_stream_param stream_params[configured_sink_stream_count];
	struct bt_bap_unicast_group_param param;
	int err;

	for (size_t i = 0U; i < configured_sink_stream_count; i++) {
		stream_params[i].stream = &streams[i];
		stream_params[i].qos =
			&sink_codec_cfg[MIN(i, ARRAY_SIZE(sink_codec_cfg) - 1)].qos;
	}

	for (size_t i = 0U; i < params_count; i++) {
		pair_params[i].tx_param = &stream_params[i];
		pair_params[i].rx_param = NULL;
	}

	param.params = pair_params;
	param.params_count = params_count;
	param.packing = BT_ISO_PACKING_SEQUENTIAL;

	err = bt_bap_unicast_group_create(&param, &unicast_group);
	if (err != 0) {
		printk("Could not create unicast group (err %d)\n", err);
	}

	return err;
}

static int delete_group(void)
{
	int err = bt_bap_unicast_group_delete(unicast_group);

	if (err != 0) {
		printk("Could not delete unicast group (err %d)\n", err);
	}

	return err;
}

static int set_stream_qos(void)
{
	int err = bt_bap_stream_qos(default_conn, unicast_group);

	if (err != 0) {
		printk("Unable to setup QoS: %d\n", err);
		return err;
	}

	for (size_t i = 0U; i < configured_sink_stream_count; i++) {
		err = k_sem_take(&sem_stream_qos, K_FOREVER);
		if (err != 0) {
			return err;
		}
	}

	return 0;
}

static int enable_streams(void)
{
	for (size_t i = 0U; i < configured_sink_stream_count; i++) {
		int err;
		const size_t cfg_idx = MIN(i, ARRAY_SIZE(sink_codec_cfg) - 1);

		err = bt_bap_stream_enable(&streams[i],
					   sink_codec_cfg[cfg_idx].codec_cfg.meta,
					   sink_codec_cfg[cfg_idx].codec_cfg.meta_len);
		if (err != 0) {
			printk("Unable to enable stream: %d\n", err);
			return err;
		}

		err = k_sem_take(&sem_stream_enabled, K_FOREVER);
		if (err != 0) {
			return err;
		}
	}

	return 0;
}

static int connect_streams(void)
{
	atomic_set(&streams_connecting, configured_sink_stream_count);

	for (size_t i = 0U; i < configured_sink_stream_count; i++) {
		int err = bt_bap_stream_connect(&streams[i]);

		if (err == -EALREADY) {
			atomic_dec(&streams_connecting);
			k_sem_give(&sem_stream_connected);
			continue;
		} else if (err != 0) {
			printk("Unable to connect stream %zu: %d\n", i, err);
			atomic_set(&streams_connecting, 0);
			return err;
		}

		err = k_sem_take(&sem_stream_connected, K_SECONDS(8));
		atomic_dec(&streams_connecting);

		if (err != 0) {
			printk("Timeout waiting for stream %zu to connect\n", i);
			atomic_set(&streams_connecting, 0);
			return -ETIMEDOUT;
		}

		if (atomic_cas(&last_connect_failed, 1, 0)) {
			printk("CIS for stream %zu failed to establish\n", i);
			atomic_set(&streams_connecting, 0);
			k_sem_take(&sem_stream_connect_failed, K_NO_WAIT);
			return -ENOTCONN;
		}
	}

	atomic_set(&streams_connecting, 0);
	return 0;
}

static int start_streams(void)
{
	for (size_t i = 0U; i < configured_sink_stream_count; i++) {
		int err = k_sem_take(&sem_stream_started, K_FOREVER);

		if (err != 0) {
			return err;
		}
	}

	return 0;
}

static void reset_data(void)
{
	k_sem_reset(&sem_connected);
	k_sem_reset(&sem_disconnected);
	k_sem_reset(&sem_mtu_exchanged);
	k_sem_reset(&sem_security_updated);
	k_sem_reset(&sem_sinks_discovered);
	k_sem_reset(&sem_stream_configured);
	k_sem_reset(&sem_stream_qos);
	k_sem_reset(&sem_stream_enabled);
	k_sem_reset(&sem_stream_started);
	k_sem_reset(&sem_stream_connected);
	k_sem_reset(&sem_stream_connect_failed);
	atomic_set(&streams_connecting, 0);
	atomic_set(&last_connect_failed, 0);
	configured_sink_stream_count = 0;
	memset(sinks, 0, sizeof(sinks));
}

static void run_unicast_client(void)
{
	int err;

	err = bt_bap_unicast_client_register_cb(&unicast_client_cbs);
	if (err != 0) {
		printk("Failed to register unicast client callbacks: %d\n", err);
		return;
	}

	while (true) {
		reset_data();

		printk("Waiting for connection\n");
		err = scan_and_connect();
		if (err == -ECANCELED) {
			printk("Exiting unicast mode.\n");
			return;
		}
		if (err != 0) {
			return;
		}
		printk("Connected\n");
		k_sem_give(&sem_broadcast_launch);

		printk("Discovering sinks\n");
		err = discover_sinks();
		if (err != 0) {
			return;
		}
		printk("Sinks discovered\n");

		printk("Configuring streams\n");
		err = configure_streams();
		if (err != 0 || configured_sink_stream_count == 0U) {
			printk("No streams configured\n");
			return;
		}

		printk("Creating unicast group\n");
		err = create_group();
		if (err != 0) {
			return;
		}

		printk("Setting stream QoS\n");
		err = set_stream_qos();
		if (err != 0) {
			return;
		}

		printk("Enabling streams\n");
		err = enable_streams();
		if (err != 0) {
			return;
		}

		printk("Connecting streams\n");
		err = connect_streams();
		if (err == -ENOTCONN || err == -ETIMEDOUT) {
			printk("CIS connection failed (%d), retrying\n", err);
			if (default_conn != NULL) {
				bt_conn_disconnect(default_conn,
						   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
				k_sem_take(&sem_disconnected, K_SECONDS(5));
			}
			delete_group();
			continue;
		} else if (err != 0) {
			return;
		}
		printk("Streams connected\n");

		printk("Starting streams\n");
		err = start_streams();
		if (err != 0) {
			return;
		}
		printk("Streams started\n");

		if (IS_ENABLED(CONFIG_BT_AUDIO_TX)) {
			for (size_t i = 0U; i < configured_sink_stream_count; i++) {
				if (stream_tx_can_send(&streams[i])) {
					err = stream_tx_register(&streams[i]);
					if (err != 0) {
						printk("TX register stream %p failed: %d\n",
						       &streams[i], err);
					}
				}
			}
		}

		k_sem_take(&sem_disconnected, K_FOREVER);
		printk("Deleting group\n");
		delete_group();
		printk("Group deleted\n");
	}
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BROADCAST SOURCE
 * ═══════════════════════════════════════════════════════════════════════════ */

#define BCAST_ENQUEUE_COUNT 2U
#define BCAST_STREAM_COUNT  CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT
#define BCAST_TOTAL_BUFS    (BCAST_ENQUEUE_COUNT * BCAST_STREAM_COUNT)

struct bcast_src_stream {
	struct bt_bap_stream stream;
	uint16_t seq_num;
	size_t sent_cnt;
};

static struct bcast_src_stream bcast_streams[BCAST_STREAM_COUNT];
static struct bt_bap_broadcast_source *bcast_source;

NET_BUF_POOL_FIXED_DEFINE(bcast_tx_pool, BCAST_TOTAL_BUFS,
			  BT_ISO_SDU_BUF_SIZE(CONFIG_BT_ISO_TX_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static struct bt_bap_lc3_preset bcast_preset = BT_BAP_LC3_BROADCAST_PRESET_16_2_1(
	BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT,
	BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);

static K_SEM_DEFINE(sem_bcast_started, 0U, 1U);
static K_SEM_DEFINE(sem_bcast_stopped, 0U, 1U);
static bool bcast_stopping;

static uint8_t bcast_dummy_sdu[155]; /* pre-allocated mock SDU frame (silence) */

static void bcast_send_data(struct bcast_src_stream *s)
{
	struct net_buf *buf;
	int ret;

	if (bcast_stopping) {
		return;
	}

	buf = net_buf_alloc(&bcast_tx_pool, K_NO_WAIT);
	if (!buf) {
		return;
	}

	net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);
	net_buf_add_mem(buf, bcast_dummy_sdu, bcast_preset.qos.sdu);

	ret = bt_bap_stream_send(&s->stream, buf, s->seq_num++);
	if (ret < 0) {
		net_buf_unref(buf);
		return;
	}

	s->sent_cnt++;
	if ((s->sent_cnt % 1000U) == 0U) {
		printk("Broadcast stream %p: %zu pkts sent\n", &s->stream, s->sent_cnt);
	}
}

static void bcast_stream_started_cb(struct bt_bap_stream *stream)
{
	struct bcast_src_stream *s = CONTAINER_OF(stream, struct bcast_src_stream, stream);

	printk("Broadcast stream %p started\n", stream);
	s->seq_num   = 0U;
	s->sent_cnt  = 0U;
}

static void bcast_stream_sent_cb(struct bt_bap_stream *stream)
{
	struct bcast_src_stream *s = CONTAINER_OF(stream, struct bcast_src_stream, stream);

	bcast_send_data(s);
}

static struct bt_bap_stream_ops bcast_stream_ops = {
	.started = bcast_stream_started_cb,
	.sent    = bcast_stream_sent_cb,
};

static void bcast_source_started_cb(struct bt_bap_broadcast_source *source)
{
	printk("Broadcast source %p started\n", source);
	k_sem_give(&sem_bcast_started);
}

static void bcast_source_stopped_cb(struct bt_bap_broadcast_source *source, uint8_t reason)
{
	printk("Broadcast source %p stopped (reason 0x%02X)\n", source, reason);
	k_sem_give(&sem_bcast_stopped);
}

static struct bt_bap_broadcast_source_cb bcast_source_cb = {
	.started = bcast_source_started_cb,
	.stopped = bcast_source_stopped_cb,
};

static int setup_broadcast_source(struct bt_bap_broadcast_source **source)
{
	struct bt_bap_broadcast_source_stream_param stream_params[BCAST_STREAM_COUNT];
	struct bt_bap_broadcast_source_subgroup_param subgroup = {
		.params_count = BCAST_STREAM_COUNT,
		.params       = stream_params,
		.codec_cfg    = &bcast_preset.codec_cfg,
	};
	struct bt_bap_broadcast_source_param create_param = {
		.params_count = 1,
		.params       = &subgroup,
		.qos          = &bcast_preset.qos,
		.encryption   = false,
		.packing      = BT_ISO_PACKING_SEQUENTIAL,
	};
	uint8_t left[] = {BT_AUDIO_CODEC_DATA(BT_AUDIO_CODEC_CFG_CHAN_ALLOC,
					      BT_BYTES_LIST_LE32(BT_AUDIO_LOCATION_FRONT_LEFT))};
	uint8_t right[] = {BT_AUDIO_CODEC_DATA(BT_AUDIO_CODEC_CFG_CHAN_ALLOC,
					       BT_BYTES_LIST_LE32(BT_AUDIO_LOCATION_FRONT_RIGHT))};

	for (size_t i = 0; i < BCAST_STREAM_COUNT; i++) {
		stream_params[i].stream    = &bcast_streams[i].stream;
		stream_params[i].data      = (i == 0) ? left : right;
		stream_params[i].data_len  = (i == 0) ? sizeof(left) : sizeof(right);
		bt_bap_stream_cb_register(stream_params[i].stream, &bcast_stream_ops);
	}

	printk("Creating broadcast source (%u streams)\n", BCAST_STREAM_COUNT);
	return bt_bap_broadcast_source_create(&create_param, source);
}

static void run_broadcast_source(void)
{
	int err;

	printk("Broadcast source waiting for unicast selection\n");
	k_sem_take(&sem_broadcast_launch, K_FOREVER);

	while (true) {
		NET_BUF_SIMPLE_DEFINE(ad_buf, BT_UUID_SIZE_16 + BT_AUDIO_BROADCAST_ID_SIZE);
		NET_BUF_SIMPLE_DEFINE(base_buf, 128);
		struct bt_data ext_ad[2];
		struct bt_data per_ad;
		struct bt_le_ext_adv *adv;
		uint32_t broadcast_id;

		/* Create extended advertising set */
		err = bt_le_ext_adv_create(BT_BAP_ADV_PARAM_BROADCAST_FAST, NULL, &adv);
		if (err != 0) {
			printk("Failed to create ext adv set: %d\n", err);
			return;
		}

		err = bt_le_per_adv_set_param(adv, BT_BAP_PER_ADV_PARAM_BROADCAST_FAST);
		if (err != 0) {
			printk("Failed to set per adv params: %d\n", err);
			return;
		}

		/* Create broadcast source */
		err = setup_broadcast_source(&bcast_source);
		if (err != 0) {
			printk("Failed to setup broadcast source: %d\n", err);
			return;
		}

		/* Generate random broadcast ID */
		err = bt_rand(&broadcast_id, BT_AUDIO_BROADCAST_ID_SIZE);
		if (err != 0) {
			printk("Failed to generate broadcast ID: %d\n", err);
			return;
		}

		/* Extended advertising data: Broadcast Audio Service UUID + ID */
		net_buf_simple_add_le16(&ad_buf, BT_UUID_BROADCAST_AUDIO_VAL);
		net_buf_simple_add_le24(&ad_buf, broadcast_id);
		ext_ad[0].type     = BT_DATA_SVC_DATA16;
		ext_ad[0].data_len = ad_buf.len;
		ext_ad[0].data     = ad_buf.data;
		ext_ad[1] = (struct bt_data)BT_DATA(BT_DATA_NAME_COMPLETE,
						    CONFIG_BT_DEVICE_NAME,
						    sizeof(CONFIG_BT_DEVICE_NAME) - 1);

		err = bt_le_ext_adv_set_data(adv, ext_ad, ARRAY_SIZE(ext_ad), NULL, 0);
		if (err != 0) {
			printk("Failed to set ext adv data: %d\n", err);
			return;
		}

		/* Periodic advertising data: BASE */
		err = bt_bap_broadcast_source_get_base(bcast_source, &base_buf);
		if (err != 0) {
			printk("Failed to get BASE: %d\n", err);
			return;
		}

		per_ad.type     = BT_DATA_SVC_DATA16;
		per_ad.data_len = base_buf.len;
		per_ad.data     = base_buf.data;

		err = bt_le_per_adv_set_data(adv, &per_ad, 1);
		if (err != 0) {
			printk("Failed to set per adv data: %d\n", err);
			return;
		}

		/* Start advertising */
		err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
		if (err != 0) {
			printk("Failed to start ext adv: %d\n", err);
			return;
		}

		err = bt_le_per_adv_start(adv);
		if (err != 0) {
			printk("Failed to start per adv: %d\n", err);
			return;
		}

		printk("Starting broadcast source (ID 0x%06X)...\n", broadcast_id);
		bcast_stopping = false;

		err = bt_bap_broadcast_source_start(bcast_source, adv);
		if (err != 0) {
			printk("Failed to start broadcast source: %d\n", err);
			return;
		}

		k_sem_take(&sem_bcast_started, K_FOREVER);
		printk("Broadcast source started — broadcasting for 5 minutes\n");

		/* Seed the send pipeline */
		for (size_t i = 0; i < BCAST_STREAM_COUNT; i++) {
			for (unsigned int j = 0; j < BCAST_ENQUEUE_COUNT; j++) {
				bcast_stream_sent_cb(&bcast_streams[i].stream);
			}
		}

		k_sleep(K_SECONDS(300));

		printk("Stopping broadcast source\n");
		bcast_stopping = true;

		err = bt_bap_broadcast_source_stop(bcast_source);
		if (err == 0) {
			k_sem_take(&sem_bcast_stopped, K_FOREVER);
		}

		bt_bap_broadcast_source_delete(bcast_source);
		bcast_source = NULL;

		bt_le_per_adv_stop(adv);
		bt_le_ext_adv_stop(adv);
		bt_le_ext_adv_delete(adv);

		printk("Restarting broadcast source...\n");
	}
}

/* ═══════════════════════════════════════════════════════════════════════════
 * COMMON INIT + MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

static struct k_thread bcast_src_thread_data;
static K_THREAD_STACK_DEFINE(bcast_src_stack, 8192);

static void bcast_src_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	run_broadcast_source();
}

static int init(void)
{
	int err;

	err = bt_enable(NULL);
	if (err != 0) {
		printk("Bluetooth enable failed (err %d)\n", err);
		return err;
	}

	for (size_t i = 0; i < ARRAY_SIZE(streams); i++) {
		streams[i].ops = &stream_ops;
	}

	bt_gatt_cb_register(&gatt_callbacks);

	if (IS_ENABLED(CONFIG_BT_AUDIO_TX)) {
		stream_tx_init();
	}

	err = bt_bap_broadcast_source_register_cb(&bcast_source_cb);
	if (err != 0) {
		printk("Failed to register broadcast source callbacks: %d\n", err);
		return err;
	}

	memset(bcast_dummy_sdu, 0, sizeof(bcast_dummy_sdu));

	return 0;
}

int main(void)
{
	int err;

	printk("BAP Unicast Client + Broadcast Source starting\n");

	if (!device_is_ready(console_uart)) {
		printk("Console UART not ready\n");
		return 0;
	}

	err = init();
	if (err != 0) {
		return 0;
	}

	printk("Bluetooth initialized\n");

	/* Start Broadcast Source in background thread */
	k_thread_create(&bcast_src_thread_data, bcast_src_stack,
			K_THREAD_STACK_SIZEOF(bcast_src_stack),
			bcast_src_thread_fn, NULL, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&bcast_src_thread_data, "bcast_src");

	printk("Broadcast Source thread started; Unicast Client scanning\n");

	/* Run Unicast Client state machine in main thread */
	run_unicast_client();

	return 0;
}
