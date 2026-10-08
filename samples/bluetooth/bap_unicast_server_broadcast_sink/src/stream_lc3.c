/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/net_buf.h>

struct tx_stream;

int stream_lc3_init(struct tx_stream *stream)
{
	(void)stream;
	return -ENOTSUP;
}

void stream_lc3_add_data(struct tx_stream *stream, struct net_buf *buf)
{
	(void)stream;
	(void)buf;
}
