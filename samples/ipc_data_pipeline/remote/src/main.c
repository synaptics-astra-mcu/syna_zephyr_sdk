/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * @brief CLIENT-side mailbox receive, process, and reply task.
 *
 * @file main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "mbox_common.h"

LOG_MODULE_REGISTER(ipc_client, CONFIG_LOG_DEFAULT_LEVEL);

/* ============================================================================
 * Global Definitions
 * ============================================================================ */

/* Task stack size, priority, and copied I2C payload limit. */
#define PROCESS_TASK_STACK_SIZE 1024
/* Zephyr priority for CLIENT mailbox process task. */
#define PROCESS_TASK_PRIORITY   4
/* Maximum copied controller payload bytes from HOST mailbox frame. */
#define I2C_RCV_DATA_SIZE       21

/* RX packet carried from ISR callback into process task queue. */
struct mbox_rx_packet {
	struct mbox_message header;                    // Mailbox header copied from HOST frame
	uint8_t controller_rx_buf[I2C_RCV_DATA_SIZE];  // Appended controller payload bytes from HOST
	size_t controller_len;                         // Valid payload byte count in controller_rx_buf
};

/* Thread stack for the CLIENT process task. */
K_THREAD_STACK_DEFINE(process_task_stack, PROCESS_TASK_STACK_SIZE);

/* Thread control block for the process task instance. */
static struct k_thread process_task_data;

/* Message queue bridging mailbox RX callback to process task. */
K_MSGQ_DEFINE(mbox_to_process_msgq, sizeof(struct mbox_rx_packet), 4, 4);

/* Count dropped RX packets when process queue is full. */
static uint32_t g_process_msgq_drop_count = 0;
/* Count send failures when replying to HOST. */
static uint32_t g_process_send_fail_count = 0;

/* Mailbox channel descriptors initialized during startup. */
static struct mbox_dt_spec g_tx_channel;
/* RX mailbox channel used by HOST->CLIENT callback path. */
static struct mbox_dt_spec g_rx_channel;

/* ============================================================================
 * Function Prototypes
 * ============================================================================ */
static void mbox_rx_callback(const struct device *dev,
			     mbox_channel_id_t channel_id,
			     void *user_data,
			     struct mbox_msg *data); // ISR mailbox payload descriptor
static void process_task_entry(void *p1, void *p2, void *p3);

/* ============================================================================
 * Local Function Implementations
 * ============================================================================ */

/**
 * \function mbox_rx_callback
 *
 * \brief Mailbox receive callback - queues messages for processing.
 *
 * \details Copies received mailbox message data into queue for process task.
 *          Called by mailbox driver in ISR context when message arrives from HOST.
 *
 * \param dev Mailbox device pointer.
 * \param channel_id Channel identifier for received message.
 * \param user_data User-defined context pointer (unused).
 * \param data Mailbox message containing payload from HOST.
 * \return None (void).
 */
static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			     void *user_data, struct mbox_msg *data)
{
	struct mbox_rx_packet rx_packet = {0}; // Packet assembled from incoming mailbox data for queuing
	size_t payload_len;                    // Byte count of I2C data appended after the mbox_message header

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(struct mbox_message)) {
		return;
	}

	memcpy(&rx_packet.header, data->data, sizeof(struct mbox_message));
	payload_len = data->size - sizeof(struct mbox_message);
	if (payload_len > I2C_RCV_DATA_SIZE) {
		payload_len = I2C_RCV_DATA_SIZE;
	}
	memcpy(rx_packet.controller_rx_buf,
	       (const uint8_t *)data->data + sizeof(struct mbox_message),
	       payload_len);
	rx_packet.controller_len = payload_len;

	/* Queue message to Process task - non-blocking in ISR context */
	if (k_msgq_put(&mbox_to_process_msgq, &rx_packet, K_NO_WAIT) != 0)
	{   //  enqueue failed, queue full!, increase enqueue fail counter
		g_process_msgq_drop_count++;
		if ((g_process_msgq_drop_count == 1u) || ((g_process_msgq_drop_count % 10u) == 0u))
		{   // Log only on first and every 10th drop (aka fail)
			LOG_WRN("[CLIENT_MBOX_RX] Process queue full, dropped=%u",
				(unsigned int)g_process_msgq_drop_count);
		}
	}
} /* mbox_rx_callback */

/**
 * \function process_task_entry
 *
 * \brief Process task - Receives messages from HOST and sends responses.
 *
 * \details Infinite loop task that waits for messages from HOST via mailbox RX callback,
 *          increments counter, timestamps data, and sends processed message back to HOST.
 *
 * \param p1 Unused thread parameter.
 * \param p2 Unused thread parameter.
 * \param p3 Unused thread parameter.
 * \return None (void).
 */
static void process_task_entry(void *p1, void *p2, void *p3)
{
	struct mbox_rx_packet rx_packet;        // Packet dequeued from mbox_to_process_msgq
	struct mbox_msg mbox_msg = {0};         // Zephyr mailbox descriptor wrapping the reply header
	int ret;                                 // Return code from mbox_send_dt()
	uint32_t processed_count = 0;            // Number of packets processed by this task instance
	bool emit_log;                         // True when this iteration should emit info logs

	LOG_INF("[CLIENT_Process_Task] Started on CLIENT");

	while (1) {
		/* Wait for message from HOST */
		if (k_msgq_get(&mbox_to_process_msgq, &rx_packet, K_FOREVER) != 0) {
			continue;
		}

		processed_count++;
		emit_log = (processed_count == 1U) ||
			((processed_count % (uint32_t)CONFIG_IPC_CLIENT_LOG_EVERY_N) == 0U);

		if (emit_log) {
			LOG_INF("[CLIENT_Process_Task] Received from HOST (rx ch %d) counter: %d, time: " TS_FMT,
				g_rx_channel.channel_id, rx_packet.header.counter,
				TS_ARGS(rx_packet.header.timestamp));
			LOG_INF("[CLIENT_Process_Task] Source interface: %u", (unsigned int)rx_packet.header.source);
		}

		if (IS_ENABLED(CONFIG_IPC_CLIENT_LOG_HEXDUMP) && emit_log) {
			LOG_HEXDUMP_INF(rx_packet.controller_rx_buf, rx_packet.controller_len,
				"[CLIENT_Process_Task] controller_rx_buf");
		}

		/* Process the data (could do computation here) */
		rx_packet.header.counter += 1;
		rx_packet.header.timestamp = k_uptime_get();

		/* Send processed data back to HOST */
		mbox_msg.data = &rx_packet.header;
		mbox_msg.size = sizeof(rx_packet.header);

		if (emit_log) {
			LOG_INF("[CLIENT_Process_Task] Sending to HOST (tx ch %d) counter: %d, time: " TS_FMT,
				g_tx_channel.channel_id, rx_packet.header.counter,
				TS_ARGS(rx_packet.header.timestamp));
			LOG_INF("[CLIENT_Process_Task] Sending source interface: %u",
				(unsigned int)rx_packet.header.source);
		}

		ret = mbox_send_dt(&g_tx_channel, &mbox_msg);
		if (ret < 0) {
			g_process_send_fail_count++;
			if ((g_process_send_fail_count == 1u) || ((g_process_send_fail_count % 10u) == 0u)) {
				LOG_WRN("[CLIENT_Process_Task] mbox_send() failed, err=%d fails=%u",
					ret,
					(unsigned int)g_process_send_fail_count);
			}
			continue;
		}
	}
} /* process_task_entry */

/* ============================================================================
 * Exported Function Implementations
 * ============================================================================ */

/**
 * \function main
 *
 * \brief Application entry point for CLIENT mailbox client.
 *
 * \param None.
 *
 * \details Sets up mailbox channels for CLIENT-HOST communication, registers receive callback,
 *          creates process task to handle incoming messages, and suspends main thread.
 *
 * \return 0 on normal completion, 0 on early exit due to initialization error.
 */
int main(void)
{
	LOG_INF("Dualcore mbox data CLIENT CLIENT - %s", CONFIG_BOARD_TARGET);

	/* Initialize mbox channels */
	g_tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);
	g_rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	const int max_transfer_size_bytes = mbox_mtu_get_dt(&g_tx_channel); // TX mailbox MTU in bytes; must cover mbox_message
	if ((max_transfer_size_bytes < 0) ||
	    (max_transfer_size_bytes < (int)sizeof(struct mbox_message))) {
		LOG_ERR("mbox_mtu_get() error: MTU %d too small for message size %zu",
			max_transfer_size_bytes, sizeof(struct mbox_message));
		return -EINVAL;
	}

	if (mbox_register_callback_dt(&g_rx_channel, mbox_rx_callback, NULL)) {
		LOG_ERR("mbox_register_callback() error");
		return -EIO;
	}

	if (mbox_set_enabled_dt(&g_rx_channel, 1)) {
		LOG_ERR("mbox_set_enable() error");
		return -EIO;
	}

	/* Create CLIENT_Process_Task thread */
	k_thread_create(&process_task_data, process_task_stack,
			K_THREAD_STACK_SIZEOF(process_task_stack),
			process_task_entry, NULL, NULL, NULL,
			PROCESS_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&process_task_data, "CLIENT_Process_Task");

	LOG_INF("CLIENT thread started. Main thread sleeping.");

	/* Main thread can do other work or just sleep */
	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
} /* main */
