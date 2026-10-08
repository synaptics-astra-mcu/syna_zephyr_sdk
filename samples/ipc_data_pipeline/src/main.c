/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * @brief HOST-side entry and task orchestration for the mailbox pipeline.
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
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/class/hid.h>
#include <usb_hid_stack_service.h>
#include <usb_cdc_stack_service.h>
#include "mbox_common.h"
#include "input_device.h"

LOG_MODULE_REGISTER(ipc_host, CONFIG_LOG_DEFAULT_LEVEL);

/* ============================================================================
 * Global Definitions
 * ============================================================================ */

/* Task stack sizes and priorities. */
#define INPUTDEV_TASK_STACK_SIZE  1024
/* Stack size for HID processing task. */
#define HID_TASK_STACK_SIZE    1024
/* Zephyr priority for per-device input polling/forwarding tasks. */
#define INPUTDEV_TASK_PRIORITY    5
/* Zephyr priority for HID forwarding task (higher than input tasks). */
#define HID_TASK_PRIORITY      4
/* Consecutive hard I2C failures required before entering recovery mode. */
#define INPUTDEV_HARD_ERR_RECOVERY_THRESHOLD 5u

/* Bring-up visibility: print every HID packet on HOST UART. */
#define HID_PRINT_EVERY_N      5u
/* Temporary bring-up toggle: route HID events to mouse report path. */
#define HID_MOUSE_ENABLED_TEST 1u
/* Number of mailbox events used for mouse-only bring-up demo. */
#define HID_MOUSE_TEST_MAX_REPORTS 10u
/* Delay between successful mouse demo reports in milliseconds. */
#define HID_MOUSE_TEST_DELAY_MS 125u

/* Sample-selected keyboard key sent on each mailbox-triggered HID event. */
#define SAMPLE_HID_KEYCODE_SRC1 HID_KEY_A
/* Source-2 selected keyboard key for mailbox-triggered HID events. */
#define SAMPLE_HID_KEYCODE_SRC2 HID_KEY_B
/* Retry budget for transient HID host-not-ready windows. */
#define SAMPLE_HID_SEND_RETRY_COUNT 5U
/* Delay between HID send retries when host endpoint is temporarily busy. */
#define SAMPLE_HID_SEND_RETRY_DELAY_MS 2U
/* Hold key briefly before release to keep key transition explicit on host. */
#define SAMPLE_HID_KEY_HOLD_MS 2U

/* DEBUG NOTES:
 * The commented LOG_* lines in this file are intentionally kept for troubleshooting.
 * Uncomment them temporarily when diagnosing mailbox/HID flow issues.
 */

/* Thread stacks: one per input device task, and one for HID task. */
K_THREAD_STACK_ARRAY_DEFINE(inputdev_task_stacks, INPUT_DEVICE_COUNT, INPUTDEV_TASK_STACK_SIZE);
K_THREAD_STACK_DEFINE(hid_task_stack, HID_TASK_STACK_SIZE);

/* Thread control blocks for created task instances. */
static struct k_thread inputdev_task_data[INPUT_DEVICE_COUNT];
/* Thread control block for HID task instance. */
static struct k_thread hid_task_data;

/* Queue depth for ISR mailbox RX bursts before HID task drains messages. */
#define MBOX_TO_HID_MSGQ_DEPTH 128

/* Message queue connecting mailbox RX callback to HID task context. */
K_MSGQ_DEFINE(mbox_to_hid_msgq, sizeof(struct mbox_message), MBOX_TO_HID_MSGQ_DEPTH, 4);

/* Mailbox channel descriptors initialized during startup. */
static struct mbox_dt_spec g_tx_channel;
/* RX mailbox channel used by CLIENT->HOST callback path. */
static struct mbox_dt_spec g_rx_channel;

/* Cached TX mailbox MTU used by input processing tasks. */
static size_t g_tx_mtu = sizeof(struct mbox_message);

/* ============================================================================
 * Function Prototypes
 * ============================================================================ */
static void mbox_rx_callback(const struct device *dev,
			     mbox_channel_id_t channel_id,
			     void *user_data,
			     struct mbox_msg *data); // ISR mailbox payload descriptor
static void inputdev_task_entry(void *p1, void *p2, void *p3);
static void hid_task_entry(void *p1, void *p2, void *p3);
static bool inputdev_is_hard_i2c_error(int ret);
static int hid_send_keyboard_key_retry(uint8_t keycode);
static uint8_t hid_keycode_for_source(source_t source);
static int hid_send_mouse_retry(uint32_t counter);
static void hid_send_reports(source_t source, uint32_t counter);
static int init_usb_services(void);

/* ============================================================================
 * Local Function Implementations
 * ============================================================================ */

 /**
 * \function inputdev_is_hard_i2c_error
 *
 * \brief Classify I2C failures that require stopping reads until recovery.
 *
 * \param ret Return code from I2C read path.
 *
 * \return true when reads must pause and recovery mode should run.
 */
static bool inputdev_is_hard_i2c_error(int ret)
{
	return (ret == -EIO) || (ret == -ENXIO);
} /* inputdev_is_hard_i2c_error */

/**
 * \function mbox_rx_callback
 *
 * \brief Mailbox receive callback - forwards messages to HID task.
 *
 * \details Copies received mailbox message data into queue for HID task processing.
 *          Called by mailbox driver in ISR context when message arrives from CLIENT.
 *
 * \param dev Mailbox device pointer.
 * \param channel_id Channel identifier for received message.
 * \param user_data User-defined context pointer (unused).
 * \param data Mailbox message containing payload from CLIENT.
 * \return None (void).
 */
static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			     void *user_data, struct mbox_msg *data)
{
	struct mbox_message rx_msg;             // Decoded payload copied from the ISR data buffer

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	#if DEBUG_RUNTIME_LOGS
		/* Number of short/invalid mailbox packets discarded in callback. */
		static uint32_t g_hid_short_packet_count;
		/* Validate received data size */
		if (data->size < sizeof(rx_msg)) {
			g_hid_short_packet_count++;
			if ((g_hid_short_packet_count == 1u) ||
				((g_hid_short_packet_count % 10u) == 0u)) {
					// Debug-only log kept commented; see DEBUG NOTES above.
					LOG_WRN("[HOST_MBOX_RX] short packet: size=%u need=%u dropped=%u",
							(unsigned int)data->size,
							(unsigned int)sizeof(rx_msg),
							(unsigned int)g_hid_short_packet_count);
			}
			return;
		}
	#endif

	memcpy(&rx_msg, data->data, sizeof(rx_msg));

	/* Queue message to HID_Task - non-blocking in ISR context */
	if (k_msgq_put(&mbox_to_hid_msgq, &rx_msg, K_NO_WAIT) != 0) {
		#if DEBUG_RUNTIME_LOGS
			/* Queue diagnostic counters for ISR->HID handoff path. */
			static uint32_t g_hid_msgq_drop_count;
			g_hid_msgq_drop_count++;
			if ((g_hid_msgq_drop_count == 1u) || ((g_hid_msgq_drop_count % 10u) == 0u)) {
					// Debug-only log kept commented; see DEBUG NOTES above.
					LOG_WRN("[HOST_MBOX_RX] HID queue full, dropped=%u",
							(unsigned int)g_hid_msgq_drop_count);
			}
		#endif
	} else {
		#if DEBUG_RUNTIME_LOGS			
			/* Number of mailbox messages successfully enqueued to HID task. */
			static uint32_t g_hid_msgq_put_count;
			g_hid_msgq_put_count++;
			if ((g_hid_msgq_put_count == 1u) || ((g_hid_msgq_put_count % 10u) == 0u)) {
					// Debug-only log kept commented; see DEBUG NOTES above.
					LOG_INF("[HOST_MBOX_RX] queued source=%u counter=%u puts=%u",
							(unsigned int)rx_msg.source,
							(unsigned int)rx_msg.counter,
							(unsigned int)g_hid_msgq_put_count);
			}
		#endif
	}
} /* mbox_rx_callback */

/**
 * \function inputdev_task_entry
 *
 * \brief Input-device task - Generic task for handling an input device.
 *
 * \details Waits for input device data request events from the queue, performs I2C reads from
 *          the device on data ready events, and sends data to CLIENT via TX mailbox.
 *          This is a generic implementation that works with any input device instance.
 *
 * \param p1 Pointer to the input device instance (struct input_device *).
 * \param p2 Unused thread parameter.
 * \param p3 Unused thread parameter.
 * \return None (void).
 */
static void inputdev_task_entry(void *p1, void *p2, void *p3)
{
	struct input_device *dev = (struct input_device *)p1; // Input device instance for this task
	struct inputdev_event_msg event;                      // Event dequeued from the per-device event queue
	int ret;                                              // Return code from event wait and read calls
	uint32_t hard_i2c_err_streak = 0u;                    // Consecutive hard I2C failures while GPIO is asserted

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("[%s_Task] Started on HOST", dev->config->name);

	/* Purge any stale events from initialization */
	inputdev_purge_events(dev);

	while (1) {
		/* Strict ISR-driven flow: never perform I2C without a queued GPIO event. */
		ret = inputdev_wait_event(dev, &event, K_FOREVER);

		if (ret != 0) {
			continue;
		}

		if (event.type == INPUTDEV_EVENT_DATA_READY) {
			/* Keep requesting while the original data-ready GPIO remains asserted. */
			while (inputdev_get_status(dev) == 0U) {
				ret = inputdev_handle_data_request(dev, &g_tx_channel, g_tx_mtu);
				if (ret != 0) {
					if (inputdev_is_hard_i2c_error(ret)) {
						hard_i2c_err_streak++;
						if (hard_i2c_err_streak < INPUTDEV_HARD_ERR_RECOVERY_THRESHOLD) {
							k_msleep(1);
							continue;
						}

						/*
						 * Enter recovery mode: stop issuing I2C transactions while data-ready
						 * remains asserted (GPIO low), then re-run startup probe once line
						 * returns high.
						 */
						LOG_WRN("[%s_Task] I2C hard error streak=%u (err=%d), pausing until GPIO high",
							dev->config->name,
							(unsigned int)hard_i2c_err_streak,
							ret);

						while (inputdev_get_status(dev) == 0U) {
							k_msleep(5);
						}

						inputdev_purge_events(dev);
						inputdev_reset_sequence(dev);
						inputdev_probe_transfer_path(dev);
						hard_i2c_err_streak = 0u;
						LOG_INF("[%s_Task] Recovery complete, waiting for next DATA_READY edge",
							dev->config->name);
						break;
					}

					hard_i2c_err_streak = 0u;

					/*
					 * A transient I2C failure must not terminate the while-low drain,
					 * otherwise no new falling edge arrives and reads stop permanently.
					 */
					k_msleep(1);
					continue;
				}

				hard_i2c_err_streak = 0u;
				k_yield();
			}
		}
	}
} /* inputdev_task_entry */

/**
 * \function hid_log_send_error
 *
 * \brief Log HID send failures that are not transient host-not-ready cases.
 *
 * \param report_name Short profile name used in the warning message.
 * \param ret Return code from the HID send helper.
 * \return None (void).
 */
static void hid_log_send_error(const char *report_name, int ret)
{
	if (ret != 0 && ret != -EAGAIN) {
		LOG_WRN("[HID_Task] %s report failed, %d", report_name, ret);
	}
} /* hid_log_send_error */

/**
 * \function hid_send_keyboard_key_retry
 *
 * \brief Send one keyboard key report with retries for transient -EAGAIN.
 *
 * \param keycode Keyboard usage ID (0 means key-release report).
 * \return 0 on success, otherwise last negative errno from send helper.
 */
static int hid_send_keyboard_key_retry(uint8_t keycode)
{
	int ret;           // Return code from usb_hid_stack_send_keyboard_key()
	uint32_t attempt;  // Retry loop index for transient -EAGAIN handling

	for (attempt = 0U; attempt < SAMPLE_HID_SEND_RETRY_COUNT; attempt++) {
		ret = usb_hid_stack_send_keyboard_key(keycode);
		if (ret == 0) {
			return 0;
		}

		if (ret != -EAGAIN) {
			return ret;
		}

		k_sleep(K_MSEC(SAMPLE_HID_SEND_RETRY_DELAY_MS));
	}

	return ret;
} /* hid_send_keyboard_key_retry */

/**
 * \function hid_keycode_for_source
 *
 * \brief Resolve keyboard keycode from mailbox source interface.
 *
 * \param source Source interface value carried in mailbox message.
 * \return HID keycode for supported sources; 0 when source is unsupported.
 */
static uint8_t hid_keycode_for_source(source_t source)
{
	if (source == INPUT_DEVICE_1) {
		return SAMPLE_HID_KEYCODE_SRC1;
	}

	if (source == INPUT_DEVICE_2) {
		return SAMPLE_HID_KEYCODE_SRC2;
	}

	return 0U;
} /* hid_keycode_for_source */

/**
 * \function hid_send_mouse_retry
 *
 * \brief Send one mouse report with retries for transient -EAGAIN.
 *
 * \param counter Mailbox counter propagated to the HID mouse helper.
 * \return 0 on success, otherwise last negative errno from send helper.
 */
static int hid_send_mouse_retry(uint32_t counter)
{
	int ret = -EAGAIN;                           // Safe default if retry count is 0
	const uint32_t max_attempts = SAMPLE_HID_SEND_RETRY_COUNT;

	/* Explicitly handle disabled retry budget. */
	if (max_attempts == 0U) {
		return -EAGAIN;
	}

	for (uint32_t attempt = 0U; attempt < max_attempts; ++attempt) {
		ret = usb_hid_stack_send_mouse(counter);

		/* Return on success, hard error, or after the final retry attempt. */
		if (ret != -EAGAIN || (attempt + 1U) == max_attempts) {
			return ret;
		}

		/* Only transient -EAGAIN reaches here: wait briefly before retrying. */
		k_sleep(K_MSEC(SAMPLE_HID_SEND_RETRY_DELAY_MS));
	}

	/* Defensive fallback; loop normally exits via return above. */
	return ret;
} /* hid_send_mouse_retry */

/**
 * \function hid_send_reports
 *
 * \brief Submit the sample-selected HID report for a mailbox event.
 *
 * \details The sample currently emits a keyboard report only, with the keycode
 *          selected locally in this file.
 *
 * \param source Mailbox source interface used for keyboard A/B key selection.
 * \param counter Mailbox counter value associated with the HID event.
 * \return None (void).
 */
static void hid_send_reports(source_t source, uint32_t counter)
{
	int ret; // Return code from HID helper send calls

#if HID_MOUSE_ENABLED_TEST
	{
		static uint32_t g_hid_mouse_send_count; // Count of successful mouse test reports sent so far
		static bool g_hid_mouse_test_done;      // One-shot latch to avoid repeating completion log

		if (g_hid_mouse_send_count < HID_MOUSE_TEST_MAX_REPORTS) {
			ret = hid_send_mouse_retry(counter);
			hid_log_send_error("Mouse", ret);

			if (ret == 0) {
				g_hid_mouse_send_count++;
				LOG_INF("[HID_Task] Mouse test send %u/%u (source=%u counter=%u)",
					(unsigned int)g_hid_mouse_send_count,
					(unsigned int)HID_MOUSE_TEST_MAX_REPORTS,
					(unsigned int)source,
					(unsigned int)counter);

				if (g_hid_mouse_send_count == HID_MOUSE_TEST_MAX_REPORTS) {
					g_hid_mouse_test_done = true;
					LOG_INF("[HID_Task] Mouse test complete (%u reports), resuming keyboard A/B",
						(unsigned int)g_hid_mouse_send_count);
				}

				k_msleep(HID_MOUSE_TEST_DELAY_MS);
			}

			return;
		}

		if (!g_hid_mouse_test_done) {
			g_hid_mouse_test_done = true;
			LOG_INF("[HID_Task] Mouse test complete (%u reports), resuming keyboard A/B",
				(unsigned int)g_hid_mouse_send_count);
		}
	}
#else
	ARG_UNUSED(counter);
#endif

	{
	uint8_t keycode;                      // Selected keyboard keycode mapped from source interface

	keycode = hid_keycode_for_source(source);
	if (keycode != 0U) {
		ret = hid_send_keyboard_key_retry(keycode);
		hid_log_send_error("Keyboard", ret);

		if (ret == 0) {
			k_sleep(K_MSEC(SAMPLE_HID_KEY_HOLD_MS));

			ret = hid_send_keyboard_key_retry(0U);
			hid_log_send_error("Keyboard release", ret);

			#if DEBUG_RUNTIME_LOGS
				static uint32_t g_hid_key_send_count; // Count of successful keyboard press/release cycles
				g_hid_key_send_count++;
				if ((g_hid_key_send_count == 1U) || ((g_hid_key_send_count % 10U) == 0U)) {
						// Debug-only log kept commented; see DEBUG NOTES above.
						LOG_INF("[HID_Task] keyboard press source=%u keycode=0x%02x count=%u",
							(unsigned int)source,
							(unsigned int)keycode,
							(unsigned int)g_hid_key_send_count);
				}
			#endif
			}
		}
	}
} /* hid_send_reports */

/**
 * \function hid_task_entry
 *
 * \brief HID task - Receives and processes messages from CLIENT.
 *
 * \details Infinite loop task that waits for messages from mailbox RX callback
 *          queue and logs received data. In production, would forward to USB HID stack.
 *
 * \param p1 Unused thread parameter.
 * \param p2 Unused thread parameter.
 * \param p3 Unused thread parameter.
 * \return None (void).
 */
static void hid_task_entry(void *p1, void *p2, void *p3)
{
	struct mbox_message rx_msg;             // Message received from CLIENT via mbox_to_hid_msgq

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("[HID_Task] Started on HOST");

	while (1) {
		/* Wait for message from mbox callback */
		if (k_msgq_get(&mbox_to_hid_msgq, &rx_msg, K_FOREVER) != 0) {
			continue;
		}

		#if DEBUG_RUNTIME_LOGS
			/* Number of mailbox messages dequeued by HID task. */
			static uint32_t g_hid_msgq_get_count;

			g_hid_msgq_get_count++;
			if ((g_hid_msgq_get_count == 1u) || ((g_hid_msgq_get_count % 10u) == 0u)) {
					// Debug-only log kept commented; see DEBUG NOTES above.
					LOG_INF("[HID_Task] dequeued source=%u counter=%u gets=%u",
							(unsigned int)rx_msg.source,
							(unsigned int)rx_msg.counter,
							(unsigned int)g_hid_msgq_get_count);
			}
		#endif

		hid_send_reports(rx_msg.source, rx_msg.counter);
			
		if ((rx_msg.counter % HID_PRINT_EVERY_N) == 0u) {
			LOG_INF("[HID_Task] RX ch %d source %u counter %d time: " TS_FMT,
				g_rx_channel.channel_id,
				(unsigned int)rx_msg.source,
				rx_msg.counter,
				TS_ARGS(rx_msg.timestamp));
		}
	}
} /* hid_task_entry */

/**
 * \function init_usb_services
 *
 * \brief Initialize the stack-owned USB HID and CDC services.
 *
 * \details Brings up the HID stack service first, then the CDC stack service,
 *          and stops on the first initialization failure.
 *
 * \return 0 on success, negative errno on initialization failure.
 */
static int init_usb_services(void)
{
	int ret; // Return code from stack-owned USB service initializers

	ret = usb_hid_stack_service_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize HID stack service, %d", ret);
		return ret;
	}

	ret = usb_cdc_stack_service_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize CDC stack service, %d", ret);
		return ret;
	}

	return 0;
} /* init_usb_services */

/* ============================================================================
 * Exported Function Implementations
 * ============================================================================ */

/**
 * \function main
 *
 * \brief Application entry point - initializes dualcore mailbox pipeline.
 *
 * \details Sets up mailbox channels for HOST-CLIENT communication, initializes input
 *          devices with reset sequence, creates input device and HID tasks, and waits
 *          for task completion.
 *
 * \return 0 on normal completion, 0 on early exit due to initialization error.
 */
int main(void)
{
	struct input_device *indev; // Pointer to each input device instance during thread creation loop
	char thread_name[32];       // Buffer for constructing per-device thread name strings
	int i;                      // Loop index for iterating over all input devices
	int ret;                    // Return code from initialization and setup calls

	LOG_INF("Dualcore mbox data HOST SERVER - %s", CONFIG_BOARD_TARGET);

	/* Initialize mbox channels */
	g_tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);
	g_rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	const int max_transfer_size_bytes = mbox_mtu_get_dt(&g_tx_channel); // TX mailbox MTU in bytes; must cover mbox_message
	if ((max_transfer_size_bytes <= 0) ||
	    (max_transfer_size_bytes < (int)sizeof(struct mbox_message))) {
		LOG_ERR("mbox_mtu_get() error: MTU %d too small for message size %zu",
			max_transfer_size_bytes, sizeof(struct mbox_message));
		return -EINVAL;
	}
	g_tx_mtu = (size_t)max_transfer_size_bytes;

	if (mbox_register_callback_dt(&g_rx_channel, mbox_rx_callback, NULL)) {
		LOG_ERR("mbox_register_callback() error");
		return -EIO;
	}

	if (mbox_set_enabled_dt(&g_rx_channel, 1)) {
		LOG_ERR("mbox_set_enable() error");
		return -EIO;
	}

	/* Initialize all input devices using the abstraction layer */
	if (inputdev_init_all() != 0) {
		LOG_ERR("Input device initialization failed");
		return -EIO;
	}

	/* Execute GPIO reset sequence on all input devices */
	inputdev_reset_all();

	ret = init_usb_services();
	if (ret != 0) {
		return ret;
	}

	/* Create input device task threads */
	for (i = 0; i < INPUT_DEVICE_COUNT; i++) {
		indev = inputdev_get_device((inputdev_id_t)i);
		if (indev == NULL) {
			LOG_ERR("Failed to get input device %d", i);
			continue;
		}

		k_thread_create(&inputdev_task_data[i], inputdev_task_stacks[i],
				K_THREAD_STACK_SIZEOF(inputdev_task_stacks[i]),
				inputdev_task_entry, indev, NULL, NULL,
				INPUTDEV_TASK_PRIORITY, 0, K_NO_WAIT);

		snprintf(thread_name, sizeof(thread_name), "%s_Task", indev->config->name);
		k_thread_name_set(&inputdev_task_data[i], thread_name);
	}

	/* Create HID_Task thread */
	k_thread_create(&hid_task_data, hid_task_stack,
			K_THREAD_STACK_SIZEOF(hid_task_stack),
			hid_task_entry, NULL, NULL, NULL,
			HID_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&hid_task_data, "HID_Task");

	LOG_INF("HOST threads started (%d input devices). Main thread sleeping.",
		INPUT_DEVICE_COUNT);

	/* Wait for first input device task to complete - blocks indefinitely */
	k_thread_join(&inputdev_task_data[0], K_FOREVER);

	LOG_INF("HOST Server demo ended.");
	return 0;
} /* main */
