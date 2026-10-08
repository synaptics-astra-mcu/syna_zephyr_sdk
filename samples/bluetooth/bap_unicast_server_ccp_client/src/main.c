/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/autoconf.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/audio/ccp.h>
#include <zephyr/bluetooth/audio/lc3.h>
#include <zephyr/bluetooth/audio/pacs.h>
#include <zephyr/bluetooth/audio/tbs.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/byteorder.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
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
#include <zephyr/sys_clock.h>
#include <zephyr/types.h>

#include "stream_tx.h"

/* ── BAP audio context masks ─────────────────────────────────────────────── */

#define AVAILABLE_SINK_CONTEXT  (BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED   | \
				 BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | \
				 BT_AUDIO_CONTEXT_TYPE_MEDIA          | \
				 BT_AUDIO_CONTEXT_TYPE_GAME           | \
				 BT_AUDIO_CONTEXT_TYPE_INSTRUCTIONAL)

#define AVAILABLE_SOURCE_CONTEXT (BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED   | \
				  BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | \
				  BT_AUDIO_CONTEXT_TYPE_MEDIA          | \
				  BT_AUDIO_CONTEXT_TYPE_GAME)

/* ── BAP stream state ────────────────────────────────────────────────────── */

#if CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT > 0
NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT,
			  BT_ISO_SDU_BUF_SIZE(CONFIG_BT_ISO_TX_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);
#endif

static const struct bt_audio_codec_cap lc3_codec_cap = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_ANY, BT_AUDIO_CODEC_CAP_DURATION_10,
	BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1), 40u, 120u, 1u,
	(BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | BT_AUDIO_CONTEXT_TYPE_MEDIA));

static struct audio_sink {
	struct bt_bap_stream stream;
	size_t recv_cnt;
} sink_streams[CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT];

#if CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT > 0
static struct audio_source {
	struct bt_bap_stream stream;
	uint16_t seq_num;
	size_t send_cnt;
} source_streams[CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT];
static size_t configured_source_stream_count;
#endif

static const struct bt_bap_qos_cfg_pref qos_pref =
	BT_BAP_QOS_CFG_PREF(true, BT_GAP_LE_PHY_2M, 0x02, 10, 40000, 40000, 40000, 40000);

/* ── Advertising data ────────────────────────────────────────────────────── */

static uint8_t unicast_server_addata[] = {
	BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL),
	BT_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED,
	BT_BYTES_LIST_LE16(AVAILABLE_SINK_CONTEXT),
	BT_BYTES_LIST_LE16(AVAILABLE_SOURCE_CONTEXT),
	0x00,
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL)),
	BT_DATA(BT_DATA_SVC_DATA16, unicast_server_addata, ARRAY_SIZE(unicast_server_addata)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* ── LC3 decoder (optional) ──────────────────────────────────────────────── */

#if defined(CONFIG_LIBLC3)
#include "lc3.h"
#define MAX_SAMPLE_RATE       48000
#define MAX_FRAME_DURATION_US 10000
#define MAX_NUM_SAMPLES       ((MAX_FRAME_DURATION_US * MAX_SAMPLE_RATE) / USEC_PER_SEC)

static lc3_decoder_t lc3_decoder;
static lc3_decoder_mem_48k_t lc3_decoder_mem;
static int frames_per_sdu;
#endif

/* ── Shared connection state ─────────────────────────────────────────────── */

static struct bt_conn *default_conn;
static K_SEM_DEFINE(sem_disconnected, 0, 1);

/* ── CCP state ───────────────────────────────────────────────────────────── */

struct bt_ccp_call_control_client *ccp_client;
static struct bt_ccp_call_control_client_bearers ccp_bearers;
static struct k_work ccp_discover_work;

/* ── BAP helpers ─────────────────────────────────────────────────────────── */

void print_hex(const uint8_t *ptr, size_t len)
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

static void print_codec_cfg(const struct bt_audio_codec_cfg *codec_cfg)
{
	printk("codec_cfg 0x%02x cid 0x%04x vid 0x%04x count %u\n",
	       codec_cfg->id, codec_cfg->cid, codec_cfg->vid, codec_cfg->data_len);

	if (codec_cfg->id == BT_HCI_CODING_FORMAT_LC3) {
		enum bt_audio_location chan_allocation;
		int ret;

		bt_audio_data_parse(codec_cfg->data, codec_cfg->data_len, print_cb, "data");

		ret = bt_audio_codec_cfg_get_freq(codec_cfg);
		if (ret > 0) {
			printk("  Frequency: %d Hz\n",
			       bt_audio_codec_cfg_freq_to_freq_hz(ret));
		}

		ret = bt_audio_codec_cfg_get_frame_dur(codec_cfg);
		if (ret > 0) {
			printk("  Frame Duration: %d us\n",
			       bt_audio_codec_cfg_frame_dur_to_frame_dur_us(ret));
		}

		ret = bt_audio_codec_cfg_get_chan_allocation(codec_cfg, &chan_allocation, false);
		if (ret == 0) {
			printk("  Channel allocation: 0x%x\n", chan_allocation);
		}

		printk("  Octets per frame: %d\n",
		       bt_audio_codec_cfg_get_octets_per_frame(codec_cfg));
		printk("  Frames per SDU: %d\n",
		       bt_audio_codec_cfg_get_frame_blocks_per_sdu(codec_cfg, true));
	} else {
		print_hex(codec_cfg->data, codec_cfg->data_len);
	}

	bt_audio_data_parse(codec_cfg->meta, codec_cfg->meta_len, print_cb, "meta");
}

static void print_qos(const struct bt_bap_qos_cfg *qos)
{
	printk("QoS: interval %u framing 0x%02x phy 0x%02x sdu %u rtn %u latency %u pd %u\n",
	       qos->interval, qos->framing, qos->phy, qos->sdu,
	       qos->rtn, qos->latency, qos->pd);
}

static enum bt_audio_dir stream_dir(const struct bt_bap_stream *stream)
{
	for (size_t i = 0U; i < ARRAY_SIZE(sink_streams); i++) {
		if (stream == &sink_streams[i].stream) {
			return BT_AUDIO_DIR_SINK;
		}
	}

#if CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT > 0
	for (size_t i = 0U; i < ARRAY_SIZE(source_streams); i++) {
		if (stream == &source_streams[i].stream) {
			return BT_AUDIO_DIR_SOURCE;
		}
	}
#endif

	__ASSERT(false, "Invalid stream %p", stream);
	return 0;
}

static struct bt_bap_stream *stream_alloc(enum bt_audio_dir dir)
{
#if CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT > 0
	if (dir == BT_AUDIO_DIR_SOURCE) {
		for (size_t i = 0; i < ARRAY_SIZE(source_streams); i++) {
			if (!source_streams[i].stream.conn) {
				return &source_streams[i].stream;
			}
		}
		return NULL;
	}
#else
	ARG_UNUSED(dir);
#endif

	for (size_t i = 0; i < ARRAY_SIZE(sink_streams); i++) {
		if (!sink_streams[i].stream.conn) {
			return &sink_streams[i].stream;
		}
	}

	return NULL;
}

/* ── BAP Unicast Server callbacks ────────────────────────────────────────── */

static int lc3_config(struct bt_conn *conn, const struct bt_bap_ep *ep,
		      enum bt_audio_dir dir,
		      const struct bt_audio_codec_cfg *codec_cfg,
		      struct bt_bap_stream **stream,
		      struct bt_bap_qos_cfg_pref *const pref,
		      struct bt_bap_ascs_rsp *rsp)
{
	printk("ASE Codec Config: conn %p ep %p dir %u\n", conn, ep, dir);
	print_codec_cfg(codec_cfg);

	*stream = stream_alloc(dir);
	if (*stream == NULL) {
		printk("No streams available\n");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_NO_MEM,
					BT_BAP_ASCS_REASON_NONE);
		return -ENOMEM;
	}

	printk("ASE Codec Config stream %p\n", *stream);

#if CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT > 0
	if (dir == BT_AUDIO_DIR_SOURCE) {
		configured_source_stream_count++;
	}
#endif

	*pref = qos_pref;

#if defined(CONFIG_LIBLC3)
	lc3_decoder = NULL;
#endif

	return 0;
}

static int lc3_reconfig(struct bt_bap_stream *stream, enum bt_audio_dir dir,
			const struct bt_audio_codec_cfg *codec_cfg,
			struct bt_bap_qos_cfg_pref *const pref,
			struct bt_bap_ascs_rsp *rsp)
{
	printk("ASE Codec Reconfig: stream %p\n", stream);
	print_codec_cfg(codec_cfg);

#if defined(CONFIG_LIBLC3)
	lc3_decoder = NULL;
#endif

	*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED,
				BT_BAP_ASCS_REASON_NONE);
	return -ENOEXEC;
}

static int lc3_qos(struct bt_bap_stream *stream, const struct bt_bap_qos_cfg *qos,
		   struct bt_bap_ascs_rsp *rsp)
{
	printk("QoS: stream %p qos %p\n", stream, qos);
	print_qos(qos);
	return 0;
}

static int lc3_enable(struct bt_bap_stream *stream, const uint8_t meta[],
		      size_t meta_len, struct bt_bap_ascs_rsp *rsp)
{
	printk("Enable: stream %p meta_len %zu\n", stream, meta_len);

#if defined(CONFIG_LIBLC3)
	{
		int frame_duration_us;
		int freq;
		int ret;

		ret = bt_audio_codec_cfg_get_freq(stream->codec_cfg);
		if (ret > 0) {
			freq = bt_audio_codec_cfg_freq_to_freq_hz(ret);
		} else {
			printk("Error: Codec frequency not set\n");
			*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
						BT_BAP_ASCS_REASON_CODEC_DATA);
			return ret;
		}

		ret = bt_audio_codec_cfg_get_frame_dur(stream->codec_cfg);
		if (ret > 0) {
			frame_duration_us = bt_audio_codec_cfg_frame_dur_to_frame_dur_us(ret);
		} else {
			printk("Error: Frame duration not set\n");
			*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
						BT_BAP_ASCS_REASON_CODEC_DATA);
			return ret;
		}

		frames_per_sdu = bt_audio_codec_cfg_get_frame_blocks_per_sdu(
			stream->codec_cfg, true);

		lc3_decoder = lc3_setup_decoder(frame_duration_us, freq, 0,
						&lc3_decoder_mem);
		if (lc3_decoder == NULL) {
			printk("ERROR: Failed to setup LC3 decoder\n");
			*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
						BT_BAP_ASCS_REASON_CODEC_DATA);
			return -1;
		}
	}
#endif

	return 0;
}

static int lc3_start(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Start: stream %p\n", stream);

#if CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT > 0
	for (size_t i = 0U; i < configured_source_stream_count; i++) {
		if (stream == &source_streams[i].stream) {
			source_streams[i].seq_num = 0U;
			break;
		}
	}
#endif

	return 0;
}

static bool data_func_cb(struct bt_data *data, void *user_data)
{
	struct bt_bap_ascs_rsp *rsp = (struct bt_bap_ascs_rsp *)user_data;

	if (!BT_AUDIO_METADATA_TYPE_IS_KNOWN(data->type)) {
		printk("Invalid metadata type %u or length %u\n",
		       data->type, data->data_len);
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_METADATA_REJECTED,
					data->type);
		return -EINVAL;
	}

	return true;
}

static int lc3_metadata(struct bt_bap_stream *stream, const uint8_t meta[],
			size_t meta_len, struct bt_bap_ascs_rsp *rsp)
{
	printk("Metadata: stream %p meta_len %zu\n", stream, meta_len);
	return bt_audio_data_parse(meta, meta_len, data_func_cb, rsp);
}

static int lc3_disable(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Disable: stream %p\n", stream);
	return 0;
}

static int lc3_stop(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Stop: stream %p\n", stream);
	return 0;
}

static int lc3_release(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Release: stream %p\n", stream);
	return 0;
}

static struct bt_bap_unicast_server_register_param bap_server_param = {
	CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT,
	CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT,
};

static const struct bt_bap_unicast_server_cb unicast_server_cb = {
	.config   = lc3_config,
	.reconfig = lc3_reconfig,
	.qos      = lc3_qos,
	.enable   = lc3_enable,
	.start    = lc3_start,
	.metadata = lc3_metadata,
	.disable  = lc3_disable,
	.stop     = lc3_stop,
	.release  = lc3_release,
};

/* ── BAP stream ops ──────────────────────────────────────────────────────── */

#if defined(CONFIG_LIBLC3)
static void stream_recv_lc3_codec(struct bt_bap_stream *stream,
				  const struct bt_iso_recv_info *info,
				  struct net_buf *buf)
{
	static int16_t audio_buf[MAX_NUM_SAMPLES];
	struct audio_sink *s = CONTAINER_OF(stream, struct audio_sink, stream);
	const bool valid = (info->flags & BT_ISO_FLAGS_VALID) != 0;
	const int octets_per_frame = buf->len / frames_per_sdu;

	if (valid) {
		s->recv_cnt++;
		if ((s->recv_cnt % 100) == 0U) {
			printk("Audio stream %p: %zu frames received\n",
			       stream, s->recv_cnt);
		}
	}

	if (lc3_decoder == NULL) {
		return;
	}

	for (int i = 0; i < frames_per_sdu; i++) {
		const uint8_t *frame = valid ? net_buf_pull_mem(buf, octets_per_frame) : NULL;
		const int err = lc3_decode(lc3_decoder, frame, octets_per_frame,
					   LC3_PCM_FORMAT_S16, audio_buf, 1);

		if (err == 1) {
			printk("[%d]: LC3 PLC\n", i);
		} else if (err < 0) {
			printk("[%d]: LC3 decode error %d\n", i, err);
		}
	}
}
#else
static void stream_recv(struct bt_bap_stream *stream,
			const struct bt_iso_recv_info *info,
			struct net_buf *buf)
{
	if (info->flags & BT_ISO_FLAGS_VALID) {
		struct audio_sink *s = CONTAINER_OF(stream, struct audio_sink, stream);

		s->recv_cnt++;
		if ((s->recv_cnt % 100) == 0U) {
			printk("Audio stream %p len %u (%zu)\n",
			       stream, buf->len, s->recv_cnt);
		}
	}
}
#endif

static void stream_stopped(struct bt_bap_stream *stream, uint8_t reason)
{
	printk("Audio stream %p stopped (reason 0x%02x)\n", stream, reason);

	if (IS_ENABLED(CONFIG_BT_AUDIO_TX) &&
	    stream_dir(stream) == BT_AUDIO_DIR_SOURCE) {
		stream_tx_unregister(stream);
	}
}

static void stream_started(struct bt_bap_stream *stream)
{
	struct bt_iso_info info;

	bt_iso_chan_get_info(stream->iso, &info);
	printk("Audio stream %p started (CIG %u CIS %u)\n",
	       stream, info.unicast.cig_id, info.unicast.cis_id);

	if (stream_dir(stream) == BT_AUDIO_DIR_SINK) {
		struct audio_sink *s = CONTAINER_OF(stream, struct audio_sink, stream);

		s->recv_cnt = 0U;
	} else if (IS_ENABLED(CONFIG_BT_AUDIO_TX)) {
		stream_tx_register(stream);
	}
}

static void stream_enabled_cb(struct bt_bap_stream *stream)
{
	if (stream_dir(stream) == BT_AUDIO_DIR_SINK) {
		const int err = bt_bap_stream_start(stream);

		if (err != 0) {
			printk("Failed to start stream %p: %d\n", stream, err);
		}
	}
}

static struct bt_bap_stream_ops stream_ops = {
#if defined(CONFIG_LIBLC3)
	.recv    = stream_recv_lc3_codec,
#else
	.recv    = stream_recv,
#endif
	.stopped = stream_stopped,
	.started = stream_started,
	.enabled = stream_enabled_cb,
};

/* ── CCP callbacks ───────────────────────────────────────────────────────── */

static void ccp_discover_cb(struct bt_ccp_call_control_client *client, int err,
			    struct bt_ccp_call_control_client_bearers *bearers,
			    void *user_data)
{
	ARG_UNUSED(user_data);

	if (err != 0) {
		printk("CCP discover failed (err %d)\n", err);
		return;
	}

	printk("CCP discover complete: %s%u TBS bearer(s)\n",
	       bearers->gtbs_bearer != NULL ? "GTBS + " : "",
	       bearers->tbs_count);

	memcpy(&ccp_bearers, bearers, sizeof(ccp_bearers));
}

#if defined(CONFIG_BT_TBS_CLIENT_BEARER_PROVIDER_NAME)
static void ccp_bearer_name_cb(struct bt_ccp_call_control_client_bearer *bearer,
			       int err, const char *name, void *user_data)
{
	ARG_UNUSED(user_data);

	if (err != 0) {
		printk("Bearer name read failed (err %d)\n", err);
		return;
	}

	printk("Bearer %p provider name: %s\n", (void *)bearer, name);
}
#endif

static struct bt_ccp_call_control_client_cb ccp_cb = {
	.discover = ccp_discover_cb,
#if defined(CONFIG_BT_TBS_CLIENT_BEARER_PROVIDER_NAME)
	.bearer_provider_name = ccp_bearer_name_cb,
#endif
};

/* ── CCP discover work ───────────────────────────────────────────────────── */

static void ccp_discover_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (default_conn == NULL) {
		return;
	}

	printk("CCP: starting GTBS/TBS discovery\n");

	int err = bt_ccp_call_control_client_discover(default_conn, &ccp_client);

	if (err != 0) {
		printk("CCP discover failed to start (err %d)\n", err);
	}
}

/* ── Connection callbacks (shared by BAP + CCP) ──────────────────────────── */

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0) {
		printk("Connection failed: %s (err %u)\n", addr, err);
		return;
	}

	printk("Connected: %s\n", addr);
	default_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != default_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(default_conn);
	default_conn = NULL;
	ccp_client = NULL;
	memset(&ccp_bearers, 0, sizeof(ccp_bearers));

#if CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT > 0
	configured_source_stream_count = 0U;
#endif

	k_sem_give(&sem_disconnected);
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != BT_SECURITY_ERR_SUCCESS) {
		printk("Security failed: %s (err %d)\n", addr, err);
		return;
	}

	printk("Security level %d established: %s\n", level, addr);
	k_work_submit(&ccp_discover_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected      = connected,
	.disconnected   = disconnected,
	.security_changed = security_changed,
};

/* ── PACS capability caps ────────────────────────────────────────────────── */

static struct bt_pacs_cap cap_sink = {
	.codec_cap = &lc3_codec_cap,
};

static struct bt_pacs_cap cap_source = {
	.codec_cap = &lc3_codec_cap,
};

static int set_location(void)
{
	int err;

	if (IS_ENABLED(CONFIG_BT_PAC_SNK_LOC)) {
		err = bt_pacs_set_location(BT_AUDIO_DIR_SINK,
					   BT_AUDIO_LOCATION_FRONT_LEFT |
					   BT_AUDIO_LOCATION_FRONT_RIGHT);
		if (err != 0) {
			printk("Failed to set sink location (err %d)\n", err);
			return err;
		}
	}

	if (IS_ENABLED(CONFIG_BT_PAC_SRC_LOC)) {
		err = bt_pacs_set_location(BT_AUDIO_DIR_SOURCE,
					   BT_AUDIO_LOCATION_FRONT_CENTER);
		if (err != 0) {
			printk("Failed to set source location (err %d)\n", err);
			return err;
		}
	}

	return 0;
}

static int set_supported_contexts(void)
{
	int err;

	if (IS_ENABLED(CONFIG_BT_PAC_SNK)) {
		err = bt_pacs_set_supported_contexts(BT_AUDIO_DIR_SINK,
						     AVAILABLE_SINK_CONTEXT);
		if (err != 0) {
			printk("Failed to set sink supported contexts (err %d)\n", err);
			return err;
		}
	}

	if (IS_ENABLED(CONFIG_BT_PAC_SRC)) {
		err = bt_pacs_set_supported_contexts(BT_AUDIO_DIR_SOURCE,
						     AVAILABLE_SOURCE_CONTEXT);
		if (err != 0) {
			printk("Failed to set source supported contexts (err %d)\n", err);
			return err;
		}
	}

	return 0;
}

static int set_available_contexts(void)
{
	int err;

	if (IS_ENABLED(CONFIG_BT_PAC_SNK)) {
		err = bt_pacs_set_available_contexts(BT_AUDIO_DIR_SINK,
						     AVAILABLE_SINK_CONTEXT);
		if (err != 0) {
			printk("Failed to set sink available contexts (err %d)\n", err);
			return err;
		}
	}

	if (IS_ENABLED(CONFIG_BT_PAC_SRC)) {
		err = bt_pacs_set_available_contexts(BT_AUDIO_DIR_SOURCE,
						     AVAILABLE_SOURCE_CONTEXT);
		if (err != 0) {
			printk("Failed to set source available contexts (err %d)\n", err);
			return err;
		}
	}

	return 0;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
	struct bt_le_ext_adv *adv;
	const struct bt_pacs_register_param pacs_param = {
		.snk_pac = true,
		.snk_loc = true,
		.src_pac = true,
		.src_loc = true,
	};
	int err;

	k_work_init(&ccp_discover_work, ccp_discover_work_fn);

	err = bt_enable(NULL);
	if (err != 0) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	/* BAP Unicast Server setup */
	err = bt_pacs_register(&pacs_param);
	if (err != 0) {
		printk("Failed to register PACS (err %d)\n", err);
		return 0;
	}

	if (IS_ENABLED(CONFIG_BT_AUDIO_TX)) {
		stream_tx_init();
	}

	bt_bap_unicast_server_register(&bap_server_param);
	bt_bap_unicast_server_register_cb(&unicast_server_cb);
	bt_pacs_cap_register(BT_AUDIO_DIR_SINK, &cap_sink);
	bt_pacs_cap_register(BT_AUDIO_DIR_SOURCE, &cap_source);

	for (size_t i = 0; i < ARRAY_SIZE(sink_streams); i++) {
		bt_bap_stream_cb_register(&sink_streams[i].stream, &stream_ops);
	}

#if CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT > 0
	for (size_t i = 0; i < ARRAY_SIZE(source_streams); i++) {
		bt_bap_stream_cb_register(&source_streams[i].stream, &stream_ops);
	}
#endif

	err = set_location();
	if (err != 0) {
		return 0;
	}

	err = set_supported_contexts();
	if (err != 0) {
		return 0;
	}

	err = set_available_contexts();
	if (err != 0) {
		return 0;
	}

	/* CCP Call Control Client setup */
	err = bt_ccp_call_control_client_register_cb(&ccp_cb);
	if (err != 0) {
		printk("CCP callback registration failed (err %d)\n", err);
		return 0;
	}

	printk("BAP Unicast Server + CCP Call Control Client initialized\n");

	/* Create extended advertising set with BAP announcement */
	err = bt_le_ext_adv_create(BT_BAP_ADV_PARAM_CONN_QUICK, NULL, &adv);
	if (err != 0) {
		printk("Failed to create advertising set (err %d)\n", err);
		return 0;
	}

	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err != 0) {
		printk("Failed to set advertising data (err %d)\n", err);
		return 0;
	}

	while (true) {
		err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
		if (err != 0) {
			printk("Failed to start advertising (err %d)\n", err);
			return 0;
		}

		printk("Advertising as \"%s\" — waiting for peer\n",
		       CONFIG_BT_DEVICE_NAME);

		k_sem_take(&sem_disconnected, K_FOREVER);

		printk("Peer disconnected — restarting advertising\n");
	}

	return 0;
}
