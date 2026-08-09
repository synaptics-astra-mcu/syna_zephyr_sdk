/*
 * Copyright (c) 2021-2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <zephyr/console/console.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <zephyr/autoconf.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/assigned_numbers.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/audio/bap_lc3_preset.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/types.h>

#include "stream_tx.h"

static void start_scan(void);

/* ── Scan list ───────────────────────────────────────────────────── */
#define MAX_FOUND_DEVICES 16

struct found_device {
	bt_addr_le_t addr;
	int8_t       rssi;
};

static struct found_device found_devices[MAX_FOUND_DEVICES];
static int found_count;
static bool scan_done;

#if defined(CONFIG_BT_AUDIO_RX)
uint64_t unicast_audio_recv_ctr; /* This value is exposed to test code */
#endif

static struct bt_bap_unicast_client_cb unicast_client_cbs;
static struct bt_conn *default_conn;
static struct bt_bap_unicast_group *unicast_group;
/* Number of CIS connections in flight; used to detect stopped-during-connect */
static atomic_t streams_connecting;
/* Set to 1 by stream_stopped when a CIS fails to establish; cleared by reset_data */
static atomic_t last_connect_failed;
static struct audio_sink {
	struct bt_bap_ep *ep;
	uint16_t seq_num;
} sinks[CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT];
#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
static struct bt_bap_ep *sources[CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT];
#endif

static struct bt_bap_stream streams[CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT +
				      CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT];
static size_t configured_sink_stream_count;
#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
static size_t configured_source_stream_count;
#else
#define configured_source_stream_count 0
#endif

#define configured_stream_count (configured_sink_stream_count + \
				 configured_source_stream_count)

/* Per-stream codec configurations: stream 0 = L, stream 1 = R.
 * TWS earbuds typically assign ASE 0 = FRONT_LEFT, ASE 1 = FRONT_RIGHT; sending the wrong
 * audio location causes the headset to reject or ignore the second stream.
 */
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
#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
static K_SEM_DEFINE(sem_sources_discovered, 0, 1);
#endif
static K_SEM_DEFINE(sem_stream_configured, 0, 1);
static K_SEM_DEFINE(sem_stream_qos, 0,
		    ARRAY_SIZE(sinks) + CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT);
static K_SEM_DEFINE(sem_stream_enabled, 0, 1);
static K_SEM_DEFINE(sem_stream_started, 0, CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT +
					    CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT);
static K_SEM_DEFINE(sem_stream_connected, 0,
		    CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT +
		    CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT);
/* Given when a CIS fails to establish so connect_streams() can unblock */
static K_SEM_DEFINE(sem_stream_connect_failed, 0,
		    CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT +
		    CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT);

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
	printk("codec id 0x%02x cid 0x%04x vid 0x%04x count %u\n", codec_cap->id, codec_cap->cid,
	       codec_cap->vid, codec_cap->data_len);

	if (codec_cap->id == BT_HCI_CODING_FORMAT_LC3) {
		bt_audio_data_parse(codec_cap->data, codec_cap->data_len, print_cb, "data");
	} else { /* If not LC3, we cannot assume it's LTV */
		printk("data: ");
		print_hex(codec_cap->data, codec_cap->data_len);
		printk("\n");
	}

	bt_audio_data_parse(codec_cap->meta, codec_cap->meta_len, print_cb, "meta");
}

/* check_audio_support_and_connect: no longer used; connection is established
 * directly from scan_and_connect() after the user selects a device.
 */

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	if (scan_done || default_conn != NULL) {
		return;
	}

	/* Only connectable */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
	    type != BT_GAP_ADV_TYPE_EXT_ADV) {
		return;
	}

	/* Deduplicate by address */
	for (int i = 0; i < found_count; i++) {
		if (bt_addr_le_eq(&found_devices[i].addr, addr)) {
			found_devices[i].rssi = rssi; /* update RSSI */
			return;
		}
	}

	if (found_count >= MAX_FOUND_DEVICES) {
		return;
	}

	bt_addr_le_copy(&found_devices[found_count].addr, addr);
	found_devices[found_count].rssi = rssi;
	found_count++;
}

static void start_scan(void)
{
	int err;

	/* This demo doesn't require active scan */
	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
	if (err != 0) {
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}

	printk("Scanning successfully started\n");
}

static void stream_configured(struct bt_bap_stream *stream, const struct bt_bap_qos_cfg_pref *pref)
{
	printk("Audio Stream %p configured\n", stream);

	k_sem_give(&sem_stream_configured);
}

static void stream_qos_set(struct bt_bap_stream *stream)
{
	struct bt_iso_info info;
	int err;

	err = bt_iso_chan_get_info(stream->iso, &info);
	__ASSERT(err == 0, "Failed to get ISO chan info: %d", err);

	printk("Audio Stream %p QoS set with CIG_ID %u and CIS_ID %u\n", stream,
	       info.unicast.cig_id, info.unicast.cis_id);

	k_sem_give(&sem_stream_qos);
}

static void stream_enabled(struct bt_bap_stream *stream)
{
	printk("Audio Stream %p enabled\n", stream);

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
	if (err != 0) {
		return false;
	}

	return info.can_send;
}

static void stream_connected_cb(struct bt_bap_stream *stream)
{
	printk("Audio Stream %p connected\n", stream);

	/* Reset sequence number for sinks */
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
	printk("Audio Stream %p started\n", stream);
#if defined(CONFIG_BT_AUDIO_RX)
	unicast_audio_recv_ctr = 0U;
#endif

	/*
	 * Do NOT register TX here. We wait for ALL streams to reach STREAMING
	 * state before starting any ISO TX. This prevents the controller from
	 * occupying radio slots for CIS 0x60 TX while CIS 0x61 is still trying
	 * to synchronise, which can cause 0x3e (Sync Timeout) on 0x61.
	 * TX registration is done in main() after start_streams() returns.
	 */

	k_sem_give(&sem_stream_started);
}

static void stream_metadata_updated(struct bt_bap_stream *stream)
{
	printk("Audio Stream %p metadata updated\n", stream);
}

static void stream_disabled(struct bt_bap_stream *stream)
{
	printk("Audio Stream %p disabled\n", stream);
}

static void stream_stopped(struct bt_bap_stream *stream, uint8_t reason)
{
	printk("Audio Stream %p stopped with reason 0x%02X\n", stream, reason);

	/* Unregister the stream for TX if it can send */
	if (IS_ENABLED(CONFIG_BT_AUDIO_TX) && stream_tx_can_send(stream)) {
		const int err = stream_tx_unregister(stream);

		if (err != 0) {
			printk("Failed to unregister stream %p for TX: %d", stream, err);
		}
	}

	/*
	 * If a CIS failed to establish (e.g. 0x3e Sync Timeout), the stream
	 * transitions back to CODEC_CONFIGURED/QOS_CONFIGURED and eventually
	 * calls stopped without calling connected. Unblock connect_streams()
	 * quickly instead of waiting for the 8 s timeout.
	 *
	 * Guard: only signal failure if:
	 *  1. connect_streams() is actively waiting (streams_connecting > 0)
	 *  2. The stream ep fell back to QOS_CONFIGURED or CODEC_CONFIGURED
	 *     (not STREAMING→STOPPED, which is a normal stop, not a CIS fail)
	 */
	if (atomic_get(&streams_connecting) > 0 && stream->ep != NULL) {
		struct bt_bap_ep_info ep_info;

		if (bt_bap_ep_get_info(stream->ep, &ep_info) == 0 &&
		    (ep_info.state == BT_BAP_EP_STATE_QOS_CONFIGURED ||
		     ep_info.state == BT_BAP_EP_STATE_CODEC_CONFIGURED ||
		     ep_info.state == BT_BAP_EP_STATE_IDLE)) {
			atomic_set(&last_connect_failed, 1);
			k_sem_give(&sem_stream_connect_failed);
			k_sem_give(&sem_stream_connected); /* wake up the k_sem_take */
		}
	}
}

static void stream_released(struct bt_bap_stream *stream)
{
	printk("Audio Stream %p released\n", stream);
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
			printk("Incoming audio on stream %p len %u (%" PRIu64 ")\n", stream,
			       buf->len, unicast_audio_recv_ctr);
		}
	}
}
#endif /* CONFIG_BT_AUDIO_RX */

static struct bt_bap_stream_ops stream_ops = {
	.configured = stream_configured,
	.qos_set = stream_qos_set,
	.enabled = stream_enabled,
	.started = stream_started,
	.metadata_updated = stream_metadata_updated,
	.disabled = stream_disabled,
	.stopped = stream_stopped,
	.released = stream_released,
#if defined(CONFIG_BT_AUDIO_RX)
	.recv = stream_recv,
#endif
	.connected = stream_connected_cb,
};

#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
static void add_remote_source(struct bt_bap_ep *ep)
{
	for (size_t i = 0U; i < ARRAY_SIZE(sources); i++) {
		if (sources[i] == NULL) {
			printk("Source #%zu: ep %p\n", i, ep);
			sources[i] = ep;
			return;
		}
	}

	printk("Could not add source ep\n");
}
#endif /* CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0 */

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

	if (err == BT_ATT_ERR_ATTRIBUTE_NOT_FOUND) {
		printk("Discover sinks completed without finding any sink ASEs\n");
	} else {
		printk("Discover sinks complete: err %d\n", err);
	}

	k_sem_give(&sem_sinks_discovered);
}

#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
static void discover_sources_cb(struct bt_conn *conn, int err, enum bt_audio_dir dir)
{
	if (err != 0 && err != BT_ATT_ERR_ATTRIBUTE_NOT_FOUND) {
		printk("Discovery failed: %d\n", err);
		return;
	}

	if (err == BT_ATT_ERR_ATTRIBUTE_NOT_FOUND) {
		printk("Discover sinks completed without finding any source ASEs\n");
	} else {
		printk("Discover sources complete: err %d\n", err);
	}

	k_sem_give(&sem_sources_discovered);
}
#endif /* CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0 */

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
		printk("Failed to set security level: %s(%u)", bt_security_err_to_str(err), err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed_cb
};

static void att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	printk("MTU exchanged: %u/%u\n", tx, rx);
	k_sem_give(&sem_mtu_exchanged);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = att_mtu_updated,
};

static void unicast_client_location_cb(struct bt_conn *conn,
				      enum bt_audio_dir dir,
				      enum bt_audio_location loc)
{
	printk("dir %u loc %X\n", dir, loc);
}

static void supported_contexts_cb(struct bt_conn *conn, enum bt_audio_context snk_ctx,
				  enum bt_audio_context src_ctx)
{
	printk("Supported snk ctx %u src ctx %u\n", snk_ctx, src_ctx);
}

static void available_contexts_cb(struct bt_conn *conn,
				  enum bt_audio_context snk_ctx,
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
	if (dir == BT_AUDIO_DIR_SOURCE) {
#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
		add_remote_source(ep);
#endif
	} else if (dir == BT_AUDIO_DIR_SINK) {
		add_remote_sink(ep);
	}
}

static struct bt_bap_unicast_client_cb unicast_client_cbs = {
	.location = unicast_client_location_cb,
	.available_contexts = available_contexts_cb,
	.pac_record = pac_record_cb,
	.endpoint = endpoint_cb,
};

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

	return 0;
}

static int scan_and_connect(void)
{
	char *line;
	int   choice;
	int   err;

rescan:
	/* Reset scan list */
	found_count = 0;
	scan_done   = false;
	memset(found_devices, 0, sizeof(found_devices));

	start_scan();
	printk("Scanning for 10 seconds...\n");
	k_sleep(K_SECONDS(10));
	scan_done = true;
	(void)bt_le_scan_stop();
	k_sleep(K_MSEC(100)); /* let last callbacks drain */

	if (found_count == 0) {
		if (IS_ENABLED(CONFIG_BT_BAP_AUTO_CONNECT)) {
			printk("No devices found, retrying...\n");
			goto rescan;
		}
		printk("No devices found. Scan again? [Y/n]: ");
		line = console_getline();
		if (line && (line[0] == 'n' || line[0] == 'N')) {
			return -ECANCELED;
		}
		goto rescan;
	}

	/* Print list */
	printk("\n--- Found %d device(s) ---\n", found_count);
	for (int i = 0; i < found_count; i++) {
		char addr_str[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(&found_devices[i].addr, addr_str,
				  sizeof(addr_str));
		printk("  [%2d] %s  RSSI=%d\n", i, addr_str,
		       found_devices[i].rssi);
	}

	if (IS_ENABLED(CONFIG_BT_BAP_AUTO_CONNECT)) {
		/*
		 * Auto-connect: no user interaction required.
		 *
		 * If CONFIG_BT_BAP_AUTO_CONNECT_ADDR is set, search the scan
		 * list for that address and connect to it.  Otherwise pick the
		 * device with the strongest RSSI.
		 */
		if (strlen(CONFIG_BT_BAP_AUTO_CONNECT_ADDR) > 0) {
			bt_addr_le_t target;

			if (bt_addr_le_from_str(CONFIG_BT_BAP_AUTO_CONNECT_ADDR,
						"public", &target) != 0) {
				/* Try random address type */
				bt_addr_le_from_str(CONFIG_BT_BAP_AUTO_CONNECT_ADDR,
						    "random", &target);
			}

			choice = -1;
			for (int i = 0; i < found_count; i++) {
				if (bt_addr_le_eq(&found_devices[i].addr, &target)) {
					choice = i;
					break;
				}
			}

			if (choice < 0) {
				printk("Target %s not found in scan, retrying...\n",
				       CONFIG_BT_BAP_AUTO_CONNECT_ADDR);
				goto rescan;
			}
		} else {
			/* Pick the device with the strongest (highest) RSSI */
			choice = 0;
			for (int i = 1; i < found_count; i++) {
				if (found_devices[i].rssi > found_devices[choice].rssi) {
					choice = i;
				}
			}
		}

		{
			char addr_str[BT_ADDR_LE_STR_LEN];

			bt_addr_le_to_str(&found_devices[choice].addr, addr_str,
					  sizeof(addr_str));
			printk("Auto-connecting to [%d] %s (RSSI=%d)...\n",
			       choice, addr_str, found_devices[choice].rssi);
		}
	} else {
		printk("Select [0-%d], r=rescan, q=quit: ", found_count - 1);

		line = console_getline();
		if (!line) {
			goto rescan;
		}
		if (line[0] == 'r' || line[0] == 'R') {
			printk("Rescanning...\n");
			goto rescan;
		}
		if (line[0] == 'q' || line[0] == 'Q') {
			return -ECANCELED;
		}

		choice = atoi(line);
		if (choice < 0 || choice >= found_count) {
			printk("Invalid choice (0~%d), try again.\n", found_count - 1);
			goto rescan;
		}

		{
			char addr_str[BT_ADDR_LE_STR_LEN];

			bt_addr_le_to_str(&found_devices[choice].addr, addr_str,
					  sizeof(addr_str));
			printk("Connecting to [%d] %s ...\n", choice, addr_str);
		}
	}

	err = bt_conn_le_create(&found_devices[choice].addr,
				BT_CONN_LE_CREATE_CONN,
				BT_BAP_CONN_PARAM_RELAXED, &default_conn);
	if (err != 0) {
		printk("Create conn failed (%d), rescanning...\n", err);
		goto rescan;
	}

	err = k_sem_take(&sem_connected, K_SECONDS(10));
	if (err != 0) {
		printk("Connection timeout, rescanning...\n");
		goto rescan;
	}

	err = k_sem_take(&sem_mtu_exchanged, K_FOREVER);
	if (err != 0) {
		printk("failed to take sem_mtu_exchanged (err %d)\n", err);
		return err;
	}

	err = bt_conn_set_security(default_conn, BT_SECURITY_L2);
	if (err != 0) {
		printk("failed to set security (err %d)\n", err);
		return err;
	}

	err = k_sem_take(&sem_security_updated, K_FOREVER);
	if (err != 0) {
		printk("failed to take sem_security_updated (err %d)\n", err);
		return err;
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

	err = k_sem_take(&sem_sinks_discovered, K_FOREVER);
	if (err != 0) {
		printk("failed to take sem_sinks_discovered (err %d)\n", err);
		return err;
	}

	return 0;
}

#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
static int discover_sources(void)
{
	int err;

	unicast_client_cbs.discover = discover_sources_cb;

	err = bt_bap_unicast_client_discover(default_conn, BT_AUDIO_DIR_SOURCE);
	if (err != 0) {
		printk("Failed to discover sources: %d\n", err);
		return err;
	}

	err = k_sem_take(&sem_sources_discovered, K_FOREVER);
	if (err != 0) {
		printk("failed to take sem_sources_discovered (err %d)\n", err);
		return err;
	}

	return 0;
}
#endif /* CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0 */

static int configure_stream(struct bt_bap_stream *stream, struct bt_bap_ep *ep,
			    struct bt_bap_lc3_preset *preset)
{
	int err;

	err = bt_bap_stream_config(default_conn, stream, ep, &preset->codec_cfg);
	if (err != 0) {
		return err;
	}

	err = k_sem_take(&sem_stream_configured, K_FOREVER);
	if (err != 0) {
		printk("failed to take sem_stream_configured (err %d)\n", err);
		return err;
	}

	return 0;
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
			printk("Could not configure sink stream[%zu]: %d\n",
			       i, err);
			return err;
		}

		printk("Configured sink stream[%zu]\n", i);
		configured_sink_stream_count++;
	}

#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
	for (size_t i = 0; i < ARRAY_SIZE(sources); i++) {
		struct bt_bap_ep *ep = sources[i];
		struct bt_bap_stream *stream = &streams[i + configured_sink_stream_count];

		if (ep == NULL) {
			continue;
		}

		err = configure_stream(stream, ep, &sink_codec_cfg[0]);
		if (err != 0) {
			printk("Could not configure source stream[%zu]: %d\n",
			       i, err);
			return err;
		}

		printk("Configured source stream[%zu]\n", i);
		configured_source_stream_count++;
	}
#endif /* CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0 */

	return 0;
}

static int create_group(void)
{
	const size_t params_count = MAX(configured_sink_stream_count,
					configured_source_stream_count);
	struct bt_bap_unicast_group_stream_pair_param pair_params[params_count];
	struct bt_bap_unicast_group_stream_param stream_params[configured_stream_count];
	struct bt_bap_unicast_group_param param;
	int err;

	for (size_t i = 0U; i < configured_stream_count; i++) {
		stream_params[i].stream = &streams[i];
		stream_params[i].qos = &sink_codec_cfg[MIN(i, ARRAY_SIZE(sink_codec_cfg) - 1)].qos;
	}

	for (size_t i = 0U; i < params_count; i++) {
		if (i < configured_sink_stream_count) {
			pair_params[i].tx_param = &stream_params[i];
		} else {
			pair_params[i].tx_param = NULL;
		}

		if (i < configured_source_stream_count) {
			pair_params[i].rx_param = &stream_params[i + configured_sink_stream_count];
		} else {
			pair_params[i].rx_param = NULL;
		}
	}

	param.params = pair_params;
	param.params_count = params_count;
	param.packing = BT_ISO_PACKING_SEQUENTIAL;

	err = bt_bap_unicast_group_create(&param, &unicast_group);
	if (err != 0) {
		printk("Could not create unicast group (err %d)\n", err);
		return err;
	}

	return 0;
}

static int delete_group(void)
{
	int err;

	err = bt_bap_unicast_group_delete(unicast_group);
	if (err != 0) {
		printk("Could not create unicast group (err %d)\n", err);
		return err;
	}

	return 0;
}

static int set_stream_qos(void)
{
	int err;

	err = bt_bap_stream_qos(default_conn, unicast_group);
	if (err != 0) {
		printk("Unable to setup QoS: %d\n", err);
		return err;
	}

	for (size_t i = 0U; i < configured_stream_count; i++) {
		printk("QoS: waiting for %zu streams\n", configured_stream_count);
		err = k_sem_take(&sem_stream_qos, K_FOREVER);
		if (err != 0) {
			printk("failed to take sem_stream_qos (err %d)\n", err);
			return err;
		}
	}

	return 0;
}

static int enable_streams(void)
{
	for (size_t i = 0U; i < configured_stream_count; i++) {
		int err;

		const size_t cfg_idx = MIN(i, ARRAY_SIZE(sink_codec_cfg) - 1);

		err = bt_bap_stream_enable(&streams[i], sink_codec_cfg[cfg_idx].codec_cfg.meta,
					   sink_codec_cfg[cfg_idx].codec_cfg.meta_len);
		if (err != 0) {
			printk("Unable to enable stream: %d\n", err);
			return err;
		}

		err = k_sem_take(&sem_stream_enabled, K_FOREVER);
		if (err != 0) {
			printk("failed to take sem_stream_enabled (err %d)\n", err);
			return err;
		}
	}

	return 0;
}

static int connect_streams(void)
{
	/*
	 * Connect CIS streams sequentially.  The BCM4381A1 controller handles
	 * sequential LE_Create_CIS (count=1) better than a single batched
	 * command (count=N): with sequential connects the controller schedules
	 * each CIS at a different time offset within the CIG interval, whereas
	 * batching causes it to try simultaneous scheduling and fail the second
	 * CIS with 0x3e (Synchronization Timeout).
	 *
	 * When a CIS fails (e.g. 0x3e) the BAP stack calls stream_stopped,
	 * which gives sem_stream_connect_failed to unblock this loop so we
	 * can return an error and retry from scratch.
	 */
	atomic_set(&streams_connecting, configured_stream_count);

	for (size_t i = 0U; i < configured_stream_count; i++) {
		int err;

		err = bt_bap_stream_connect(&streams[i]);
		if (err == -EALREADY) {
			/* Paired bidirectional stream already in flight */
			atomic_dec(&streams_connecting);
			k_sem_give(&sem_stream_connected);
			continue;
		} else if (err != 0) {
			printk("Unable to connect stream %zu: %d\n", i, err);
			atomic_set(&streams_connecting, 0);
			return err;
		}

		/*
		 * Wait up to 8 s for the CIS to connect.  sem_stream_connected
		 * is given by stream_connected_cb on success; stream_stopped
		 * also gives it (and sets last_connect_failed) when the CIS
		 * fails to establish, so we don't have to wait the full timeout.
		 */
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
			/* Drain failure semaphore */
			k_sem_take(&sem_stream_connect_failed, K_NO_WAIT);
			return -ENOTCONN;
		}
	}

	atomic_set(&streams_connecting, 0);
	return 0;
}

static enum bt_audio_dir stream_dir(const struct bt_bap_stream *stream)
{
	struct bt_bap_ep_info ep_info;
	int err;

	err = bt_bap_ep_get_info(stream->ep, &ep_info);
	if (err != 0) {
		printk("Failed to get ep info for %p: %d\n", stream, err);
		__ASSERT_NO_MSG(false);

		return 0;
	}

	return ep_info.dir;
}

static int start_streams(void)
{
	for (size_t i = 0U; i < configured_stream_count; i++) {
		struct bt_bap_stream *stream = &streams[i];
		int err;

		if (stream_dir(stream) == BT_AUDIO_DIR_SOURCE) {
			err = bt_bap_stream_start(&streams[i]);
			if (err != 0) {
				printk("Unable to start stream: %d\n", err);
				return err;
			}
		} /* Sink streams are started by the unicast server */

		err = k_sem_take(&sem_stream_started, K_FOREVER);
		if (err != 0) {
			printk("failed to take sem_stream_started (err %d)\n", err);
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
#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
	k_sem_reset(&sem_sources_discovered);
#endif
	k_sem_reset(&sem_stream_configured);
	k_sem_reset(&sem_stream_qos);
	k_sem_reset(&sem_stream_enabled);
	k_sem_reset(&sem_stream_started);
	k_sem_reset(&sem_stream_connected);
	k_sem_reset(&sem_stream_connect_failed);
	atomic_set(&streams_connecting, 0);
	atomic_set(&last_connect_failed, 0);

	configured_sink_stream_count = 0;
#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
	configured_source_stream_count = 0;
#endif
	memset(sinks, 0, sizeof(sinks));
#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
	memset(sources, 0, sizeof(sources));
#endif
}

int main(void)
{
	int err;

	printk("Initializing\n");
	err = init();
	if (err != 0) {
		return 0;
	}
	printk("Initialized\n");

	err = bt_bap_unicast_client_register_cb(&unicast_client_cbs);
	if (err != 0) {
		printk("Failed to register client callbacks: %d", err);
		return 0;
	}

	while (true) {
		reset_data();

		printk("Waiting for connection\n");
		console_getline_init();
		err = scan_and_connect();
		if (err == -ECANCELED) {
			printk("Exiting.\n");
			return 0;
		}
		if (err != 0) {
			return 0;
		}
		printk("Connected\n");

		printk("Discovering sinks\n");
		err = discover_sinks();
		if (err != 0) {
			return 0;
		}
		printk("Sinks discovered\n");

#if CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0
		printk("Discovering sources\n");
		err = discover_sources();
		if (err != 0) {
			return 0;
		}
		printk("Sources discovered\n");
#endif /* CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT > 0 */

		printk("Configuring streams\n");
		err = configure_streams();
		if (err != 0) {
			return 0;
		}

		if (configured_stream_count == 0U) {
			printk("No streams were configured\n");
			return 0;
		}

		printk("Creating unicast group\n");
		err = create_group();
		if (err != 0) {
			return 0;
		}
		printk("Unicast group created\n");

		printk("Setting stream QoS\n");
		err = set_stream_qos();
		if (err != 0) {
			return 0;
		}
		printk("Stream QoS Set\n");

		printk("Enabling streams\n");
		err = enable_streams();
		if (err != 0) {
			return 0;
		}
		printk("Streams enabled\n");

		printk("Connecting streams\n");
		err = connect_streams();
		if (err == -ENOTCONN || err == -ETIMEDOUT) {
			printk("CIS connection failed (%d), retrying from scratch\n", err);
			/* Disconnect ACL cleanly so we can restart */
			if (default_conn != NULL) {
				bt_conn_disconnect(default_conn,
						   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
				k_sem_take(&sem_disconnected, K_SECONDS(5));
			}
			err = delete_group();
			if (err != 0) {
				return 0;
			}
			continue; /* retry the outer while(true) loop */
		} else if (err != 0) {
			return 0;
		}
		printk("Streams connected\n");

		printk("Starting streams\n");
		err = start_streams();
		if (err != 0) {
			return 0;
		}
		printk("Streams started\n");

		/*
		 * Register all TX streams only AFTER every stream has reached
		 * STREAMING state (i.e. after start_streams() returns).
		 * Delaying TX start prevents radio-level interference between
		 * CIS 0x60 data transmissions and CIS 0x61 connection setup.
		 */
		if (IS_ENABLED(CONFIG_BT_AUDIO_TX)) {
			for (size_t i = 0U; i < configured_stream_count; i++) {
				if (stream_tx_can_send(&streams[i])) {
					err = stream_tx_register(&streams[i]);
					if (err != 0) {
						printk("Failed to register stream %p for TX: %d\n",
						       &streams[i], err);
					}
				}
			}
		}

		/* Wait for disconnect */
		err = k_sem_take(&sem_disconnected, K_FOREVER);
		if (err != 0) {
			printk("failed to take sem_disconnected (err %d)\n", err);
			return 0;
		}

		printk("Deleting group\n");
		err = delete_group();
		if (err != 0) {
			return 0;
		}
		printk("Group deleted\n");
	}
}
