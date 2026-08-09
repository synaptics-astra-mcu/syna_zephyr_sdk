/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/classic.h>
#include <zephyr/bluetooth/classic/a2dp.h>
#include <zephyr/bluetooth/classic/a2dp_codec_sbc.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/sbc.h>
#include <zephyr/console/console.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(a2dp_source);

#define A2DP_VERSION     0x0103
#define MAX_SCAN_RESULTS 10
#define INQUIRY_LENGTH   8   /* 8 × 1.28 s ≈ 10 s */
#define MAX_SEPS         4

/* ── Tone parameters ─────────────────────────────────────────────────────── */
#define SEND_INTERVAL_MS  10          /* timer period, ms */
#define TONE_DURATION_MS  10000       /* stop after 10 s */
#define TX_BUF_COUNT      6
#define TX_BUF_SIZE       900         /* bytes of SBC payload per PDU */
#define MAX_FRAMES_PER_SEND 7         /* hard cap on frames per tick */

/* 441 Hz sine table: sin(2π·i/100)×32767, one full period = 100 entries */
static const int16_t sine_tbl[100] = {
	     0,  2057,  4107,  6142,  8149, 10124, 12066, 13953, 15790, 17566,
	 19268, 20887, 22432, 23889, 25249, 26511, 27670, 28718, 29652, 30469,
	 31167, 31741, 32190, 32511, 32705, 32767, 32705, 32511, 32190, 31741,
	 31167, 30469, 29652, 28718, 27670, 26511, 25249, 23889, 22432, 20887,
	 19268, 17566, 15790, 13953, 12066, 10124,  8149,  6142,  4107,  2057,
	     0, -2057, -4107, -6142, -8149,-10124,-12066,-13953,-15790,-17566,
	-19268,-20887,-22432,-23889,-25249,-26511,-27670,-28718,-29652,-30469,
	-31167,-31741,-32190,-32511,-32705,-32767,-32705,-32511,-32190,-31741,
	-31167,-30469,-29652,-28718,-27670,-26511,-25249,-23889,-22432,-20887,
	-19268,-17566,-15790,-13953,-12066,-10124, -8149, -6142, -4107, -2057,
};

/* ── SDP record ──────────────────────────────────────────────────────────── */
static struct bt_sdp_attribute a2dp_src_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST({
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_AUDIO_SOURCE_SVCLASS)
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
static struct bt_sdp_record a2dp_src_rec = BT_SDP_RECORD(a2dp_src_attrs);

/* ── A2DP endpoints ──────────────────────────────────────────────────────── */
BT_A2DP_SBC_SOURCE_EP_DEFAULT(sbc_src_ep);

static struct bt_a2dp_codec_ie sbc_sink_ep_cap;
static struct bt_a2dp_ep sbc_sink_ep = {
	.codec_cap = &sbc_sink_ep_cap,
};

static struct bt_a2dp_stream sbc_stream;
static bool peer_sbc_found;

BT_A2DP_SBC_EP_CFG_DEFAULT(sbc_cfg_default, A2DP_SBC_SAMP_FREQ_44100);

/* ── TX buffer pool ──────────────────────────────────────────────────────── */
NET_BUF_POOL_DEFINE(a2dp_tx_pool, TX_BUF_COUNT,
		    BT_L2CAP_BUF_SIZE(TX_BUF_SIZE),
		    CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

/* ── Audio sender state ──────────────────────────────────────────────────── */
static struct sbc_encoder sbc_enc;
static uint32_t  a2dp_src_sf;        /* samples/sec from config */
static uint8_t   a2dp_src_nc;        /* channel count from config */
static uint32_t  send_count;         /* RTP sequence number */
static uint32_t  send_ts;            /* RTP timestamp (samples) */
static int64_t   ref_time;           /* k_uptime of last work call */
static int64_t   stop_uptime;        /* k_uptime when we stop */
static uint32_t  missed_subsamples;  /* fractional-sample accumulator (×1000) */
static volatile bool a2dp_playing;
static uint32_t dbg_tick;

/* PCM double-buffer: max 8 frames × 128 samples × 2 ch × 2 bytes = 4096 B */
static int16_t pcm_buf[8 * 128 * 2];
static uint32_t sine_phase;          /* index into sine_tbl, wraps at 100 */

/* ── Workqueue for audio encoding ────────────────────────────────────────── */
static K_KERNEL_STACK_DEFINE(audio_stack, 2048);
static struct k_work_q audio_wq;
static void audio_work_fn(struct k_work *w);
static K_WORK_DEFINE(audio_work, audio_work_fn);
static void tone_timer_fn(struct k_timer *t);
K_TIMER_DEFINE(tone_timer, tone_timer_fn, NULL);

static void tone_timer_fn(struct k_timer *t)
{
	ARG_UNUSED(t);
	k_work_submit_to_queue(&audio_wq, &audio_work);
}

static void audio_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);

	if (!a2dp_playing) {
		return;
	}

	/* Auto-stop after TONE_DURATION_MS */
	if (k_uptime_get() >= stop_uptime) {
		LOG_INF("Tone complete — stopping stream");
		a2dp_playing = false;
		k_timer_stop(&tone_timer);
		bt_a2dp_stream_suspend(&sbc_stream);
		return;
	}

	int64_t elapsed_ms = k_uptime_delta(&ref_time);

	int pcm_frame_samples    = sbc_frame_samples(&sbc_enc);   /* 128 */
	int pcm_frame_bytes      = sbc_frame_bytes(&sbc_enc);     /* 512 */
	int encoded_frame_bytes  = sbc_frame_encoded_bytes(&sbc_enc);

	bool do_log = (dbg_tick++ % 10 == 0);

	if (do_log) {
		LOG_INF("audio[%u]: elapsed=%lld fps=%d fenc=%d mtu=%u",
			dbg_tick, elapsed_ms, pcm_frame_samples, encoded_frame_bytes,
			bt_a2dp_get_mtu(&sbc_stream));
	}

	/* How many PCM samples this interval covers (with fractional accumulator) */
	uint32_t total_samples = (uint32_t)((elapsed_ms * a2dp_src_sf) / 1000);

	missed_subsamples += (uint32_t)((elapsed_ms * a2dp_src_sf) % 1000);
	missed_subsamples += ((total_samples % pcm_frame_samples) * 1000);
	total_samples = (total_samples / pcm_frame_samples) * pcm_frame_samples;

	/* Drain sub-sample accumulator */
	while (missed_subsamples >= (1000u * pcm_frame_samples)) {
		total_samples        += pcm_frame_samples;
		missed_subsamples    -= (1000u * pcm_frame_samples);
	}

	uint8_t frame_num = (uint8_t)(total_samples / pcm_frame_samples);

	if (do_log) {
		LOG_INF("audio[%u]: total=%u frame_num=%u", dbg_tick, total_samples, frame_num);
	}

	if (frame_num == 0) {
		return;
	}
	if (frame_num > MAX_FRAMES_PER_SEND) {
		missed_subsamples += (frame_num - MAX_FRAMES_PER_SEND) * 1000u * pcm_frame_samples;
		frame_num = MAX_FRAMES_PER_SEND;
		total_samples = frame_num * pcm_frame_samples;
	}

	/* Clamp to what fits in PDU and net_buf */
	uint32_t pdu_payload = (uint32_t)frame_num * encoded_frame_bytes + 1u /* SBC hdr */;
	struct net_buf *buf = bt_a2dp_stream_create_pdu(&a2dp_tx_pool, K_FOREVER);

	while (pdu_payload > net_buf_tailroom(buf) ||
	       (buf->len + pdu_payload) > bt_a2dp_get_mtu(&sbc_stream)) {
		if (frame_num == 0) {
			break;
		}
		frame_num--;
		total_samples  -= pcm_frame_samples;
		pdu_payload    -= encoded_frame_bytes;
		missed_subsamples += 1000u * pcm_frame_samples;
	}

	if (frame_num == 0) {
		LOG_WRN("audio: frame_num clamped to 0 (mtu=%u pdu=%u)",
			bt_a2dp_get_mtu(&sbc_stream), pdu_payload);
		net_buf_unref(buf);
		return;
	}

	if (do_log) {
		LOG_INF("audio[%u]: sending %u frames pdu=%u", dbg_tick, frame_num, pdu_payload);
	}

	/* Generate stereo 16-bit PCM into pcm_buf */
	uint32_t num_samples = (uint32_t)frame_num * pcm_frame_samples;
	int16_t *p = pcm_buf;

	for (uint32_t i = 0; i < num_samples; i++) {
		int16_t v = sine_tbl[sine_phase];
		sine_phase = (sine_phase + 1 >= 100) ? 0 : sine_phase + 1;
		*p++ = v;   /* L */
		*p++ = v;   /* R */
	}

	/* Reserve SBC media payload header byte */
	uint8_t *sbc_hdr = net_buf_add(buf, 1u);

	/* Encode each frame */
	const uint8_t *pcm_ptr = (const uint8_t *)pcm_buf;

	for (uint8_t f = 0; f < frame_num; f++) {
		uint32_t out = sbc_encode(&sbc_enc, pcm_ptr, net_buf_tail(buf));

		if ((int)out != encoded_frame_bytes) {
			LOG_ERR("sbc_encode: got %u want %d", out, encoded_frame_bytes);
		}
		net_buf_add(buf, encoded_frame_bytes);
		pcm_ptr += pcm_frame_bytes;
	}

	*sbc_hdr = (uint8_t)BT_A2DP_SBC_MEDIA_HDR_ENCODE(frame_num, 0, 0, 0);

	int err = bt_a2dp_stream_send(&sbc_stream, buf, send_count, send_ts);

	if (err) {
		net_buf_unref(buf);
		LOG_WRN("stream_send err %d (state=%d)", err,
			sbc_stream.local_ep ? (int)sbc_stream.local_ep->sep.state : -1);
	} else if (do_log) {
		LOG_INF("audio[%u]: sent seq=%u ts=%u", dbg_tick, send_count, send_ts);
	}

	send_count++;
	send_ts += total_samples;
}

/* ── Stream ops ──────────────────────────────────────────────────────────── */
static void stream_configured(struct bt_a2dp_stream *stream)
{
	struct bt_a2dp_codec_sbc_params *sbc =
		(struct bt_a2dp_codec_sbc_params *)&sbc_cfg_default.codec_config->codec_ie[0];
	struct sbc_encoder_init_param p = {
		.bit_rate   = 229,
		.samp_freq  = bt_a2dp_sbc_get_sampling_frequency(sbc),
		.blk_len    = bt_a2dp_sbc_get_block_length(sbc),
		.subband    = bt_a2dp_sbc_get_subband_num(sbc),
		.alloc_mthd = bt_a2dp_sbc_get_allocation_method(sbc),
		.ch_mode    = bt_a2dp_sbc_get_channel_mode(sbc),
		.ch_num     = bt_a2dp_sbc_get_channel_num(sbc),
		.min_bitpool = sbc->min_bitpool,
		.max_bitpool = sbc->max_bitpool,
	};

	a2dp_src_sf = p.samp_freq;
	a2dp_src_nc = p.ch_num;

	if (sbc_setup_encoder(&sbc_enc, &p)) {
		LOG_ERR("sbc_setup_encoder failed");
		return;
	}

	LOG_INF("Stream configured (sf=%u nc=%u), opening...", a2dp_src_sf, a2dp_src_nc);
	int err = bt_a2dp_stream_establish(stream);

	if (err) {
		LOG_ERR("stream_establish failed (%d)", err);
	}
}

static void stream_established(struct bt_a2dp_stream *stream)
{
	LOG_INF("Stream established, starting...");
	int err = bt_a2dp_stream_start(stream);

	if (err) {
		LOG_ERR("stream_start failed (%d)", err);
	}
}

static void stream_started(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);

	LOG_INF("Stream STARTED — playing 441 Hz tone for %d s", TONE_DURATION_MS / 1000);

	send_count        = 0;
	send_ts           = 0;
	sine_phase        = 0;
	missed_subsamples = 0;
	dbg_tick          = 0;
	a2dp_playing      = true;
	stop_uptime       = k_uptime_get() + TONE_DURATION_MS;

	k_uptime_delta(&ref_time);
	k_timer_start(&tone_timer,
		      K_MSEC(SEND_INTERVAL_MS),
		      K_MSEC(SEND_INTERVAL_MS));
}

static void stream_suspended(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	LOG_INF("Stream suspended");
}

static void stream_released(struct bt_a2dp_stream *stream)
{
	ARG_UNUSED(stream);
	LOG_INF("Stream released");
	a2dp_playing = false;
	k_timer_stop(&tone_timer);
}

static struct bt_a2dp_stream_ops sbc_stream_ops = {
	.configured  = stream_configured,
	.established = stream_established,
	.started     = stream_started,
	.suspended   = stream_suspended,
	.released    = stream_released,
};

/* ── AVDTP endpoint discovery ────────────────────────────────────────────── */
static struct bt_avdtp_sep_info found_seps[MAX_SEPS];
static struct bt_a2dp_discover_param ep_discover_param;

static uint8_t discover_ep_cb(struct bt_a2dp *a2dp, struct bt_a2dp_ep_info *info,
			      struct bt_a2dp_ep **ep)
{
	if (peer_sbc_found) {
		int err;

		bt_a2dp_stream_cb_register(&sbc_stream, &sbc_stream_ops);
		LOG_INF("Configuring: local tsep=%d  remote tsep=%d",
			sbc_src_ep.sep.sep_info.tsep,
			sbc_sink_ep.sep.sep_info.tsep);
		err = bt_a2dp_stream_config(a2dp, &sbc_stream,
					    &sbc_src_ep, &sbc_sink_ep,
					    &sbc_cfg_default);
		if (err) {
			LOG_ERR("stream_config failed (%d)", err);
		}
		return BT_A2DP_DISCOVER_EP_STOP;
	}

	if (info != NULL) {
		LOG_INF("Discovered EP: codec=0x%02x tsep=%d",
			info->codec_type,
			info->sep_info ? (int)info->sep_info->tsep : -1);

		if (info->codec_type == BT_A2DP_SBC &&
		    ep != NULL &&
		    info->sep_info != NULL &&
		    info->sep_info->tsep == BT_AVDTP_SINK) {
			LOG_INF("Found remote SBC SINK EP — will configure");
			peer_sbc_found = true;
			*ep = &sbc_sink_ep;
		}
	} else {
		LOG_INF("Discover complete (peer_sbc_found=%d)", peer_sbc_found);
	}

	return BT_A2DP_DISCOVER_EP_CONTINUE;
}

/* ── Scan state ──────────────────────────────────────────────────────────── */
static struct bt_a2dp *current_a2dp;
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
	char addr[BT_ADDR_STR_LEN];

	if (!bt_conn_is_type(conn, BT_CONN_TYPE_BR)) {
		return;
	}

	if (err) {
		LOG_WRN("ACL connect failed (err %u)", err);
		k_sem_give(&conn_event_sem);
		return;
	}

	bt_addr_to_str(bt_conn_get_dst_br(conn), addr, sizeof(addr));
	LOG_INF("ACL connected: %s", addr);

	is_connected = true;
	bt_br_discovery_stop();
	k_sem_give(&scan_done_sem);
	k_sem_give(&conn_event_sem);

	LOG_INF("Opening A2DP signaling...");
	current_a2dp = bt_a2dp_connect(conn);
	if (!current_a2dp) {
		LOG_ERR("bt_a2dp_connect failed");
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

	a2dp_playing = false;
	k_timer_stop(&tone_timer);
	current_a2dp = NULL;
	is_connected = false;
	peer_sbc_found = false;
	k_sem_give(&conn_event_sem);
}

static struct bt_conn_cb conn_cb = {
	.connected    = on_connected,
	.disconnected = on_disconnected,
};

/* ── A2DP callbacks ──────────────────────────────────────────────────────── */
static void on_a2dp_connected(struct bt_a2dp *a2dp, int err)
{
	if (err) {
		LOG_WRN("A2DP connect failed (err %d)", err);
		return;
	}

	LOG_INF("A2DP signaling connected — discovering remote endpoints...");

	peer_sbc_found = false;
	ep_discover_param.cb            = discover_ep_cb;
	ep_discover_param.seps_info     = found_seps;
	ep_discover_param.sep_count     = MAX_SEPS;
	ep_discover_param.avdtp_version = 0;

	err = bt_a2dp_discover(a2dp, &ep_discover_param);
	if (err) {
		LOG_ERR("bt_a2dp_discover failed (%d)", err);
	}
}

static void on_a2dp_disconnected(struct bt_a2dp *a2dp)
{
	ARG_UNUSED(a2dp);
	LOG_INF("A2DP disconnected");
	current_a2dp = NULL;
}

static struct bt_a2dp_cb a2dp_cb = {
	.connected    = on_a2dp_connected,
	.disconnected = on_a2dp_disconnected,
};

/* ── Interactive select loop ─────────────────────────────────────────────── */
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

	printk("Starting A2DP Source - Synaptics SR110\n");

	console_init();

	/* Start audio workqueue */
	k_work_queue_start(&audio_wq, audio_stack, K_KERNEL_STACK_SIZEOF(audio_stack),
			   K_PRIO_COOP(7), NULL);
	k_thread_name_set(&audio_wq.thread, "a2dp_tx");

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable failed (%d)", err);
		return err;
	}

	bt_conn_cb_register(&conn_cb);
	bt_sdp_register_service(&a2dp_src_rec);

	err = bt_a2dp_register_ep(&sbc_src_ep, BT_AVDTP_AUDIO, BT_AVDTP_SOURCE);
	if (err) {
		LOG_ERR("A2DP source EP register failed (%d)", err);
	}

	err = bt_a2dp_register_cb(&a2dp_cb);
	if (err) {
		LOG_ERR("A2DP cb register failed (%d)", err);
	}

	bt_br_discovery_cb_register(&discovery_cb);
	bt_br_set_connectable(false, NULL);

	do_scan();

	while (1) {
		if (is_connected) {
			printk("Connected. Waiting for disconnect...\n");
			k_sem_take(&conn_event_sem, K_FOREVER);
			printk("Disconnected.\n");
			continue;
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
