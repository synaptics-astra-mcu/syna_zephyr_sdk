/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * @brief Public API for input-device abstraction in the dualcore mailbox pipeline.
 *
 * @file input_device.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INPUT_DEVICE_H
/* Include guard for input-device public API. */
#define INPUT_DEVICE_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mbox.h>
#include <stdbool.h>
#include <stdint.h>
#include "mbox_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Global Definitions
 * ============================================================================ */

/** Maximum I2C receive data buffer size (write-read=25, read-only=21) */
#define INPUTDEV_I2C_RX_BUF_SIZE     25

/** Read-only transfer: plain read, 21-byte response */
#define INPUTDEV_READ_ONLY_RX_SIZE   21

/** Write-read transfer: write then read, 25-byte response */
#define INPUTDEV_WRITE_READ_RX_SIZE  25

/** Number of command bytes written to the slave for write-read transfer */
#define INPUTDEV_WRITE_READ_CMD_LEN  6

/** Invalid GPIO status return value */
#define INPUTDEV_GPIO_STATUS_INVALID 0xFFFFFFFFu

/** Default GPIO reset sequence delay in milliseconds */
#define INPUTDEV_RESET_SEQ_DELAY_MS  10

/** Event queue depth for input devices */
#define INPUTDEV_EVENT_QUEUE_DEPTH   8

/** Delay after each successful read transaction. */
#define INPUTDEV_POST_READ_DELAY_MS  5

/**
 * I2C probe addresses used during transfer-path auto-detection (8-bit IDs).
 *
 * Keep *_7BIT aliases aligned with the known-good transfer-path values used by this
 * project so bus transactions remain on 0x0A / 0x42 as validated on hardware.
 */
#define INPUTDEV_PROBE_ADDR_READ_ONLY   0x0Au
#define INPUTDEV_PROBE_ADDR_WRITE_READ  0x42u
#define INPUTDEV_PROBE_ADDR_READ_ONLY_7BIT INPUTDEV_PROBE_ADDR_READ_ONLY
#define INPUTDEV_PROBE_ADDR_WRITE_READ_7BIT INPUTDEV_PROBE_ADDR_WRITE_READ

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

/** Forward declaration for use in function pointer types */
struct input_device;

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Input device event types.
 */
typedef enum {
	INPUTDEV_EVENT_DATA_READY = 0,   /**< Data ready (GPIO falling edge) */
	INPUTDEV_EVENT_ERROR,            /**< Error condition */
} inputdev_event_type_t;

/**
 * @brief Input device event message structure.
 *
 * Queued from ISR to task for synchronized processing.
 */
struct inputdev_event_msg {
	inputdev_event_type_t type;      /**< Event type */
	int64_t timestamp;            /**< Kernel timestamp when event occurred */
};

/** Input device identifier */
typedef enum {
	INPUT_DEVICE_1 = 0,   /**< Input device 1 */
	INPUT_DEVICE_2 = 1,   /**< Input device 2 */
	INPUT_DEVICE_COUNT    /**< Number of input devices */
} inputdev_id_t;

/**
 * @brief Input device configuration structure.
 *
 * Contains all device-specific configuration parameters that are
 * set at initialization time and remain constant during operation.
 */
struct inputdev_config {
	const char *name;                   /**< Device name for logging */

	/* Data request GPIO (input with interrupt) */
	const struct device *data_req_port; /**< GPIO port for data request */
	gpio_pin_t data_req_pin;            /**< GPIO pin for data request */
	gpio_flags_t interrupt_flags;       /**< GPIO interrupt configuration flags */

	/* Reset GPIO (output) */
	const struct device *reset_port;    /**< GPIO port for reset control */
	gpio_pin_t reset_pin;               /**< GPIO pin for reset control */

	/* I2C configuration */
	struct i2c_dt_spec i2c;             /**< I2C device tree spec (bus + address) */
};

/**
 * @brief Input device operations structure (virtual function table).
 *
 * Allows for customization of device behavior. Each function pointer
 * can be overridden to provide device-specific implementations.
 * If NULL, the default implementation is used.
 */
struct inputdev_ops {
	/** Initialize device hardware. Returns 0 on success. */
	int (*init)(struct input_device *dev);

	/** Get data request GPIO status. Returns GPIO state or INPUTDEV_GPIO_STATUS_INVALID. */
	uint32_t (*get_status)(struct input_device *dev);

	/** Arm GPIO interrupt. Returns 0 on success. */
	int (*arm_interrupt)(struct input_device *dev);

	/** Execute reset sequence. */
	void (*reset)(struct input_device *dev);

	/** Read data via I2C. Returns 0 on success. */
	int (*read_data)(struct input_device *dev, uint8_t *buf, size_t len);
};

/**
 * @brief Input device runtime data structure.
 *
 * Contains mutable state that changes during device operation.
 */
struct inputdev_data {
	struct inputdev_ops ops_instance;   /**< Per-device ops copy (allows runtime customization) */
	struct gpio_callback gpio_cb;       /**< GPIO interrupt callback structure */
	struct k_msgq event_queue;          /**< Event message queue */
	char __aligned(4) event_queue_buf[INPUTDEV_EVENT_QUEUE_DEPTH * sizeof(struct inputdev_event_msg)];
	uint8_t rx_buf[INPUTDEV_I2C_RX_BUF_SIZE]; /**< I2C receive buffer */
	uint8_t mbox_tx_payload[sizeof(struct mbox_message) +
			       INPUTDEV_I2C_RX_BUF_SIZE]; /**< Per-device mbox TX buffer */
	uint16_t active_i2c_addr;           /**< Runtime-selected slave address for this interface (0x0A or 0x42) */
	uint32_t tx_counter;                /**< Message counter for mbox TX */
	uint32_t events_received;           /**< Total events received (debug) */
	uint32_t events_dropped;            /**< Events dropped due to full queue (debug) */
	bool initialized;                   /**< Device initialization status */
};

/**
 * @brief Input device instance structure.
 *
 * Combines configuration and runtime data into a single device object.
 * This is the primary handle used for all input device operations.
 */
struct input_device {
	const struct inputdev_config *config;    /**< Pointer to device configuration */
	struct inputdev_data *data;              /**< Pointer to device runtime data */
	const struct inputdev_ops *ops;          /**< Pointer to device operations (vtable) */
};

/* ============================================================================
 * Public API Prototypes
 * ============================================================================ */

/**
 * \function inputdev_get_default_ops
 *
 * \brief Get the default input device operations table.
 *
 * \param None.
 *
 * \return Pointer to default input-device operations.
 */
const struct inputdev_ops *inputdev_get_default_ops(void);

/**
 * \function inputdev_init
 *
 * \brief Initialize an input device.
 *
 * \param dev Pointer to the input device instance.
 *
 * \return 0 on success, negative error code on failure.
 */
int inputdev_init(struct input_device *dev);

/**
 * \function inputdev_get_status
 *
 * \brief Get the current data request GPIO status.
 *
 * \param dev Pointer to the input device instance.
 *
 * \return GPIO state (0 or 1), or INPUTDEV_GPIO_STATUS_INVALID on error.
 */
uint32_t inputdev_get_status(struct input_device *dev);

/**
 * \function inputdev_arm_interrupt
 *
 * \brief Arm the GPIO interrupt for the next falling edge.
 *
 * \param dev Pointer to the input device instance.
 *
 * \return 0 on success, negative error code on failure.
 */
int inputdev_arm_interrupt(struct input_device *dev);

/**
 * \function inputdev_reset_sequence
 *
 * \brief Execute the GPIO reset sequence for an input device.
 *
 * \param dev Pointer to the input device instance.
 *
 * \return None (void).
 */
void inputdev_reset_sequence(struct input_device *dev);

/**
 * \function inputdev_read_data
 *
 * \brief Read data from the input device via I2C.
 *
 * \param dev Pointer to the input device instance.
 * \param buf Buffer to store received data.
 * \param len Number of bytes to read.
 *
 * \return 0 on success, negative error code on failure.
 */
int inputdev_read_data(struct input_device *dev, uint8_t *buf, size_t len);

/**
 * \function inputdev_handle_data_request
 *
 * \brief Handle data request for an input device.
 *
 * \param dev Pointer to the input device instance.
 * \param tx_channel Pointer to mailbox TX channel spec.
 * \param mtu Maximum transfer unit for mailbox.
 *
 * \return 0 on success, negative error code on failure.
 */
int inputdev_handle_data_request(struct input_device *dev,
			      const struct mbox_dt_spec *tx_channel,
			      size_t mtu);

/**
 * \function inputdev_wait_event
 *
 * \brief Wait for an event from the input device event queue.
 *
 * \param dev Pointer to the input device instance.
 * \param event Pointer to store the received event.
 * \param timeout Timeout for waiting.
 *
 * \return 0 on success, -EAGAIN on timeout, negative error code on failure.
 */
int inputdev_wait_event(struct input_device *dev, struct inputdev_event_msg *event, k_timeout_t timeout);

/**
 * \function inputdev_peek_event
 *
 * \brief Peek at the next event without removing it from the queue.
 *
 * \param dev Pointer to the input device instance.
 * \param event Pointer to store the peeked event.
 *
 * \return 0 on success, -ENOMSG if queue is empty.
 */
int inputdev_peek_event(struct input_device *dev, struct inputdev_event_msg *event);

/**
 * \function inputdev_purge_events
 *
 * \brief Purge all events from the device queue.
 *
 * \param dev Pointer to the input device instance.
 *
 * \return None (void).
 */
void inputdev_purge_events(struct input_device *dev);

/**
 * \function inputdev_get_pending_events
 *
 * \brief Get the number of events pending in the queue.
 *
 * \param dev Pointer to the input device instance.
 *
 * \return Number of pending events.
 */
uint32_t inputdev_get_pending_events(struct input_device *dev);

/**
 * \function inputdev_get_event_stats
 *
 * \brief Get event statistics for debugging.
 *
 * \param dev Pointer to the input device instance.
 * \param received Pointer to store total events received (can be NULL).
 * \param dropped Pointer to store total events dropped (can be NULL).
 *
 * \return None (void).
 */
void inputdev_get_event_stats(struct input_device *dev, uint32_t *received, uint32_t *dropped);

/**
 * \function inputdev_probe_transfer_path
 *
 * \brief Auto-detect the I2C transfer path for a device.
 *
 * \param dev Pointer to the input device instance.
 *
 * \return None (void).
 */
void inputdev_probe_transfer_path(struct input_device *dev);

/**
 * \function inputdev_get_device
 *
 * \brief Get an input device instance by ID.
 *
 * \param id Input device identifier (INPUT_DEVICE_1 or INPUT_DEVICE_2).
 *
 * \return Pointer to the input device instance, or NULL if invalid ID.
 */
struct input_device *inputdev_get_device(inputdev_id_t id);

/**
 * \function inputdev_init_all
 *
 * \brief Initialize all input devices.
 *
 * \param None.
 *
 * \return 0 on success, negative error code if any device fails to initialize.
 */
int inputdev_init_all(void);

/**
 * \function inputdev_reset_all
 *
 * \brief Reset all input devices.
 *
 * \param None.
 *
 * \return None (void).
 */
void inputdev_reset_all(void);

/* ============================================================================
 * Macros for Device Definition
 * ============================================================================ */

/**
 * @brief Define an input device configuration.
 *
 * The I2C transfer path (read-only or write-read) is NOT set here;
 * it is determined at runtime by inputdev_probe_transfer_path() during init.
 * Write-read mode uses a fixed command of INPUTDEV_WRITE_READ_CMD_LEN bytes
 * (defined in input_device.c) for both probing and every subsequent read.
 *
 * @param _name      Device name string.
 * @param _req_port  Data request GPIO port device (DT_NODELABEL).
 * @param _req_pin   Data request GPIO pin number.
 * @param _int_flags GPIO interrupt flags (e.g., GPIO_INT_EDGE_FALLING).
 * @param _rst_port  Reset GPIO port device (DT_NODELABEL).
 * @param _rst_pin   Reset GPIO pin number.
 * @param _i2c_node  I2C device node (DT_NODELABEL) - must have reg property for address.
 */
#define INPUTDEV_CONFIG_DEFINE(_name, _req_port, _req_pin, _int_flags, _rst_port, _rst_pin, \
			       _i2c_node)                                                     \
	{                                                                                        \
		.name = _name,                                                                   \
		.data_req_port = DEVICE_DT_GET(DT_NODELABEL(_req_port)),                         \
		.data_req_pin = _req_pin,                                                        \
		.interrupt_flags = _int_flags,                                                   \
		.reset_port = DEVICE_DT_GET(DT_NODELABEL(_rst_port)),                            \
		.reset_pin = _rst_pin,                                                           \
		.i2c = I2C_DT_SPEC_GET(DT_NODELABEL(_i2c_node)),                                 \
	}

#ifdef __cplusplus
}
#endif

#endif /* INPUT_DEVICE_H */
