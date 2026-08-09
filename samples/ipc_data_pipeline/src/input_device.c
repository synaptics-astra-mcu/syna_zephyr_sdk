/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * @brief Handles input-device GPIO/I2C events and mailbox transmit.
 *
 * @file input_device.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "input_device.h"
#include "mbox_common.h"

LOG_MODULE_REGISTER(ipc_inputdev, CONFIG_LOG_DEFAULT_LEVEL);

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

/* Forward declaration of default ops table. */
static const struct inputdev_ops inputdev_ops;

/* Probe command used for 0x42 write-read detection. */
static const uint8_t write_read_probe_cmd[INPUTDEV_WRITE_READ_CMD_LEN] = {
	0x38, 0x2c, 0x00, 0x00, 0x4d, 0xde
};

/* ============================================================================
 * Private Data
 * ============================================================================ */

/* Static configuration for input devices */
static const struct inputdev_config inputdev_configs[INPUT_DEVICE_COUNT] = {
	[INPUT_DEVICE_1] =
		INPUTDEV_CONFIG_DEFINE("InputDev1", gpioa, 0, GPIO_INT_EDGE_FALLING,
				     gpioa, 4, i2c_controller_1),
	[INPUT_DEVICE_2] =
		INPUTDEV_CONFIG_DEFINE("InputDev2", gpioa, 18, GPIO_INT_EDGE_FALLING,
				     gpioa, 1, i2c_controller_2),
};

/* Runtime data for input devices */
static struct inputdev_data inputdev_data_instances[INPUT_DEVICE_COUNT];

/* Device instances combining config and data (ops set during init) */
static struct input_device input_devices[INPUT_DEVICE_COUNT] = {
	[INPUT_DEVICE_1] = {
		.config = &inputdev_configs[INPUT_DEVICE_1],
		.data = &inputdev_data_instances[INPUT_DEVICE_1],
	},
	[INPUT_DEVICE_2] = {
		.config = &inputdev_configs[INPUT_DEVICE_2],
		.data = &inputdev_data_instances[INPUT_DEVICE_2],
	},
};

/* ============================================================================
 * Private Function Prototypes
 * ============================================================================ */

static void inputdev_gpio_isr_callback(const struct device *port,
				    struct gpio_callback *cb,
				    uint32_t pins); // Triggered GPIO pin bitmask provided by ISR framework

/* Default operation implementations */
static int inputdev_default_init(struct input_device *dev);
static uint32_t inputdev_default_get_status(struct input_device *dev);
static int inputdev_default_arm_interrupt(struct input_device *dev);
static void inputdev_default_reset(struct input_device *dev);
static int inputdev_read_data_impl(struct input_device *dev, uint8_t *buf, size_t len);
static int inputdev_default_write_read_data_impl(struct input_device *dev, uint8_t *buf, size_t len);

/* ============================================================================
 * Default Operations (Virtual Function Table)
 * ============================================================================ */

/** Default operations for standard I2C-based input devices */
static const struct inputdev_ops inputdev_ops = {
	.init = inputdev_default_init,
	.get_status = inputdev_default_get_status,
	.arm_interrupt = inputdev_default_arm_interrupt,
	.reset = inputdev_default_reset,
	.read_data = inputdev_read_data_impl,
};

/* ============================================================================
 * Private Functions
 * ============================================================================ */

/**
 * \function inputdev_find_device_from_callback
 *
 * \brief Find the input device instance that owns a GPIO callback object.
 *
 * \param cb Pointer to the GPIO callback object.
 *
 * \return Pointer to matching input device, or NULL if not found.
 */
static struct input_device *inputdev_find_device_from_callback(struct gpio_callback *cb)
{
	struct inputdev_data *data = CONTAINER_OF(cb, struct inputdev_data, gpio_cb); // Runtime data block that owns this callback object

	/* Find which device this data belongs to */
	for (int i = 0; i < INPUT_DEVICE_COUNT; i++) {
		if (input_devices[i].data == data) {
			return &input_devices[i];
		}
	}
	return NULL;
} /* inputdev_find_device_from_callback */

/**
 * \function inputdev_gpio_isr_callback
 *
 * \brief Handle input-device GPIO interrupts and queue data-ready events.
 *
 * \param port GPIO device that raised the interrupt.
 * \param cb Pointer to registered callback object.
 * \param pins Bitmask of triggered pins.
 *
 * \return None (void).
 */
static void inputdev_gpio_isr_callback(const struct device *port,
				    struct gpio_callback *cb,
				    uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	struct input_device *dev = inputdev_find_device_from_callback(cb); // Device that owns the triggered GPIO callback
	if (dev == NULL) {
		return;
	}

	uint32_t pin_state = inputdev_get_status(dev); // Current data-ready GPIO pin level (0=asserted, 1=idle)
	if (pin_state == INPUTDEV_GPIO_STATUS_INVALID) {
		return;
	}

	/* Queue event on falling edge (active-low) */
	if (pin_state == 0) {
		struct inputdev_event_msg event = {
			.type = INPUTDEV_EVENT_DATA_READY,
			.timestamp = k_uptime_get(),
		};

		dev->data->events_received++;

		/* Non-blocking put in ISR context */
		if (k_msgq_put(&dev->data->event_queue, &event, K_NO_WAIT) != 0) {
			dev->data->events_dropped++;
		}
	}
} /* inputdev_gpio_isr_callback */

/* ============================================================================
 * Default Operation Implementations
 * ============================================================================ */

/**
 * \function inputdev_default_init
 *
 * \brief Default initialization sequence for an input device.
 *
 * \param dev Pointer to input device instance.
 *
 * \return 0 on success, negative error code on failure.
 */
static int inputdev_default_init(struct input_device *dev)
{
	int ret;                                      // Return code from GPIO and I2C configuration calls
	const struct inputdev_config *cfg = dev->config; // Static device configuration (pins, I2C spec)
	struct inputdev_data *data = dev->data;        // Mutable runtime state for this device

	if (data->initialized) {
		LOG_WRN("[%s] Already initialized", cfg->name);
		return 0;
	}

	/* Initialize event queue */
	k_msgq_init(&data->event_queue, data->event_queue_buf,
		    sizeof(struct inputdev_event_msg), INPUTDEV_EVENT_QUEUE_DEPTH);
	data->events_received = 0;
	data->events_dropped = 0;
	data->active_i2c_addr = INPUTDEV_PROBE_ADDR_READ_ONLY;

	/* Initialize per-device ops from default template */
	data->ops_instance = inputdev_ops;
	dev->ops = &data->ops_instance;

	/* Validate data request GPIO port */
	if (!device_is_ready(cfg->data_req_port)) {
		LOG_ERR("[%s] Data request GPIO port not ready", cfg->name);
		return -ENODEV;
	}

	/* Configure data request GPIO as input */
	ret = gpio_pin_configure(cfg->data_req_port, cfg->data_req_pin, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("[%s] Failed to configure data request GPIO, err=%d",
			cfg->name, ret);
		return ret;
	}

	/* Register GPIO callback */
	gpio_init_callback(&data->gpio_cb, inputdev_gpio_isr_callback,
			   BIT(cfg->data_req_pin));
	ret = gpio_add_callback(cfg->data_req_port, &data->gpio_cb);
	if (ret != 0) {
		LOG_ERR("[%s] Failed to add GPIO callback, err=%d", cfg->name, ret);
		return -EFAULT;
	}

	/* Arm initial edge interrupt */
	ret = inputdev_arm_interrupt(dev);
	if (ret != 0) {
		LOG_ERR("[%s] Failed to arm GPIO interrupt", cfg->name);
		return -EIO;
	}

	LOG_INF("[%s] Data request GPIO configured (pin %d)",
		cfg->name, cfg->data_req_pin);

	/* Validate and configure reset GPIO */
	if (!device_is_ready(cfg->reset_port)) {
		LOG_ERR("[%s] Reset GPIO port not ready", cfg->name);
		return -ENODEV;
	}

	ret = gpio_pin_configure(cfg->reset_port, cfg->reset_pin, GPIO_OUTPUT);
	if (ret != 0) {
		LOG_ERR("[%s] Failed to configure reset GPIO, err=%d",
			cfg->name, ret);
		return ret;
	}

	/* Set reset GPIO high (inactive) */
	gpio_pin_set_raw(cfg->reset_port, cfg->reset_pin, 1);
	LOG_INF("[%s] Reset GPIO configured (pin %d), set HIGH",
		cfg->name, cfg->reset_pin);

	/* Validate I2C device */
	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("[%s] I2C device not ready", cfg->name);
		return -ENODEV;
	}

	data->tx_counter = 0;
	data->initialized = true;

	LOG_INF("[%s] Initialization complete", cfg->name);
	return 0;
} /* inputdev_default_init */

/**
 * \function inputdev_default_get_status
 *
 * \brief Read data-request GPIO pin status using default implementation.
 *
 * \param dev Pointer to input device instance.
 *
 * \return GPIO level or INPUTDEV_GPIO_STATUS_INVALID on error.
 */
static uint32_t inputdev_default_get_status(struct input_device *dev)
{
	const struct inputdev_config *cfg = dev->config; // Static config providing the GPIO port and pin
	int gpio_state = gpio_pin_get(cfg->data_req_port, cfg->data_req_pin); // Raw GPIO read; negative value indicates error

	if (gpio_state < 0) {
		return INPUTDEV_GPIO_STATUS_INVALID;
	}

	return (uint32_t)gpio_state;
} /* inputdev_default_get_status */

/**
 * \function inputdev_default_arm_interrupt
 *
 * \brief Configure GPIO interrupt using default implementation.
 *
 * \param dev Pointer to input device instance.
 *
 * \return 0 on success, negative error code on failure.
 */
static int inputdev_default_arm_interrupt(struct input_device *dev)
{
	const struct inputdev_config *cfg = dev->config; // Static config providing interrupt pin and flags
	return gpio_pin_interrupt_configure(cfg->data_req_port,
					    cfg->data_req_pin,
					    cfg->interrupt_flags);
} /* inputdev_default_arm_interrupt */

/**
 * \function inputdev_default_reset
 *
 * \brief Toggle reset GPIO lines using default reset sequence.
 *
 * \param dev Pointer to input device instance.
 *
 * \return None (void).
 */
static void inputdev_default_reset(struct input_device *dev)
{
	const struct inputdev_config *cfg = dev->config; // Static config providing the reset GPIO port and pin

	LOG_INF("[%s] Starting reset sequence...", cfg->name);

	/* Phase 1: Drive HIGH */
	gpio_pin_set_raw(cfg->reset_port, cfg->reset_pin, 1);
	LOG_INF("[%s] Reset GPIO = %d (phase 1: HIGH)", cfg->name,
		gpio_pin_get_raw(cfg->reset_port, cfg->reset_pin));
	k_msleep(INPUTDEV_RESET_SEQ_DELAY_MS);

	/* Phase 2: Drive LOW (reset asserted) */
	gpio_pin_set_raw(cfg->reset_port, cfg->reset_pin, 0);
	LOG_INF("[%s] Reset GPIO = %d (phase 2: LOW)", cfg->name,
		gpio_pin_get_raw(cfg->reset_port, cfg->reset_pin));
	k_msleep(INPUTDEV_RESET_SEQ_DELAY_MS);

	/* Phase 3: Drive HIGH (reset de-asserted) */
	gpio_pin_set_raw(cfg->reset_port, cfg->reset_pin, 1);
	LOG_INF("[%s] Reset GPIO = %d (phase 3: HIGH)", cfg->name,
		gpio_pin_get_raw(cfg->reset_port, cfg->reset_pin));

	LOG_INF("[%s] Reset sequence completed", cfg->name);
} /* inputdev_default_reset */

/**
 * \function inputdev_read_with_addr
 *
 * \brief Execute one plain-read I2C transfer on the selected address.
 */
static int inputdev_read_with_addr(const struct inputdev_config *cfg,
				  uint16_t addr7,
				  uint8_t *buf,
				  size_t len)
{
	return i2c_read(cfg->i2c.bus, buf, len, addr7);
} /* inputdev_read_with_addr */

/**
 * \function inputdev_write_read_with_addr
 *
 * \brief Execute one write-read I2C transfer on the selected address.
 */
static int inputdev_write_read_with_addr(const struct inputdev_config *cfg,
					 uint16_t addr7,
					 uint8_t *buf,
					 size_t len,
					 size_t rd_len)
{
	(void)memset(buf, 0, len);
	return i2c_write_read(cfg->i2c.bus,
			      addr7,
			      write_read_probe_cmd,
			      sizeof(write_read_probe_cmd),
			      buf,
			      rd_len);
} /* inputdev_write_read_with_addr */

/**
 * \function inputdev_read_data_impl
 *
 * \brief Read data using plain-read transfer on the selected runtime address.
 */
static int inputdev_read_data_impl(struct input_device *dev, uint8_t *buf, size_t len)
{
	const struct inputdev_config *cfg = dev->config;
	struct inputdev_data *data = dev->data;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("[%s] I2C device not ready", cfg->name);
		return -ENODEV;
	}

	return inputdev_read_with_addr(cfg, data->active_i2c_addr, buf, len);
} /* inputdev_read_data_impl */

/**
 * \function inputdev_default_write_read_data_impl
 *
 * \brief Read data using write-read transfer on the selected runtime address.
 */
static int inputdev_default_write_read_data_impl(struct input_device *dev, uint8_t *buf, size_t len)
{
	const struct inputdev_config *cfg = dev->config;
	struct inputdev_data *data = dev->data;
	size_t rd_len = (len < INPUTDEV_WRITE_READ_RX_SIZE) ? len : INPUTDEV_WRITE_READ_RX_SIZE;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("[%s] I2C device not ready", cfg->name);
		return -ENODEV;
	}

	return inputdev_write_read_with_addr(cfg, data->active_i2c_addr, buf, len, rd_len);
} /* inputdev_default_write_read_data_impl */

/* ============================================================================
 * Public API (Exported Functions)
 * ============================================================================ */

/**
 * \function inputdev_get_default_ops
 *
 * \brief Get the default operation table for input devices.
 *
 * \param None.
 *
 * \return Pointer to default input-device operations.
 */
const struct inputdev_ops *inputdev_get_default_ops(void)
{
	return &inputdev_ops;
} /* inputdev_get_default_ops */

/**
 * \function inputdev_get_device
 *
 * \brief Get input device instance by device ID.
 *
 * \param id Input device identifier.
 *
 * \return Pointer to device instance, or NULL if id is invalid.
 */
struct input_device *inputdev_get_device(inputdev_id_t id)
{
	if (id >= INPUT_DEVICE_COUNT) {
		return NULL;
	}
	return &input_devices[id];
} /* inputdev_get_device */

/**
 * \function inputdev_init
 *
 * \brief Initialize an input device through configured ops.
 *
 * \param dev Pointer to input device instance.
 *
 * \return 0 on success, negative error code on failure.
 */
int inputdev_init(struct input_device *dev)
{
	if (dev->ops && dev->ops->init) {
		return dev->ops->init(dev);
	}
	return inputdev_default_init(dev);
} /* inputdev_init */

/**
 * \function inputdev_get_status
 *
 * \brief Get current data-request GPIO state for an input device.
 *
 * \param dev Pointer to input device instance.
 *
 * \return GPIO state or INPUTDEV_GPIO_STATUS_INVALID.
 */
uint32_t inputdev_get_status(struct input_device *dev)
{
	if (dev->ops && dev->ops->get_status) {
		return dev->ops->get_status(dev);
	}
	return inputdev_default_get_status(dev);
} /* inputdev_get_status */

/**
 * \function inputdev_arm_interrupt
 *
 * \brief Arm interrupt configuration for the input device GPIO.
 *
 * \param dev Pointer to input device instance.
 *
 * \return 0 on success, negative error code on failure.
 */
int inputdev_arm_interrupt(struct input_device *dev)
{
	if (dev->ops && dev->ops->arm_interrupt) {
		return dev->ops->arm_interrupt(dev);
	}
	return inputdev_default_arm_interrupt(dev);
} /* inputdev_arm_interrupt */

/**
 * \function inputdev_reset_sequence
 *
 * \brief Execute reset sequence through configured device ops.
 *
 * \param dev Pointer to input device instance.
 *
 * \return None (void).
 */
void inputdev_reset_sequence(struct input_device *dev)
{
	if (dev->ops && dev->ops->reset) {
		dev->ops->reset(dev);
		return;
	}
	inputdev_default_reset(dev);
} /* inputdev_reset_sequence */

/**
 * \function inputdev_read_data
 *
 * \brief Read input data through configured device ops.
 *
 * \param dev Pointer to input device instance.
 * \param buf Destination buffer for received bytes.
 * \param len Number of bytes requested.
 *
 * \return 0 on success, negative error code on failure.
 */
int inputdev_read_data(struct input_device *dev, uint8_t *buf, size_t len)
{
	if (dev->ops && dev->ops->read_data) {
		return dev->ops->read_data(dev, buf, len);
	}
	return inputdev_read_data_impl(dev, buf, len);
} /* inputdev_read_data */

/**
 * \function inputdev_probe_transfer_path
 *
 * \brief Probe runtime I2C transfer path/address for an input device.
 *
 * \param dev Pointer to input device instance.
 *
 * \return None (void).
 */
void inputdev_probe_transfer_path(struct input_device *dev)
{
	const struct inputdev_config *cfg = dev->config;              // Static config providing the I2C bus and device name
	struct inputdev_data *data = dev->data;                       // Mutable state where the selected address/path is written
	uint8_t probe_buf[INPUTDEV_READ_ONLY_RX_SIZE] = {0};          // Scratch buffer for probe read responses
	int ret;                                                      // Result from probe attempts

	/* Safe defaults if probing fails (ops already initialized, just reset read_data). */
	data->ops_instance.read_data = inputdev_read_data_impl;
	data->active_i2c_addr = INPUTDEV_PROBE_ADDR_READ_ONLY;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_WRN("[%s] I2C bus not ready, path probe skipped", cfg->name);
		return;
	}

	/* Probe write-read path first on address 0x42. */
	data->active_i2c_addr = INPUTDEV_PROBE_ADDR_WRITE_READ_7BIT;
	ret = inputdev_default_write_read_data_impl(dev, probe_buf, sizeof(probe_buf));
	if (ret == 0) {
		data->ops_instance.read_data = inputdev_default_write_read_data_impl;
		LOG_INF("[%s] Probe OK on %s: addr=0x%02x path=write-read",
			cfg->name,
			cfg->i2c.bus->name,
			(unsigned int)data->active_i2c_addr);
		return;
	}
	LOG_WRN("[%s] Probe attempt write-read@0x%02x failed, err=%d",
		cfg->name,
		(unsigned int)INPUTDEV_PROBE_ADDR_WRITE_READ_7BIT,
		ret);

	/* Fallback to read-only path on address 0x0A. */
	data->active_i2c_addr = INPUTDEV_PROBE_ADDR_READ_ONLY_7BIT;
	ret = inputdev_read_data(dev, probe_buf, sizeof(probe_buf));
	if (ret == 0) {
		/* read_data already set to default impl */
		LOG_INF("[%s] Probe OK on %s: addr=0x%02x path=read",
			cfg->name,
			cfg->i2c.bus->name,
			(unsigned int)data->active_i2c_addr);
		return;
	}
	LOG_WRN("[%s] Probe attempt read@0x%02x failed, err=%d",
		cfg->name,
		(unsigned int)INPUTDEV_PROBE_ADDR_READ_ONLY_7BIT,
		ret);

	/* Keep default and make fallback explicit in logs. */
	LOG_WRN("[%s] Probe failed on %s (0x42/0x0A), fallback addr=0x%02x path=read",
		cfg->name,
		cfg->i2c.bus->name,
		(unsigned int)data->active_i2c_addr);
} /* inputdev_probe_transfer_path */

/**
 * \function inputdev_handle_data_request
 *
 * \brief Read device data and send it over mailbox to CLIENT.
 *
 * \param dev Pointer to input device instance.
 * \param tx_channel Pointer to mailbox TX channel.
 * \param mtu Mailbox maximum transfer size.
 *
 * \return 0 on success, negative error code on failure.
 */
int inputdev_handle_data_request(struct input_device *dev,
			      const struct mbox_dt_spec *tx_channel,
			      size_t mtu)
{
	const struct inputdev_config *cfg = dev->config;             // Static config used for logging device name
	struct inputdev_data *data = dev->data;                      // Mutable state holding rx_buf, counter, and TX payload buffer
	struct mbox_message tx_msg = {0};                            // Mailbox header populated with source, counter, and timestamp
	struct mbox_msg mbox_msg = {0};                              // Zephyr mailbox descriptor wrapping the TX payload
	const size_t required_size = sizeof(tx_msg) + sizeof(data->rx_buf); // Total payload bytes required from mailbox MTU
	inputdev_id_t dev_id = INPUT_DEVICE_COUNT;                   // Resolved device index; initialized to sentinel (invalid)
	int ret;                                                     // Return code from I2C read and mailbox send calls

	/* Map device pointer to stable interface id (INPUT_DEVICE_1/2). */
	for (int i = 0; i < INPUT_DEVICE_COUNT; i++) {
		if (dev == &input_devices[i]) {
			dev_id = (inputdev_id_t)i;
			break;
		}
	}

	if (dev_id == INPUT_DEVICE_COUNT) {
		LOG_ERR("[%s] Invalid input device pointer", cfg->name);
		return -EINVAL;
	}

	ret = inputdev_read_data(dev, data->rx_buf, sizeof(data->rx_buf));
	if (ret == -EAGAIN) {
		/* Retry is intentionally deferred to keep ISR/task flow responsive. */
		k_msleep(1);
		return 0;
	}

	/* reduce logs when device is not connected */
	static uint32_t g_log_drop_count;
	g_log_drop_count++;
	if (ret != 0) {
		if ((g_log_drop_count == 1u) || ((g_log_drop_count % 10u) == 0u)) {
			LOG_ERR("[%s] I2C read failed, err=%d", cfg->name, ret);
		}
		return ret;
	}

	/*
	 * Short pacing delay after each successful full read (A=25B, B=21B)
	 * to avoid back-to-back bus pressure while data-ready remains asserted.
	 */
	k_msleep(INPUTDEV_POST_READ_DELAY_MS);

	/* Prepare mbox message */
	tx_msg.source = (source_t)dev_id;
	tx_msg.counter = data->tx_counter;
	tx_msg.timestamp = k_uptime_get();

	if (mtu < required_size) {
		LOG_ERR("[%s] MTU=%zu too small, need %zu bytes",
			cfg->name, mtu, required_size);
		return -ENOMEM;
	}

	/* Use per-device buffer to avoid race conditions */
	memcpy(data->mbox_tx_payload, &tx_msg, sizeof(tx_msg));
	memcpy(data->mbox_tx_payload + sizeof(tx_msg), data->rx_buf, sizeof(data->rx_buf));

	mbox_msg.data = data->mbox_tx_payload;
	mbox_msg.size = sizeof(tx_msg) + sizeof(data->rx_buf);

	ret = mbox_send_dt(tx_channel, &mbox_msg);
	if (ret < 0) {
		LOG_ERR("[%s] mbox_send() failed, err=%d", cfg->name, ret);
		return ret;
	}

	data->tx_counter++;
	return 0;
} /* inputdev_handle_data_request */

/**
 * \function inputdev_wait_event
 *
 * \brief Wait for next input-device event from queue.
 *
 * \param dev Pointer to input device instance.
 * \param event Output event structure.
 * \param timeout Wait timeout.
 *
 * \return 0 on success, -EAGAIN on timeout, negative error code on failure.
 */
int inputdev_wait_event(struct input_device *dev, struct inputdev_event_msg *event, k_timeout_t timeout)
{
	struct inputdev_data *data = dev->data; // Device runtime data holding the event queue
	return k_msgq_get(&data->event_queue, event, timeout);
} /* inputdev_wait_event */

/**
 * \function inputdev_peek_event
 *
 * \brief Peek next queued event without removing it.
 *
 * \param dev Pointer to input device instance.
 * \param event Output event structure.
 *
 * \return 0 on success, -ENOMSG if queue is empty.
 */
int inputdev_peek_event(struct input_device *dev, struct inputdev_event_msg *event)
{
	struct inputdev_data *data = dev->data; // Device runtime data holding the event queue
	return k_msgq_peek(&data->event_queue, event);
} /* inputdev_peek_event */

/**
 * \function inputdev_purge_events
 *
 * \brief Purge all pending events from input-device queue.
 *
 * \param dev Pointer to input device instance.
 *
 * \return None (void).
 */
void inputdev_purge_events(struct input_device *dev)
{
	struct inputdev_data *data = dev->data; // Device runtime data holding the event queue to purge
	k_msgq_purge(&data->event_queue);
} /* inputdev_purge_events */

/**
 * \function inputdev_get_pending_events
 *
 * \brief Get number of currently queued events.
 *
 * \param dev Pointer to input device instance.
 *
 * \return Number of pending queue entries.
 */
uint32_t inputdev_get_pending_events(struct input_device *dev)
{
	struct inputdev_data *data = dev->data; // Device runtime data holding the event queue
	return k_msgq_num_used_get(&data->event_queue);
} /* inputdev_get_pending_events */

/**
 * \function inputdev_get_event_stats
 *
 * \brief Get cumulative event receive/drop counters.
 *
 * \param dev Pointer to input device instance.
 * \param received Output total received events pointer (nullable).
 * \param dropped Output total dropped events pointer (nullable).
 *
 * \return None (void).
 */
void inputdev_get_event_stats(struct input_device *dev, uint32_t *received, uint32_t *dropped)
{
	struct inputdev_data *data = dev->data; // Device runtime data holding the event counters
	if (received != NULL) {
		*received = data->events_received;
	}
	if (dropped != NULL) {
		*dropped = data->events_dropped;
	}
} /* inputdev_get_event_stats */

/**
 * \function inputdev_init_all
 *
 * \brief Initialize all registered input devices.
 *
 * \param None.
 *
 * \return 0 on success, negative error code on first failure.
 */
int inputdev_init_all(void)
{
	int ret; // Return code from individual device initialization calls

	for (int i = 0; i < INPUT_DEVICE_COUNT; i++) {
		ret = inputdev_init(&input_devices[i]);
		if (ret != 0) {
			LOG_ERR("Failed to initialize input device %d, err=%d", i, ret);
			return ret;
		}
	}

	return 0;
} /* inputdev_init_all */

/**
 * \function inputdev_reset_all
 *
 * \brief Execute reset sequence on all registered input devices.
 *
 * \param None.
 *
 * \return None (void).
 */
void inputdev_reset_all(void)
{
	LOG_INF("Starting reset sequence for all input devices...");

	/* Drive all reset GPIOs HIGH */
	for (int i = 0; i < INPUT_DEVICE_COUNT; i++) {
		const struct inputdev_config *cfg = input_devices[i].config; // Static reset GPIO mapping for device i
		gpio_pin_set_raw(cfg->reset_port, cfg->reset_pin, 1);
	}
	k_msleep(INPUTDEV_RESET_SEQ_DELAY_MS);

	/* Drive all reset GPIOs LOW (assert reset) */
	for (int i = 0; i < INPUT_DEVICE_COUNT; i++) {
		const struct inputdev_config *cfg = input_devices[i].config; // Static reset GPIO mapping for device i
		gpio_pin_set_raw(cfg->reset_port, cfg->reset_pin, 0);
	}
	k_msleep(INPUTDEV_RESET_SEQ_DELAY_MS);

	/* Drive all reset GPIOs HIGH (de-assert reset) */
	for (int i = 0; i < INPUT_DEVICE_COUNT; i++) {
		const struct inputdev_config *cfg = input_devices[i].config; // Static reset GPIO mapping for device i
		gpio_pin_set_raw(cfg->reset_port, cfg->reset_pin, 1);
	}

	LOG_INF("Reset sequence completed for all input devices");
} /* inputdev_reset_all */
