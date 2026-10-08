/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/kernel.h>

#include "syna_hci_snoop.h"

/* Maximum HCI bytes per console line */
#define SNOOP_BYTES_PER_LINE   100

/* Maximum packet bytes stored per queue entry (truncates longer packets) */
#define SNOOP_MAX_PACKET       255

/* Depth of the snoop message queue (number of packets that can be buffered) */
#define SNOOP_QUEUE_DEPTH      32

/* Snoop thread stack size and priority */
#define SNOOP_THREAD_STACK     1024
#define SNOOP_THREAD_PRIO      10

/*
 * Line buffer:
 *   header: "SNOOP:TX[4294967295](99):TX:" ~32 chars
 *   data:   100 x "XX " = 300 chars
 *   NUL:    1
 */
#define SNOOP_LINE_BUF  336

/* Queue entry — small fixed size, enqueue is a fast memcpy */
struct snoop_entry {
	uint8_t  dir;                    /* 'T' = TX, 'R' = RX */
	uint32_t count;
	uint16_t len;                    /* actual bytes stored in data[] */
	uint8_t  data[SNOOP_MAX_PACKET];
};

K_MSGQ_DEFINE(snoop_queue, sizeof(struct snoop_entry), SNOOP_QUEUE_DEPTH, 4);

static K_THREAD_STACK_DEFINE(snoop_thread_stack, SNOOP_THREAD_STACK);
static struct k_thread snoop_thread_data;

static atomic_t snoop_tx_count;
static atomic_t snoop_rx_count;
static atomic_t snoop_paused;

static void snoop_print(const char *dir, uint32_t count,
			const uint8_t *data, uint32_t len)
{
	char line[SNOOP_LINE_BUF];
	uint32_t offset = 0;
	uint32_t chunk = 1;

	do {
		uint32_t chunk_len = len - offset;

		if (chunk_len > SNOOP_BYTES_PER_LINE) {
			chunk_len = SNOOP_BYTES_PER_LINE;
		}

		int pos = snprintf(line, sizeof(line),
				   "SNOOP:%s[%u](%02u):%s:",
				   dir, count, chunk, dir);

		for (uint32_t i = 0; i < chunk_len; i++) {
			if (pos >= (int)sizeof(line) - 4) {
				break;
			}
			if (i > 0) {
				line[pos++] = ' ';
			}
			pos += snprintf(line + pos, sizeof(line) - pos,
					"%02X", data[offset + i]);
		}

		printk("%s\n", line);

		offset += chunk_len;
		chunk++;
	} while (offset < len);
}

static void snoop_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct snoop_entry entry;

	while (1) {
		k_msgq_get(&snoop_queue, &entry, K_FOREVER);
		snoop_print(entry.dir == 'T' ? "TX" : "RX",
			    entry.count, entry.data, entry.len);
	}
}

static int snoop_init(void)
{
	k_thread_create(&snoop_thread_data, snoop_thread_stack,
			K_THREAD_STACK_SIZEOF(snoop_thread_stack),
			snoop_thread_fn, NULL, NULL, NULL,
			SNOOP_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&snoop_thread_data, "bt_snoop");
	return 0;
}

SYS_INIT(snoop_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

void hci_snoop_tx(const uint8_t *data, uint32_t len)
{
	if (atomic_get(&snoop_paused)) {
		return;
	}

	struct snoop_entry entry;

	entry.dir   = 'T';
	entry.count = (uint32_t)atomic_inc(&snoop_tx_count) + 1;
	entry.len   = (uint16_t)MIN(len, SNOOP_MAX_PACKET);
	memcpy(entry.data, data, entry.len);

	/* Non-blocking: drop silently if queue is full */
	k_msgq_put(&snoop_queue, &entry, K_NO_WAIT);
}

void hci_snoop_rx(const uint8_t *data, uint32_t len)
{
	if (atomic_get(&snoop_paused)) {
		return;
	}

	struct snoop_entry entry;

	entry.dir   = 'R';
	entry.count = (uint32_t)atomic_inc(&snoop_rx_count) + 1;
	entry.len   = (uint16_t)MIN(len, SNOOP_MAX_PACKET);
	memcpy(entry.data, data, entry.len);

	/* Non-blocking: drop silently if queue is full */
	k_msgq_put(&snoop_queue, &entry, K_NO_WAIT);
}

void hci_snoop_pause(void)
{
	atomic_set(&snoop_paused, 1);
}

void hci_snoop_resume(void)
{
	atomic_set(&snoop_paused, 0);
}
