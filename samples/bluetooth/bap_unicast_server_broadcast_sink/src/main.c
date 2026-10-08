/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <zephyr/autoconf.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/audio/lc3.h>
#include <zephyr/bluetooth/audio/pacs.h>
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
#include "stream_rx.h"

/* SR110 stub implementations; forward-declared here to avoid header name
 * conflicts with the ASCS lc3_enable/lc3_disable callbacks defined below. */
int lc3_init(void);
int usb_init(void);


/* ═══════════════════════════════════════════════════════════════════════════
 * SHARED CODEC CAP (used by both unicast server PACS and broadcast sink PACS)
 * ═══════════════════════════════════════════════════════════════════════════ */

static const struct bt_audio_codec_cap lc3_codec_cap = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_ANY, BT_AUDIO_CODEC_CAP_DURATION_10,
	BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1), 40u, 120u, 1u,
	(BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | BT_AUDIO_CONTEXT_TYPE_MEDIA));

static const struct bt_audio_codec_cap bcast_codec_cap = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_16KHZ | BT_AUDIO_CODEC_CAP_FREQ_24KHZ,
	BT_AUDIO_CODEC_CAP_DURATION_10, BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1), 40u, 60u,
	CONFIG_MAX_CODEC_FRAMES_PER_SDU,
	(BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | BT_AUDIO_CONTEXT_TYPE_MEDIA));

/* ═══════════════════════════════════════════════════════════════════════════
 * UNICAST SERVER
 * ═══════════════════════════════════════════════════════════════════════════ */

#define AVAILABLE_SINK_CONTEXT  (BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED   | \
				 BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | \
				 BT_AUDIO_CONTEXT_TYPE_MEDIA          | \
				 BT_AUDIO_CONTEXT_TYPE_GAME           | \
				 BT_AUDIO_CONTEXT_TYPE_INSTRUCTIONAL)

#define AVAILABLE_SOURCE_CONTEXT (BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED   | \
				  BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | \
				  BT_AUDIO_CONTEXT_TYPE_MEDIA          | \
				  BT_AUDIO_CONTEXT_TYPE_GAME)

static const struct bt_bap_qos_cfg_pref qos_pref =
	BT_BAP_QOS_CFG_PREF(true, BT_GAP_LE_PHY_2M, 0x02, 10, 40000, 40000, 40000, 40000);

static uint8_t unicast_server_addata[] = {
	BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL),
	BT_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED,
	BT_BYTES_LIST_LE16(AVAILABLE_SINK_CONTEXT),
	BT_BYTES_LIST_LE16(AVAILABLE_SOURCE_CONTEXT),
	0x00,
};

static const struct bt_data unicast_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA_BYTES(BT_DATA_UUID16_SOME, BT_UUID_16_ENCODE(BT_UUID_CAS_VAL)),
	BT_DATA_BYTES(BT_DATA_SVC_DATA16,
		      BT_UUID_16_ENCODE(BT_UUID_CAS_VAL),
		      BT_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED),
	BT_DATA(BT_DATA_SVC_DATA16, unicast_server_addata, ARRAY_SIZE(unicast_server_addata)),
	IF_ENABLED(CONFIG_BT_BAP_SCAN_DELEGATOR,
		   (BT_DATA_BYTES(BT_DATA_SVC_DATA16,
				  BT_UUID_16_ENCODE(BT_UUID_BASS_VAL)),))
};

#if defined(CONFIG_LIBLC3)
#define MAX_SAMPLE_RATE       48000
#define MAX_FRAME_DURATION_US 10000
#define MAX_NUM_SAMPLES       ((MAX_FRAME_DURATION_US * MAX_SAMPLE_RATE) / USEC_PER_SEC)

static lc3_decoder_t lc3_decoder;
static lc3_decoder_mem_48k_t lc3_decoder_mem;
static int frames_per_sdu;
#endif

static struct audio_sink {
	struct bt_bap_stream stream;
	size_t recv_cnt;
} sink_streams[CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT];

static struct bt_conn *default_conn;
static K_SEM_DEFINE(sem_ucast_disconnected, 0, 1);
static K_SEM_DEFINE(sem_security_updated, 0, 1);

static void ucast_print_hex(const uint8_t *ptr, size_t len)
{
	while (len-- != 0) {
		printk("%02x", *ptr++);
	}
}

static bool ucast_print_cb(struct bt_data *data, void *user_data)
{
	const char *str = (const char *)user_data;

	printk("%s: type 0x%02x value_len %u\n", str, data->type, data->data_len);
	ucast_print_hex(data->data, data->data_len);
	printk("\n");
	return true;
}

static void print_codec_cfg(const struct bt_audio_codec_cfg *codec_cfg)
{
	printk("codec_cfg 0x%02x cid 0x%04x vid 0x%04x count %u\n",
	       codec_cfg->id, codec_cfg->cid, codec_cfg->vid, codec_cfg->data_len);

	if (codec_cfg->id == BT_HCI_CODING_FORMAT_LC3) {
		int ret;

		bt_audio_data_parse(codec_cfg->data, codec_cfg->data_len, ucast_print_cb, "data");

		ret = bt_audio_codec_cfg_get_freq(codec_cfg);
		if (ret > 0) {
			printk("  Frequency: %d Hz\n", bt_audio_codec_cfg_freq_to_freq_hz(ret));
		}

		ret = bt_audio_codec_cfg_get_frame_dur(codec_cfg);
		if (ret > 0) {
			printk("  Frame Duration: %d us\n",
			       bt_audio_codec_cfg_frame_dur_to_frame_dur_us(ret));
		}

		printk("  Octets per frame: %d\n",
		       bt_audio_codec_cfg_get_octets_per_frame(codec_cfg));
	} else {
		ucast_print_hex(codec_cfg->data, codec_cfg->data_len);
	}

	bt_audio_data_parse(codec_cfg->meta, codec_cfg->meta_len, ucast_print_cb, "meta");
}

static enum bt_audio_dir ucast_stream_dir(const struct bt_bap_stream *stream)
{
	for (size_t i = 0U; i < ARRAY_SIZE(sink_streams); i++) {
		if (stream == &sink_streams[i].stream) {
			return BT_AUDIO_DIR_SINK;
		}
	}
	__ASSERT(false, "Invalid stream %p", stream);
	return 0;
}

static struct bt_bap_stream *ucast_stream_alloc(enum bt_audio_dir dir)
{
	ARG_UNUSED(dir);

	for (size_t i = 0; i < ARRAY_SIZE(sink_streams); i++) {
		if (!sink_streams[i].stream.conn) {
			return &sink_streams[i].stream;
		}
	}

	return NULL;
}

static int lc3_config(struct bt_conn *conn, const struct bt_bap_ep *ep,
		      enum bt_audio_dir dir, const struct bt_audio_codec_cfg *codec_cfg,
		      struct bt_bap_stream **stream, struct bt_bap_qos_cfg_pref *const pref,
		      struct bt_bap_ascs_rsp *rsp)
{
	printk("ASE Codec Config: conn %p ep %p dir %u\n", conn, ep, dir);
	print_codec_cfg(codec_cfg);

	*stream = ucast_stream_alloc(dir);
	if (*stream == NULL) {
		printk("No streams available\n");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_NO_MEM, BT_BAP_ASCS_REASON_NONE);
		return -ENOMEM;
	}

	*pref = qos_pref;

#if defined(CONFIG_LIBLC3)
	lc3_decoder = NULL;
#endif

	return 0;
}

static int lc3_reconfig(struct bt_bap_stream *stream, enum bt_audio_dir dir,
			const struct bt_audio_codec_cfg *codec_cfg,
			struct bt_bap_qos_cfg_pref *const pref, struct bt_bap_ascs_rsp *rsp)
{
	printk("ASE Codec Reconfig: stream %p\n", stream);
#if defined(CONFIG_LIBLC3)
	lc3_decoder = NULL;
#endif
	*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED, BT_BAP_ASCS_REASON_NONE);
	return -ENOEXEC;
}

static int lc3_qos(struct bt_bap_stream *stream, const struct bt_bap_qos_cfg *qos,
		   struct bt_bap_ascs_rsp *rsp)
{
	printk("QoS: stream %p interval %u sdu %u\n", stream, qos->interval, qos->sdu);
	return 0;
}

static int lc3_enable(struct bt_bap_stream *stream, const uint8_t meta[],
		      size_t meta_len, struct bt_bap_ascs_rsp *rsp)
{
	printk("Enable: stream %p meta_len %zu\n", stream, meta_len);

#if defined(CONFIG_LIBLC3)
	{
		int freq, frame_duration_us, ret;

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

		lc3_decoder = lc3_setup_decoder(frame_duration_us, freq, 0, &lc3_decoder_mem);
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
	return 0;
}

static bool data_func_cb(struct bt_data *data, void *user_data)
{
	struct bt_bap_ascs_rsp *rsp = (struct bt_bap_ascs_rsp *)user_data;

	if (!BT_AUDIO_METADATA_TYPE_IS_KNOWN(data->type)) {
		printk("Invalid metadata type %u\n", data->type);
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_METADATA_REJECTED, data->type);
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

#if defined(CONFIG_LIBLC3)
static void ucast_stream_recv_lc3(struct bt_bap_stream *stream,
				  const struct bt_iso_recv_info *info,
				  struct net_buf *buf)
{
	static int16_t audio_buf[MAX_NUM_SAMPLES];
	struct audio_sink *s = CONTAINER_OF(stream, struct audio_sink, stream);
	const bool valid = (info->flags & BT_ISO_FLAGS_VALID) != 0;
	const int octets_per_frame = frames_per_sdu > 0 ? buf->len / frames_per_sdu : buf->len;

	if (valid) {
		s->recv_cnt++;
		if ((s->recv_cnt % 100) == 0U) {
			printk("Audio stream %p: %zu frames received\n", stream, s->recv_cnt);
		}
	}

	if (lc3_decoder == NULL) {
		return;
	}

	for (int i = 0; i < frames_per_sdu; i++) {
		const uint8_t *frame = valid ? net_buf_pull_mem(buf, octets_per_frame) : NULL;
		const int ret = lc3_decode(lc3_decoder, frame, octets_per_frame,
					   LC3_PCM_FORMAT_S16, audio_buf, 1);

		if (ret == 1) {
			printk("[%d]: LC3 PLC\n", i);
		} else if (ret < 0) {
			printk("[%d]: LC3 decode error %d\n", i, ret);
		}
	}
}
#else
static void ucast_stream_recv(struct bt_bap_stream *stream,
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

static void ucast_stream_stopped(struct bt_bap_stream *stream, uint8_t reason)
{
	printk("Audio stream %p stopped (reason 0x%02x)\n", stream, reason);

	if (IS_ENABLED(CONFIG_BT_AUDIO_TX) &&
	    ucast_stream_dir(stream) == BT_AUDIO_DIR_SOURCE) {
		stream_tx_unregister(stream);
	}
}

static void ucast_stream_started(struct bt_bap_stream *stream)
{
	struct bt_iso_info info;

	bt_iso_chan_get_info(stream->iso, &info);
	printk("Audio stream %p started (CIG %u CIS %u)\n",
	       stream, info.unicast.cig_id, info.unicast.cis_id);

	if (ucast_stream_dir(stream) == BT_AUDIO_DIR_SINK) {
		struct audio_sink *s = CONTAINER_OF(stream, struct audio_sink, stream);

		s->recv_cnt = 0U;
	}
}

static void ucast_stream_enabled_cb(struct bt_bap_stream *stream)
{
	if (ucast_stream_dir(stream) == BT_AUDIO_DIR_SINK) {
		const int err = bt_bap_stream_start(stream);

		if (err != 0) {
			printk("Failed to start stream %p: %d\n", stream, err);
		}
	}
}

static struct bt_bap_stream_ops ucast_stream_ops = {
#if defined(CONFIG_LIBLC3)
	.recv    = ucast_stream_recv_lc3,
#else
	.recv    = ucast_stream_recv,
#endif
	.stopped = ucast_stream_stopped,
	.started = ucast_stream_started,
	.enabled = ucast_stream_enabled_cb,
};

static struct bt_pacs_cap ucast_cap_sink = {
	.codec_cap = &lc3_codec_cap,
};

static struct bt_pacs_cap ucast_cap_source = {
	.codec_cap = &lc3_codec_cap,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * BROADCAST SINK
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SEM_TIMEOUT                 K_SECONDS(60)
#define PA_SYNC_INTERVAL_TO_TIMEOUT_RATIO 10
#define PA_SYNC_SKIP                1
#define NAME_LEN                    (sizeof(CONFIG_TARGET_BROADCAST_NAME) + 1)

static K_SEM_DEFINE(sem_broadcast_sink_stopped, 0U, 1U);
static struct bt_le_ext_adv *unicast_adv;
static K_SEM_DEFINE(sem_broadcaster_found, 0U, 1U);
static K_SEM_DEFINE(sem_pa_synced, 0U, 1U);
static K_SEM_DEFINE(sem_base_received, 0U, 1U);
static K_SEM_DEFINE(sem_syncable, 0U, 1U);
static K_SEM_DEFINE(sem_pa_sync_lost, 0U, 1U);
static K_SEM_DEFINE(sem_pa_sync_requested, 0U, 1U);
static K_SEM_DEFINE(sem_broadcast_code_received, 0U, 1U);
static K_SEM_DEFINE(sem_bis_sync_requested, 0U, 1U);
static K_SEM_DEFINE(sem_bcast_stream_connected, 0U, CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT);
static K_SEM_DEFINE(sem_bcast_stream_started, 0U, CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT);
static K_SEM_DEFINE(sem_big_synced, 0U, 1U);

static const struct bt_bap_scan_delegator_recv_state *req_recv_state;
static struct bt_bap_broadcast_sink *broadcast_sink;
static struct bt_le_scan_recv_info broadcaster_info;
static bt_addr_le_t broadcaster_addr;
static struct bt_le_per_adv_sync *pa_sync;
static uint32_t broadcaster_broadcast_id;
static struct bt_bap_stream *bap_streams_p[CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT];
static volatile bool big_synced;
static volatile bool base_received;
static struct bt_le_ext_adv *bcast_ext_adv;

struct bis_audio_allocation {
	bool valid;
	enum bt_audio_location value;
};

struct base_subgroup_data {
	uint32_t bis_index_bitfield;
	struct bis_audio_allocation audio_allocation[BT_ISO_BIS_INDEX_MAX + 1];
};

struct base_data {
	struct base_subgroup_data subgroup_bis[CONFIG_BT_BAP_BASS_MAX_SUBGROUPS];
	uint8_t subgroup_cnt;
};

static struct base_data base_recv_data;
static uint32_t requested_bis_sync[CONFIG_BT_BAP_BASS_MAX_SUBGROUPS];
static uint8_t sink_broadcast_code[BT_ISO_BROADCAST_CODE_SIZE];

static struct bt_pacs_cap bcast_cap_sink = {
	.codec_cap = &bcast_codec_cap,
};

static void bcast_stream_connected_cb(struct bt_bap_stream *bap_stream)
{
	printk("Broadcast stream %p connected\n", bap_stream);
	k_sem_give(&sem_bcast_stream_connected);
}

static void bcast_stream_disconnected_cb(struct bt_bap_stream *bap_stream, uint8_t reason)
{
	printk("Broadcast stream %p disconnected (reason 0x%02X)\n", bap_stream, reason);
	k_sem_take(&sem_bcast_stream_connected, K_NO_WAIT);
}

static void bcast_stream_started_cb(struct bt_bap_stream *bap_stream)
{
	struct bt_iso_info info;
	int err;

	err = bt_iso_chan_get_info(bap_stream->iso, &info);
	__ASSERT(err == 0, "bt_iso_chan_get_info failed: %d", err);

	printk("Broadcast stream %p started (BIG %u BIS %u)\n", bap_stream,
	       info.sync_receiver.big_handle, info.sync_receiver.bis_number);

	err = stream_rx_started(bap_stream);
	if (err != 0) {
		printk("stream_rx_started error: %d\n", err);
	}

	k_sem_give(&sem_bcast_stream_started);
}

static void bcast_stream_stopped_cb(struct bt_bap_stream *bap_stream, uint8_t reason)
{
	printk("Broadcast stream %p stopped (reason 0x%02X)\n", bap_stream, reason);
	stream_rx_stopped(bap_stream);
	k_sem_take(&sem_bcast_stream_started, K_NO_WAIT);
}

static void bcast_stream_recv_cb(struct bt_bap_stream *bap_stream,
				 const struct bt_iso_recv_info *info, struct net_buf *buf)
{
	stream_rx_recv(bap_stream, info, buf);
}

static struct bt_bap_stream_ops bcast_stream_ops = {
	.connected    = bcast_stream_connected_cb,
	.disconnected = bcast_stream_disconnected_cb,
	.started      = bcast_stream_started_cb,
	.stopped      = bcast_stream_stopped_cb,
	.recv         = bcast_stream_recv_cb,
};

static void base_recv_cb(struct bt_bap_broadcast_sink *sink, const struct bt_bap_base *base,
			 size_t base_size)
{
	printk("Received BASE with %d subgroup(s) from sink %p\n",
	       bt_bap_base_get_subgroup_count(base), sink);

	(void)memset(&base_recv_data, 0, sizeof(base_recv_data));

	base_received = true;
	k_sem_give(&sem_base_received);
}

static void syncable_cb(struct bt_bap_broadcast_sink *sink, const struct bt_iso_biginfo *biginfo)
{
	printk("Broadcast sink (%p) syncable, BIG %s\n", (void *)sink,
	       biginfo->encryption ? "encrypted" : "unencrypted");

	k_sem_give(&sem_syncable);

	if (!biginfo->encryption) {
		k_sem_give(&sem_broadcast_code_received);
	}
}

static void broadcast_sink_started_cb(struct bt_bap_broadcast_sink *sink)
{
	printk("Broadcast sink %p started\n", sink);
	big_synced = true;
	k_sem_give(&sem_big_synced);
}

static void broadcast_sink_stopped_cb(struct bt_bap_broadcast_sink *sink, uint8_t reason)
{
	printk("Broadcast sink %p stopped (reason 0x%02X)\n", sink, reason);
	big_synced = false;
	k_sem_give(&sem_broadcast_sink_stopped);
}

static struct bt_bap_broadcast_sink_cb broadcast_sink_cbs = {
	.base_recv = base_recv_cb,
	.syncable  = syncable_cb,
	.started   = broadcast_sink_started_cb,
	.stopped   = broadcast_sink_stopped_cb,
};

static uint16_t interval_to_sync_timeout(uint16_t pa_interval)
{
	if (pa_interval == BT_BAP_PA_INTERVAL_UNKNOWN) {
		return BT_GAP_PER_ADV_MAX_TIMEOUT;
	}

	uint32_t interval_us = BT_GAP_PER_ADV_INTERVAL_TO_US(pa_interval);
	uint32_t timeout = BT_GAP_US_TO_PER_ADV_SYNC_TIMEOUT(interval_us) *
			   PA_SYNC_INTERVAL_TO_TIMEOUT_RATIO;

	return (uint16_t)CLAMP(timeout, BT_GAP_PER_ADV_MIN_TIMEOUT, BT_GAP_PER_ADV_MAX_TIMEOUT);
}

static bool is_substring(const char *substr, const char *str)
{
	const size_t str_len = strlen(str);
	const size_t sub_len = strlen(substr);

	if (sub_len > str_len) {
		return false;
	}

	for (size_t pos = 0U; pos < str_len; pos++) {
		if (pos + sub_len > str_len) {
			return false;
		}
		if (strncasecmp(substr, &str[pos], sub_len) == 0) {
			return true;
		}
	}

	return false;
}

static bool data_cb(struct bt_data *data, void *user_data)
{
	bool *device_found = user_data;
	char name[NAME_LEN] = {0};

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_BROADCAST_NAME:
		memcpy(name, data->data, MIN(data->data_len, NAME_LEN - 1));
		if (is_substring(CONFIG_TARGET_BROADCAST_NAME, name)) {
			*device_found = true;
			return false;
		}
		return true;
	default:
		return true;
	}
}

static bool scan_check_and_sync_broadcast(struct bt_data *data, void *user_data)
{
	const struct bt_le_scan_recv_info *info = user_data;
	struct bt_uuid_16 adv_uuid;
	uint32_t broadcast_id;
	char le_addr[BT_ADDR_LE_STR_LEN];

	if (data->type != BT_DATA_SVC_DATA16) {
		return true;
	}
	if (data->data_len < BT_UUID_SIZE_16 + BT_AUDIO_BROADCAST_ID_SIZE) {
		return true;
	}
	if (!bt_uuid_create(&adv_uuid.uuid, data->data, BT_UUID_SIZE_16)) {
		return true;
	}
	if (bt_uuid_cmp(&adv_uuid.uuid, BT_UUID_BROADCAST_AUDIO)) {
		return true;
	}

	broadcast_id = sys_get_le24(data->data + BT_UUID_SIZE_16);

	if (req_recv_state != NULL &&
	    (!bt_addr_le_eq(info->addr, &req_recv_state->addr) ||
	     info->sid != req_recv_state->adv_sid ||
	     broadcast_id != req_recv_state->broadcast_id)) {
		return true;
	}

	bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));

	printk("Found broadcaster: ID 0x%06X addr %s sid 0x%02X\n",
	       broadcast_id, le_addr, info->sid);

	memcpy(&broadcaster_info, info, sizeof(broadcaster_info));
	bt_addr_le_copy(&broadcaster_addr, info->addr);
	broadcaster_broadcast_id = broadcast_id;
	k_sem_give(&sem_broadcaster_found);

	return false; /* stop parsing */
}

static void broadcast_scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *ad)
{
	if (info->interval == 0U) {
		/* Not an extended adv with PA */
		return;
	}

	if (strlen(CONFIG_TARGET_BROADCAST_NAME) > 0U) {
		bool device_found = false;
		struct net_buf_simple buf_copy;

		net_buf_simple_clone(ad, &buf_copy);
		bt_data_parse(&buf_copy, data_cb, &device_found);

		if (!device_found) {
			return;
		}
	}

	bt_data_parse(ad, scan_check_and_sync_broadcast, (void *)info);
}

static struct bt_le_scan_cb bcast_scan_cb = {
	.recv = broadcast_scan_recv,
};

static void bap_pa_sync_synced_cb(struct bt_le_per_adv_sync *sync,
				  struct bt_le_per_adv_sync_synced_info *info)
{
	if (sync == pa_sync) {
		printk("PA sync %p synced for broadcast ID 0x%06X\n",
		       sync, broadcaster_broadcast_id);
		k_sem_give(&sem_pa_synced);
	}
}

static void bap_pa_sync_terminated_cb(struct bt_le_per_adv_sync *sync,
				      const struct bt_le_per_adv_sync_term_info *info)
{
	if (sync == pa_sync) {
		printk("PA sync %p lost (reason 0x%02X)\n", sync, info->reason);
		pa_sync = NULL;
		k_sem_give(&sem_pa_sync_lost);
	}
}

static struct bt_le_per_adv_sync_cb bap_pa_sync_cb = {
	.synced = bap_pa_sync_synced_cb,
	.term   = bap_pa_sync_terminated_cb,
};

static int pa_sync_req_cb(struct bt_conn *conn,
			  const struct bt_bap_scan_delegator_recv_state *recv_state,
			  bool past_avail, uint16_t pa_interval)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(past_avail);
	ARG_UNUSED(pa_interval);

	req_recv_state = recv_state;
	broadcaster_broadcast_id = recv_state->broadcast_id;
	(void)memset(requested_bis_sync, 0, sizeof(requested_bis_sync));
	k_sem_reset(&sem_bis_sync_requested);
	printk("PA sync request for broadcast ID 0x%06X\n", broadcaster_broadcast_id);
	k_sem_give(&sem_pa_sync_requested);

	return 0;
}

static int pa_sync_term_req_cb(struct bt_conn *conn,
			       const struct bt_bap_scan_delegator_recv_state *recv_state)
{
	ARG_UNUSED(conn);

	if (pa_sync != NULL) {
		int err = bt_le_per_adv_sync_delete(pa_sync);

		if (err != 0) {
			printk("Failed to terminate PA sync: %d\n", err);
			return err;
		}
	}

	if (recv_state == req_recv_state) {
		req_recv_state = NULL;
		broadcaster_broadcast_id = BT_BAP_INVALID_BROADCAST_ID;
		(void)memset(requested_bis_sync, 0, sizeof(requested_bis_sync));
		(void)memset(sink_broadcast_code, 0, sizeof(sink_broadcast_code));
	}

	return 0;
}

static void broadcast_code_cb(struct bt_conn *conn,
			      const struct bt_bap_scan_delegator_recv_state *recv_state,
			      const uint8_t broadcast_code[BT_ISO_BROADCAST_CODE_SIZE])
{
	ARG_UNUSED(conn);

	req_recv_state = recv_state;
	(void)memcpy(sink_broadcast_code, broadcast_code, BT_ISO_BROADCAST_CODE_SIZE);
	printk("Broadcast code received for ID 0x%06X\n", recv_state->broadcast_id);
	k_sem_give(&sem_broadcast_code_received);
}

static int bis_sync_req_cb(struct bt_conn *conn,
			   const struct bt_bap_scan_delegator_recv_state *recv_state,
			   const uint32_t bis_sync_req[CONFIG_BT_BAP_BASS_MAX_SUBGROUPS])
{
	bool has_req = false;

	ARG_UNUSED(conn);
	req_recv_state = recv_state;

	(void)memset(requested_bis_sync, 0, sizeof(requested_bis_sync));

	for (uint8_t i = 0U; i < MIN(recv_state->num_subgroups, CONFIG_BT_BAP_BASS_MAX_SUBGROUPS); i++) {
		if (bis_sync_req[i] != 0U) {
			requested_bis_sync[i] = bis_sync_req[i];
			has_req = true;
		}
	}

	if (has_req) {
		printk("BIS sync request received\n");
		k_sem_give(&sem_bis_sync_requested);
	}

	return 0;
}

static struct bt_bap_scan_delegator_cb scan_delegator_cbs = {
	.pa_sync_req = pa_sync_req_cb,
	.pa_sync_term_req = pa_sync_term_req_cb,
	.broadcast_code = broadcast_code_cb,
	.bis_sync_req = bis_sync_req_cb,
};

static int pa_sync_create(void)
{
	struct bt_le_per_adv_sync_param create_params = {0};
	struct bt_le_local_features feature;
	int err;

	err = bt_le_get_local_features(&feature);
	if (err < 0) {
		printk("Failed to get local features: %d\n", err);
		return err;
	}

	bt_addr_le_copy(&create_params.addr, &broadcaster_addr);

	if (BT_FEAT_LE_PER_ADV_ADI_SUPP(feature.features)) {
		create_params.options = BT_LE_PER_ADV_SYNC_OPT_FILTER_DUPLICATE;
	} else {
		create_params.options = BT_LE_PER_ADV_SYNC_OPT_NONE;
	}

	create_params.sid     = broadcaster_info.sid;
	create_params.skip    = PA_SYNC_SKIP;
	create_params.timeout = interval_to_sync_timeout(broadcaster_info.interval);

	return bt_le_per_adv_sync_create(&create_params, &pa_sync);
}

static uint8_t get_stream_count(uint32_t bitfield)
{
	uint8_t count = 0U;

	for (uint8_t i = 0U; i < BT_ISO_MAX_GROUP_ISO_COUNT; i++) {
		if ((bitfield & BIT(i)) != 0) {
			count++;
		}
	}

	return count;
}

static uint32_t keep_n_lsb_ones(uint32_t bitfield, uint8_t n)
{
	uint32_t result = 0U;

	for (uint8_t i = 0U; i < n && bitfield != 0; i++) {
		uint32_t lsb = bitfield & -bitfield;

		result |= lsb;
		bitfield &= ~lsb;
	}

	return result;
}

static uint32_t select_bis_sync_bitfield(void)
{
	uint32_t result = 0U;

	for (uint8_t i = 0U; i < CONFIG_BT_BAP_BASS_MAX_SUBGROUPS; i++) {
		if (requested_bis_sync[i] != 0) {
			result |= requested_bis_sync[i];
		}
	}

	/* Limit to max supported stream count */
	result = keep_n_lsb_ones(result, CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT);

	return result;
}

static int bcast_sink_reset(void)
{
	int err;

	printk("Broadcast sink reset\n");

	big_synced     = false;
	base_received  = false;

	(void)memset(&base_recv_data, 0, sizeof(base_recv_data));
	(void)memset(&requested_bis_sync, 0, sizeof(requested_bis_sync));
	(void)memset(&broadcaster_info, 0, sizeof(broadcaster_info));
	(void)memset(&broadcaster_addr, 0, sizeof(broadcaster_addr));

	if (broadcast_sink != NULL) {
		err = bt_bap_broadcast_sink_delete(broadcast_sink);
		if (err != 0) {
			printk("Failed to delete broadcast sink: %d\n", err);
			return err;
		}
		broadcast_sink = NULL;
	}

	if (pa_sync != NULL) {
		bt_le_per_adv_sync_delete(pa_sync);
		pa_sync = NULL;
	}

	k_sem_reset(&sem_broadcaster_found);
	k_sem_reset(&sem_pa_synced);
	k_sem_reset(&sem_base_received);
	k_sem_reset(&sem_syncable);
	k_sem_reset(&sem_pa_sync_lost);
	k_sem_reset(&sem_broadcast_code_received);
	k_sem_reset(&sem_bis_sync_requested);
	k_sem_reset(&sem_bcast_stream_connected);
	k_sem_reset(&sem_bcast_stream_started);
	k_sem_reset(&sem_broadcast_sink_stopped);
	k_sem_reset(&sem_big_synced);

	return 0;
}

static void run_broadcast_sink(void)
{
	int err;

	while (true) {
		uint32_t sync_bitfield;
		uint8_t stream_count;

		err = bcast_sink_reset();
		if (err != 0) {
			printk("Sink reset failed: %d\n", err);
			return;
		}

		if (req_recv_state == NULL) {
			printk("Waiting for assistant PA sync request\n");
			err = k_sem_take(&sem_pa_sync_requested, K_FOREVER);
			if (err != 0) {
				printk("PA sync request wait failed: %d\n", err);
				continue;
			}
		}

		broadcaster_broadcast_id = req_recv_state->broadcast_id;

		printk("Scanning for broadcast sources%s%s\n",
		       strlen(CONFIG_TARGET_BROADCAST_NAME) > 0 ? " named `" : "",
		       strlen(CONFIG_TARGET_BROADCAST_NAME) > 0 ? CONFIG_TARGET_BROADCAST_NAME : "");

		err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, NULL);
		if (err != 0 && err != -EALREADY) {
			printk("Unable to start scan: %d\n", err);
			return;
		}

		err = k_sem_take(&sem_broadcaster_found, SEM_TIMEOUT);
		if (err != 0) {
			printk("Broadcaster not found (timeout), retrying\n");
			bt_le_scan_stop();
			continue;
		}

		err = bt_le_scan_stop();
		if (err != 0) {
			printk("bt_le_scan_stop failed: %d\n", err);
			continue;
		}

		printk("PA syncing to broadcaster ID 0x%06X\n", broadcaster_broadcast_id);
		err = pa_sync_create();
		if (err != 0) {
			printk("PA sync create failed: %d\n", err);
			continue;
		}

		printk("Waiting for PA sync\n");
		err = k_sem_take(&sem_pa_synced, SEM_TIMEOUT);
		if (err != 0) {
			printk("PA sync timed out, retrying\n");
			continue;
		}

		printk("PA synced, creating broadcast sink\n");
		err = bt_bap_broadcast_sink_create(pa_sync, broadcaster_broadcast_id,
						   &broadcast_sink);
		if (err != 0) {
			printk("Failed to create broadcast sink: %d\n", err);
			continue;
		}

		printk("Waiting for BASE\n");
		err = k_sem_take(&sem_base_received, SEM_TIMEOUT);
		if (err != 0) {
			printk("BASE not received (timeout), retrying\n");
			continue;
		}
		if (pa_sync == NULL) {
			printk("PA sync lost before BASE handling completed, retrying\n");
			continue;
		}

		printk("BASE received, waiting for syncable\n");
		err = k_sem_take(&sem_syncable, SEM_TIMEOUT);
		if (err != 0) {
			printk("Syncable timed out, retrying\n");
			continue;
		}
		if (pa_sync == NULL) {
			printk("PA sync lost before syncable handling completed, retrying\n");
			continue;
		}

		printk("Waiting for broadcast code\n");
		err = k_sem_take(&sem_broadcast_code_received, SEM_TIMEOUT);
		if (err != 0) {
			printk("Broadcast code timed out, retrying\n");
			continue;
		}
		if (pa_sync == NULL) {
			printk("PA sync lost before broadcast code handling completed, retrying\n");
			continue;
		}

		printk("Waiting for BIS sync request\n");
		err = k_sem_take(&sem_bis_sync_requested, SEM_TIMEOUT);
		if (err != 0) {
			printk("BIS sync request timed out, retrying\n");
			continue;
		}
		if (pa_sync == NULL) {
			printk("PA sync lost before BIS sync handling completed, retrying\n");
			continue;
		}

		sync_bitfield = select_bis_sync_bitfield();
		if (sync_bitfield == 0U) {
			printk("No valid BIS sync bitfield, retrying\n");
			continue;
		}

		stream_count = get_stream_count(sync_bitfield);
		printk("Syncing to BIG: bitfield=0x%08x, streams=%u\n",
		       sync_bitfield, stream_count);

		err = bt_bap_broadcast_sink_sync(broadcast_sink, sync_bitfield,
						 bap_streams_p, sink_broadcast_code);
		if (err != 0) {
			printk("Failed to sync to BIG: %d\n", err);
			return;
		}
		if (pa_sync == NULL) {
			printk("PA sync lost before BIG sync completed, retrying\n");
			continue;
		}

		printk("Waiting for BIG sync\n");
		for (int i = 0; i < 12; i++) {
			err = k_sem_take(&sem_big_synced, K_SECONDS(5));
			if (err == 0) {
				break;
			}
			if (pa_sync == NULL) {
				printk("PA sync lost while waiting for BIG sync, retrying\n");
				break;
			}
		}
		if (err != 0) {
			printk("BIG sync timed out, retrying\n");
			continue;
		}

		printk("BIG synced — receiving broadcast audio\n");
		printk("Waiting for PA sync loss to restart\n");
		k_sem_take(&sem_pa_sync_lost, K_FOREVER);

		printk("PA lost, waiting for sink to stop\n");
		k_sem_take(&sem_broadcast_sink_stopped, SEM_TIMEOUT);
	}
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SHARED BT_CONN_CB (mode-aware)
 * ═══════════════════════════════════════════════════════════════════════════ */


static void _connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int sec_err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0U) {
		printk("Connection failed: %s (err %u)\n", addr, err);
		return;
	}

	printk("Connected: %s\n", addr);
	default_conn = bt_conn_ref(conn);

	sec_err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (sec_err != 0) {
		printk("Failed to request security for %s (err %d)\n", addr, sec_err);
	}
}

static void _disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	if (conn != default_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);
	bt_conn_unref(default_conn);
	default_conn = NULL;

	/* Auto-restart unicast server advertising */
	if (unicast_adv != NULL) {
		err = bt_le_ext_adv_start(unicast_adv, BT_LE_EXT_ADV_START_DEFAULT);
		if (err != 0) {
			printk("Failed to restart adv (err %d)\n", err);
		} else {
			printk("Unicast server advertising restarted\n");
		}
	}
}

static void _security_changed(struct bt_conn *conn, bt_security_t level,
			      enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != BT_SECURITY_ERR_SUCCESS) {
		printk("Security failed: %s (err %d)\n", addr, err);
		return;
	}

	printk("Security level %d: %s\n", level, addr);
	k_sem_give(&sem_security_updated);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = _connected,
	.disconnected     = _disconnected,
	.security_changed = _security_changed,
};

static void passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Passkey display for %s: %06u\n", addr, passkey);
}

static void passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Passkey confirm for %s: %06u\n", addr, passkey);
	bt_conn_auth_passkey_confirm(conn);
}

static void pairing_confirm(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Pairing confirm for %s\n", addr);
	bt_conn_auth_pairing_confirm(conn);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Pairing cancelled for %s\n", addr);
}

static const struct bt_conn_auth_cb auth_cb = {
	.passkey_display = passkey_display,
	.passkey_confirm = passkey_confirm,
	.pairing_confirm = pairing_confirm,
	.cancel = auth_cancel,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * PACS HELPERS
 * ═══════════════════════════════════════════════════════════════════════════ */

static int set_location_and_contexts(void)
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

	if (IS_ENABLED(CONFIG_BT_PAC_SNK)) {
		err = bt_pacs_set_supported_contexts(BT_AUDIO_DIR_SINK, AVAILABLE_SINK_CONTEXT);
		if (err != 0) {
			printk("Failed to set sink supported contexts (err %d)\n", err);
			return err;
		}

		err = bt_pacs_set_available_contexts(BT_AUDIO_DIR_SINK, AVAILABLE_SINK_CONTEXT);
		if (err != 0) {
			printk("Failed to set sink available contexts (err %d)\n", err);
			return err;
		}
	}

	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RUN UNICAST SERVER
 * ═══════════════════════════════════════════════════════════════════════════ */

static int setup_unicast_server(void)
{
	const struct bt_pacs_register_param pacs_param = {
		.snk_pac = true,
		.snk_loc = true,
		.src_pac = true,
		.src_loc = true,
	};
	int err;

	err = bt_pacs_register(&pacs_param);
	if (err != 0) {
		printk("Failed to register PACS (err %d)\n", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_BT_AUDIO_TX)) {
		stream_tx_init();
	}

	bt_bap_unicast_server_register(&bap_server_param);
	bt_bap_unicast_server_register_cb(&unicast_server_cb);
	bt_pacs_cap_register(BT_AUDIO_DIR_SINK, &ucast_cap_sink);
	bt_pacs_cap_register(BT_AUDIO_DIR_SOURCE, &ucast_cap_source);

	for (size_t i = 0; i < ARRAY_SIZE(sink_streams); i++) {
		bt_bap_stream_cb_register(&sink_streams[i].stream, &ucast_stream_ops);
	}

	err = set_location_and_contexts();
	if (err != 0) {
		return err;
	}

	err = bt_le_ext_adv_create(BT_BAP_ADV_PARAM_CONN_QUICK, NULL, &unicast_adv);
	if (err != 0) {
		printk("Failed to create advertising set (err %d)\n", err);
		return err;
	}

	err = bt_le_ext_adv_set_data(unicast_adv, unicast_ad, ARRAY_SIZE(unicast_ad), NULL, 0);
	if (err != 0) {
		printk("Failed to set advertising data (err %d)\n", err);
		return err;
	}

	err = bt_le_ext_adv_start(unicast_adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err != 0) {
		printk("Failed to start advertising (err %d)\n", err);
		return err;
	}

	printk("BAP Unicast Server advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);
	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
	int err;

	printk("BAP Unicast Server + Broadcast Sink starting\n");

	err = bt_enable(NULL);
	if (err != 0) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	err = bt_conn_auth_cb_register(&auth_cb);
	if (err != 0) {
		printk("Failed to register auth callbacks (err %d)\n", err);
		return 0;
	}

	/* Start Unicast Server (callback-driven, non-blocking) */
	err = setup_unicast_server();
	if (err != 0) {
		printk("Unicast server setup failed (err %d)\n", err);
		return 0;
	}

	/* Add broadcast sink PAC capability (PACS already registered above) */
	err = bt_pacs_cap_register(BT_AUDIO_DIR_SINK, &bcast_cap_sink);
	if (err != 0) {
		printk("Failed to register bcast sink cap: %d\n", err);
		return 0;
	}

	/* Required for BASS receive-state setup used by broadcast sink internals */
	err = bt_bap_scan_delegator_register(&scan_delegator_cbs);
	if (err != 0 && err != -EALREADY) {
		printk("Failed to register scan delegator: %d\n", err);
		return 0;
	}

	/* Register Broadcast Sink callbacks and scan */
	bt_bap_broadcast_sink_register_cb(&broadcast_sink_cbs);
	bt_le_per_adv_sync_cb_register(&bap_pa_sync_cb);
	bt_le_scan_cb_register(&bcast_scan_cb);

	stream_rx_get_streams(bap_streams_p);

	for (size_t i = 0U; i < CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT; i++) {
		bt_bap_stream_cb_register(bap_streams_p[i], &bcast_stream_ops);
	}

	if (IS_ENABLED(CONFIG_LIBLC3)) {
		lc3_init();
	}

	if (IS_ENABLED(CONFIG_USE_USB_AUDIO_OUTPUT)) {
		usb_init();
	}

	printk("Unicast Server advertising + Broadcast Sink scanning simultaneously\n");

	/* Run Broadcast Sink state machine (loops forever in main thread) */
	run_broadcast_sink();

	return 0;
}
