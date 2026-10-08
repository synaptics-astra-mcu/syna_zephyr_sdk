/* Copyright (c) 2026 Synaptics Incorporated — SR110 stub: no LC3 decode on SR110 */
#ifndef SAMPLE_BAP_BROADCAST_SINK_LC3_H
#define SAMPLE_BAP_BROADCAST_SINK_LC3_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include "stream_rx.h"

/* LC3 stub prototypes — no actual decoding on SR110 (no liblc3, no USB) */
int lc3_enable(struct stream_rx *stream);
int lc3_disable(struct stream_rx *stream);
void lc3_enqueue_for_decoding(struct stream_rx *stream, const struct bt_iso_recv_info *info,
                               struct net_buf *buf);
int lc3_init(void);

#endif /* SAMPLE_BAP_BROADCAST_SINK_LC3_H */
