/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MHD_TPUT_STATS_H__
#define __MHD_TPUT_STATS_H__

#include <stdint.h>

#ifdef MHD_TPUT_STATS

/* Mirror MAX_PKTID_TX from bcmmsgbuf.h to avoid heavy include chain */
#ifdef MHD_SCI_BUS
#define MHD_TPUT_STATS_MAX_PKTID  (256)
#else
#define MHD_TPUT_STATS_MAX_PKTID  (4 * 1024)
#endif

/**
 * Throughput statistics structure.
 * Collects host-side Tx/Rx counters, completion interrupt stats,
 * and Tx completion latency measurements.
 */
typedef struct mhd_tput_stats {
	/* --- Collection timestamps (ms) --- */
	uint32_t start_time_ms;           /* Time when stats collection started */
	uint32_t last_query_time_ms;      /* Time of last stats query */
	uint32_t tx_first_pkt_time_ms;    /* Time of first Tx packet (0 = no Tx traffic) */
	uint32_t tx_last_pkt_time_ms;     /* Time of most recent Tx packet */
	uint32_t rx_first_pkt_time_ms;    /* Time of first Rx packet (0 = no Rx traffic) */
	uint32_t rx_last_pkt_time_ms;     /* Time of most recent Rx packet */

	/* --- Basic Tx counters --- */
	uint64_t tx_bytes;                /* Total Tx data bytes submitted to driver */
	uint32_t tx_packets;              /* Total Tx data packets submitted to driver */
	uint32_t tx_total_packets;        /* Total Tx pkts entering send_ethernet_data */
	uint32_t tx_dropped;              /* Tx pkts dropped (flowid fail, queue overflow) */
	uint32_t tx_queue_overflow;       /* Tx drops: flow ring resource exhaustion */

	/* --- Basic Rx counters --- */
	uint64_t rx_bytes;                /* Total Rx data bytes delivered to network stack */
	uint32_t rx_packets;              /* Total Rx data packets delivered to network stack */

	/* --- Tx completion interrupt stats (msgs processed per interrupt) --- */
	uint32_t txcpl_interrupts;        /* Number of txcpl ring processing calls */
	uint32_t txcpl_total_freed;       /* Total packets freed across all txcpl interrupts */
	uint32_t txcpl_min_per_int;       /* Min txcpl messages processed in a single interrupt */
	uint32_t txcpl_max_per_int;       /* Max txcpl messages processed in a single interrupt */

	/* --- Rx completion interrupt stats (msgs processed per interrupt) --- */
	uint32_t rxcpl_interrupts;        /* Number of rxcpl ring processing calls */
	uint32_t rxcpl_total_pkts;        /* Total packets received across all rxcpl interrupts */
	uint32_t rxcpl_min_per_int;       /* Min rxcpl messages processed in a single interrupt */
	uint32_t rxcpl_max_per_int;       /* Max rxcpl messages processed in a single interrupt */

	/* --- Flow queue stats --- */
	uint32_t flowq_enqueue;            /* Total packets enqueued to flow queue */
	uint32_t flowq_dequeue;            /* Total packets dequeued from flow queue */
	uint32_t flowq_max_depth;          /* Max flow queue depth observed */
	uint32_t flowq_cur_depth;          /* Current flow queue depth at last update */
	uint32_t flowq_throttle;           /* Times flow queue throttle triggered (queue full) */
	uint32_t flowq_max_size;           /* Configured max size (capacity) of flow queue */

	/* --- mhd_thread_notify stats (Tx direction) --- */
	uint32_t tx_thread_notify;         /* Times mhd_thread_notify called for Tx scheduling */

	/* --- Enqueue-to-txcpl latency (ms) --- */
	uint64_t tx_latency_sum_ms;       /* Sum of all enqueue-to-txcpl latencies in ms */
	uint32_t tx_latency_count;        /* Number of Tx latency samples */
	uint32_t tx_latency_min_ms;       /* Minimum enqueue-to-txcpl latency in ms */
	uint32_t tx_latency_max_ms;       /* Maximum enqueue-to-txcpl latency in ms */

	/* --- Per-pktid enqueue timestamp (ms) for latency computation --- */
	uint32_t tx_enqueue_ts_ms[MHD_TPUT_STATS_MAX_PKTID + 1];
} mhd_tput_stats_t;

/* Initialize / reset all stats to zero and record start time */
void mhd_tput_stats_init(void);

/* Get a snapshot of current stats (caller provides buffer) */
void mhd_tput_stats_get(mhd_tput_stats_t *out);

/* Print formatted stats to console */
void mhd_tput_stats_dump(void);

/* --- Inline instrumentation hooks called from data path --- */

/* Called from mhd_network_send_ethernet_data() on every Tx packet entry (counts total only) */
void mhd_tput_stats_tx_enqueue(void);

/* Called when a Tx packet is dropped (flowid fail, no resource, etc.) */
void mhd_tput_stats_tx_drop(void);

/* Called when Tx packet is dropped specifically due to flow ring resource exhaustion */
void mhd_tput_stats_tx_queue_overflow(void);

/*
 * Called from mhd_prot_txdata() after pktid is assigned;
 * counts tx_packets/tx_bytes and stamps timestamp.
 */
void mhd_tput_stats_tx_pktid_enqueue(uint32_t pktid, uint32_t pkt_len);

/* Called from mhd_prot_txstatus_process() for each freed Tx packet with its pktid */
void mhd_tput_stats_txcpl_packet(uint32_t pktid);

/* Called once per mhd_prot_process_msgbuf_txcpl() invocation with count of freed pkts */
void mhd_tput_stats_txcpl_done(uint32_t freed_count);

/* Called from mhd_prot_process_msgbuf_rxcpl() for each received Rx packet */
void mhd_tput_stats_rx_packet(uint32_t pkt_len);

/* Called once per mhd_prot_process_msgbuf_rxcpl() invocation with count of rx pkts */
void mhd_tput_stats_rxcpl_done(uint32_t pkt_count);

/* Called after a packet is successfully enqueued to flow queue; queue_len/queue_max from queue */
void mhd_tput_stats_flowq_enqueue(uint32_t queue_len, uint32_t queue_max);

/* Called after a packet is dequeued from flow queue; queue_len is queue->len after dequeue */
void mhd_tput_stats_flowq_dequeue(uint32_t queue_len);

/* Called from mhd_bus_schedule_queue() when thread_notify is called for Tx */
void mhd_tput_stats_tx_thread_notify(void);

/* Called when flow queue throttle triggers (queue full) */
void mhd_tput_stats_flowq_throttle(void);

#else /* !MHD_TPUT_STATS */

/* No-op stubs when feature is disabled */
#define mhd_tput_stats_init()
#define mhd_tput_stats_get(out)
#define mhd_tput_stats_dump()
#define mhd_tput_stats_tx_enqueue()
#define mhd_tput_stats_tx_drop()
#define mhd_tput_stats_tx_queue_overflow()
#define mhd_tput_stats_tx_pktid_enqueue(pktid, pkt_len)
#define mhd_tput_stats_txcpl_packet(pktid)
#define mhd_tput_stats_txcpl_done(freed_count)
#define mhd_tput_stats_rx_packet(pkt_len)
#define mhd_tput_stats_rxcpl_done(pkt_count)
#define mhd_tput_stats_flowq_enqueue(queue_len, queue_max)
#define mhd_tput_stats_flowq_dequeue(queue_len)
#define mhd_tput_stats_tx_thread_notify()
#define mhd_tput_stats_flowq_throttle()

#endif /* MHD_TPUT_STATS */

#endif /* __MHD_TPUT_STATS_H__ */
