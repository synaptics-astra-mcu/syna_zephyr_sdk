/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/printk.h>
#include "lc3.h"

int lc3_init(void)
{
	printk("[lc3] stub: LC3 decoder not available on SR110\n");
	return 0;
}

int lc3_enable(struct stream_rx *stream)
{
	(void)stream;
	return 0;
}

int lc3_disable(struct stream_rx *stream)
{
	(void)stream;
	return 0;
}

void lc3_enqueue_for_decoding(struct stream_rx *stream, const struct bt_iso_recv_info *info,
			       struct net_buf *buf)
{
	(void)stream;
	(void)info;
	(void)buf;
}
