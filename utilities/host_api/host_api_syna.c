/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "host_api.h"
#include "host_api_internal.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>

#include <logger.h>

#include "host_api_utils.h"
#if defined(CONFIG_SERVICE_FLASH_AUX_ENABLED)
#include "service_flash_aux.h"
#endif
#if defined(CONFIG_SERVICE_SYSTEM_ENABLED)
#include "service_system.h"
#endif
#if defined(CONFIG_SERVICE_UC_MANAGER_ENABLED)
#include "service_uc_manager.h"
#endif

#ifndef LOG_MOD_HOST_API
#define LOG_MOD_HOST_API "HOST_API"
#endif

#define ONE_BYTE 1U
#define HEADER_LENGTH_NO_SYNC 6U
#define HEADER_LENGTH sizeof(h_api_header_t)
#define MAX_ACK_BUFFER_SIZE HEADER_LENGTH
#define MAX_EVENT_BUFFER_SIZE 50U
#define MAX_EVENTS 5U

#define NO_PAYLOAD 0U
#define PRINT_HEADER 0x1
#define PRINT_DATA 0x2
#define PRINT_ACK 0x3
#define PRINT_RESPONSE 0x4
#define PRINT_NACK 0x5

#define RESPONSE_READY_TIMEOUT K_MSEC(100)
#define HOST_API_USB_DTR_POLL_INTERVAL K_MSEC(10)
#define HOST_API_RESET_DELAY_MS 100U

#define SR100_AON_POR_MASK_EVENTS_REG 0x50350038U
#define SR100_AON_POR_RST_EVENT_SW_RESET_BIT 11U
#define SR100_GLOBAL_BASE_ADDRESS 0x50330000U
#define SR100_GLOBAL_RESET_TRIGGER_OFFSET 0x600U

#define MAX_SERVICES 15U
#define NUM_OF_FIXED_SERVICES 3U
#define MAX_EXTERNAL_SERVICES (MAX_SERVICES - NUM_OF_FIXED_SERVICES)
#define QUEUE_LENGTH 1U

#define CDC_RX_RING_BUFFER_SIZE MAX_REQUEST_BUFFER_SIZE

BUILD_ASSERT(HOST_API_NEW_FORMAT == 1, "This port currently supports only Host API v0.4");
BUILD_ASSERT(sizeof(h_api_header_t) == 8U, "Host API header must be 8 bytes");

typedef struct communication_interface {
	int32_t active;
	int32_t (*send)(const void *data, uint32_t num);
	int32_t (*receive)(void *data, uint32_t num);
} communication_interface_t;

typedef struct st_request_metadata {
	uint8_t ack;
	uint8_t response;
	uint8_t service_id;
	uint8_t opcode;
	uint32_t data_length;
} request_metadata_t;

typedef struct queue_entry {
	uint16_t id;
	struct k_msgq *queue;
} queue_entry_t;

typedef struct event_buffer {
	uint8_t event_buffer[MAX_EVENT_BUFFER_SIZE];
} event_buffer_t;

typedef struct st_event_manager {
	struct k_sem event_sem;
	uint8_t num_of_events;
	uint8_t read_index;
	uint8_t write_index;
	event_buffer_t events[MAX_EVENTS];
} event_manager_t;

struct host_api_usb_context {
	const struct device *cdc_dev;
	struct k_sem tx_done_sem;
	struct k_sem rx_ready_sem;
	struct ring_buf rx_ringbuf;
	uint8_t rx_ring_storage[CDC_RX_RING_BUFFER_SIZE];
	const uint8_t *tx_ptr;
	size_t tx_remaining;
	bool line_signals_asserted;
	bool dtr_was_high;
	bool wait_online_logged;
	bool initialized;
};

static int service_external_handler(uint8_t opcode_id, uint8_t *p_input, uint8_t *p_output);
static void fill_response(int type, uint8_t *output, uint16_t buffer_len);
static int check_params(uint8_t *buffer, uint16_t len);
static void host_api_run_loop(void);
static int32_t host_api_usb_init(void);
static int32_t send(const void *data, uint32_t num);
static int32_t receive(void *data, uint32_t num);
static void clean_buffers(void);
static void host_api_execute_scheduled_reset(void);
static void host_api_trigger_reset_now(void);

static struct handler_entry services[MAX_SERVICES] = {
#if defined(CONFIG_SERVICE_SYSTEM_ENABLED)
	{ HOST_API_SERVICE_ID_SYSTEM, service_system_handler },
#endif
#if defined(CONFIG_SERVICE_FLASH_AUX_ENABLED)
	{ HOST_API_SERVICE_ID_FLASH_AUX, service_flash_aux_handler },
#endif
#if defined(CONFIG_SERVICE_UC_MANAGER_ENABLED)
	{ HOST_API_SERVICE_ID_UC_MANAGER, service_uc_manager_handler },
#endif
};

static queue_entry_t service_queues[MAX_SERVICES] = { 0 };

static uint8_t header_buffer[HEADER_LENGTH];
static uint8_t request_buffer[MAX_REQUEST_BUFFER_SIZE + HEADER_LENGTH];
static uint8_t response_buffer[MAX_RESPONSE_BUFFER_SIZE] = { 0 };
static uint8_t ack_buffer[MAX_ACK_BUFFER_SIZE] = { 0 };
static uint16_t request_buf_size;
static communication_interface_t interface_ctx;
static event_manager_t event_manager;
static struct k_msgq *service_queue;
static struct k_msgq host_api_queue;
static char host_api_queue_buffer[QUEUE_LENGTH * sizeof(int32_t)];
static struct host_api_usb_context usb_ctx;
static volatile bool host_api_ready;
static volatile bool host_api_started;
static volatile uint32_t host_api_reset_delay_ms;
static volatile bool host_api_reset_pending;

K_THREAD_STACK_DEFINE(host_api_stack, CONFIG_HOST_API_THREAD_STACK_SIZE);
static struct k_thread host_api_thread;

static const char *const interfaces[] = { "UART", "USB", "I2C", "SPI" };

static void host_api_usb_wait_online(void)
{
	uint32_t dtr = 0U;
	bool dtr_high;

	if (usb_ctx.cdc_dev == NULL) {
		return;
	}

	while (true) {
		dtr_high = (uart_line_ctrl_get(usb_ctx.cdc_dev, UART_LINE_CTRL_DTR, &dtr) == 0) &&
			   (dtr != 0U);

		if (dtr_high) {
			if (!usb_ctx.dtr_was_high) {
				unsigned int key;

				key = irq_lock();
				ring_buf_reset(&usb_ctx.rx_ringbuf);
				irq_unlock(key);
				k_sem_reset(&usb_ctx.rx_ready_sem);
				(void)uart_line_ctrl_set(usb_ctx.cdc_dev, UART_LINE_CTRL_DCD, 1);
				(void)uart_line_ctrl_set(usb_ctx.cdc_dev, UART_LINE_CTRL_DSR, 1);
				k_msleep(100);
				usb_ctx.dtr_was_high = true;
				usb_ctx.line_signals_asserted = true;
				usb_ctx.wait_online_logged = false;
				LOG_INFO(LOG_MOD_HOST_API, "USB CDC DTR asserted; host session ready\n");
			}
			return;
		}

		if (usb_ctx.dtr_was_high || usb_ctx.line_signals_asserted) {
			(void)uart_line_ctrl_set(usb_ctx.cdc_dev, UART_LINE_CTRL_DCD, 0);
			(void)uart_line_ctrl_set(usb_ctx.cdc_dev, UART_LINE_CTRL_DSR, 0);
			usb_ctx.dtr_was_high = false;
			usb_ctx.line_signals_asserted = false;
			usb_ctx.wait_online_logged = false;
			LOG_INFO(LOG_MOD_HOST_API, "USB CDC DTR deasserted; host session closed\n");
		}

		if (!usb_ctx.wait_online_logged) {
			LOG_INFO(LOG_MOD_HOST_API, "USB CDC ready; waiting for host DTR...\n");
			usb_ctx.wait_online_logged = true;
		}

		k_msleep(10);
	}
}

static void host_api_usb_irq(const struct device *dev, void *user_data)
{
	struct host_api_usb_context *ctx = user_data;

	if ((dev == NULL) || (ctx == NULL)) {
		return;
	}

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buffer[64];
			int recv_len;
			uint32_t rb_len;

			recv_len = uart_fifo_read(dev, buffer, sizeof(buffer));
			if (recv_len < 0) {
				LOG_ERROR(LOG_MOD_HOST_API, "Failed to read CDC RX FIFO\n");
				continue;
			}

			rb_len = ring_buf_put(&ctx->rx_ringbuf, buffer, (uint32_t)recv_len);
			if (rb_len < (uint32_t)recv_len) {
				LOG_ERROR(LOG_MOD_HOST_API, "Drop %u CDC RX bytes\n",
					  (unsigned int)((uint32_t)recv_len - rb_len));
			}

			if (rb_len > 0U) {
				k_sem_give(&ctx->rx_ready_sem);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			int wrote;

			if (ctx->tx_remaining == 0U) {
				uart_irq_tx_disable(dev);
				k_sem_give(&ctx->tx_done_sem);
				continue;
			}

			wrote = uart_fifo_fill(dev, ctx->tx_ptr, ctx->tx_remaining);
			if (wrote > 0) {
				ctx->tx_ptr += (size_t)wrote;
				ctx->tx_remaining -= (size_t)wrote;
			} else {
				break;
			}
		}
	}
}

static int host_api_usb_read(void *data, size_t len)
{
	uint8_t *dst = data;
	size_t received = 0U;

	if ((data == NULL) || (len == 0U)) {
		return -EINVAL;
	}

	host_api_usb_wait_online();

	while (received < len) {
		uint32_t chunk = ring_buf_get(&usb_ctx.rx_ringbuf, dst + received,
					      (uint32_t)(len - received));

		if (chunk > 0U) {
			received += chunk;
			continue;
		}

		if (k_sem_take(&usb_ctx.rx_ready_sem, HOST_API_USB_DTR_POLL_INTERVAL) != 0) {
			host_api_usb_wait_online();
		}
	}

	return 0;
}

static int host_api_usb_write(const void *data, size_t len)
{
	unsigned int key;
	int ret;

	if ((data == NULL) || (len == 0U)) {
		return -EINVAL;
	}

	host_api_usb_wait_online();

	key = irq_lock();
	uart_irq_tx_disable(usb_ctx.cdc_dev);
	usb_ctx.tx_ptr = data;
	usb_ctx.tx_remaining = len;
	irq_unlock(key);

	k_sem_reset(&usb_ctx.tx_done_sem);
	uart_irq_tx_enable(usb_ctx.cdc_dev);

	ret = k_sem_take(&usb_ctx.tx_done_sem, K_SECONDS(10));
	if (ret != 0) {
		uart_irq_tx_disable(usb_ctx.cdc_dev);
		usb_ctx.tx_ptr = NULL;
		usb_ctx.tx_remaining = 0U;
	}

	return ret;
}

static uint16_t get_allowed_req_size(uint8_t service_id)
{
	if (service_id == HOST_API_SERVICE_ID_USB_BOOT) {
		return MAX_REQUEST_BUFFER_SIZE;
	}

	return MIN_REQUEST_BUFFER_SIZE;
}

int32_t h_api_register_service(struct k_msgq *p_queue, uint32_t service_id)
{
	int i = 0;
	int j = 0;

	while (i < MAX_SERVICES) {
		if (services[i].id == 0U) {
			break;
		}
		if ((service_id != EXTERNAL_SERVICE_REGISTRATION) && (services[i].id == service_id)) {
			break;
		}
		i++;
	}

	if (i == MAX_SERVICES) {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "h_api_register_service() failed, maximum services amount was reached.\n");
		return HOST_API_RC_TOO_MANY_SERVICES;
	}

	if ((service_id != 0U) && (services[i].id == service_id)) {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "h_api_register_service() failed, service ID %d already exists.\n",
			  (int)service_id);
		return HOST_API_RC_ALREADY_REGISTERED;
	}

	while (j < MAX_EXTERNAL_SERVICES) {
		if (service_queues[j].id != 0U) {
			if (service_queues[j].queue == p_queue) {
				LOG_ERROR(LOG_MOD_HOST_API,
					  "h_api_register_service() failed, service is already registered.\n");
				return HOST_API_RC_ALREADY_REGISTERED;
			}
		} else {
			break;
		}
		j++;
	}

	if (service_id != EXTERNAL_SERVICE_REGISTRATION) {
		services[i].id = (uint16_t)service_id;
		service_queues[j].id = (uint16_t)service_id;
	} else {
		services[i].id = (uint16_t)(i + 1);
		service_queues[j].id = (uint16_t)(i + 1);
	}

	services[i].handler = service_external_handler;
	service_queues[j].queue = p_queue;

	LOG_DEBUG(LOG_MOD_HOST_API, "A new service was registered, service ID: %d.\n",
		  services[i].id);
	return services[i].id;
}

int32_t h_api_response_ready(int32_t *rc)
{
	int ret;

	ret = k_msgq_put(&host_api_queue, rc, RESPONSE_READY_TIMEOUT);
	if (ret == 0) {
		return HOST_API_RC_OK;
	}

	LOG_ERROR(LOG_MOD_HOST_API, "h_api_response_ready() failed, reached timeout.\n");
	return HOST_API_RC_TIMEOUT;
}

int32_t h_api_event_notify(uint8_t *data_buffer, uint16_t buffer_len)
{
	uint8_t *event_buffer;
	int32_t rc;

	LOG_DEBUG(LOG_MOD_HOST_API, "Call %s()\n", __func__);

	rc = check_params(data_buffer, buffer_len);
	if (rc != HOST_API_RC_OK) {
		return rc;
	}

	if (k_sem_take(&event_manager.event_sem, K_NO_WAIT) == 0) {
		event_buffer = event_manager.events[event_manager.write_index].event_buffer;
		event_manager.write_index = (uint8_t)((event_manager.write_index + 1U) % MAX_EVENTS);
		event_manager.num_of_events++;
		k_sem_give(&event_manager.event_sem);
	} else {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "h_api_event_notify() failed, semaphore was not acquired.\n");
		return HOST_API_RC_SEMAPHORE_TIMEOUT;
	}

	fill_response(HOST_API_EVENT_NOTIFY, event_buffer, buffer_len);
	memcpy(&event_buffer[HEADER_LENGTH], data_buffer, buffer_len);
	return HOST_API_RC_OK;
}

static int service_external_handler(uint8_t opcode_id, uint8_t *p_input, uint8_t *p_output)
{
	h_api_message_t message = {
		.opcode_id = opcode_id,
		.p_input = p_input,
		.p_output = p_output,
	};

	if ((service_queue != NULL) && (k_msgq_put(service_queue, &message, K_NO_WAIT) == 0)) {
		return HOST_API_RC_OK;
	}

	return HOST_API_RC_TIMEOUT;
}

static int check_params(uint8_t *buffer, uint16_t len)
{
	if (buffer == NULL) {
		LOG_ERROR(LOG_MOD_HOST_API, "h_api_event_notify() failed, buffer pointer is null.\n");
		return HOST_API_RC_NULL_POINTER;
	}

	if (len == 0U) {
		LOG_ERROR(LOG_MOD_HOST_API, "h_api_event_notify() failed, buffer length is 0.\n");
		return HOST_API_RC_BUFFER_LEN_0;
	}

	if (event_manager.num_of_events == MAX_EVENTS) {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "h_api_event_notify() failed, maximum events amount was reached.\n");
		return HOST_API_RC_TOO_MANY_EVENTS;
	}

	if (len > MAX_EVENT_BUFFER_SIZE) {
		LOG_ERROR(LOG_MOD_HOST_API, "h_api_event_notify() failed, buffer size is too big.\n");
		return HOST_API_RC_DATA_TOO_LONG;
	}

	return HOST_API_RC_OK;
}

int32_t send(const void *data, uint32_t num)
{
	int32_t rc = HOST_API_RC_ERROR;

	switch (interface_ctx.active) {
	case ACTIVE_INTERFACE_USB:
		if (host_api_usb_write(data, num) == 0) {
			rc = HOST_API_RC_OK;
		}
		break;
	default:
		break;
	}

	if (rc != HOST_API_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "Error sending response to host\n");
	}

	return rc;
}

int32_t receive(void *data, uint32_t num)
{
	int32_t rc = HOST_API_RC_ERROR;

	switch (interface_ctx.active) {
	case ACTIVE_INTERFACE_USB:
		if (host_api_usb_read(data, num) == 0) {
			rc = HOST_API_RC_OK;
		}
		break;
	default:
		break;
	}

	if (rc != HOST_API_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "Error receiving data\n");
	}

	return rc;
}

static bool is_header_empty(const uint8_t *buffer)
{
	if (buffer == NULL) {
		return true;
	}

	return (buffer[2] == 0U) && (buffer[3] == 0U);
}

static void copy_header(bool ack, uint8_t *output, const uint8_t *input)
{
	memcpy(output, input, 4U);
	if (ack) {
		memcpy(&output[4], &input[4], sizeof(uint32_t));
	}
}

static void fill_response(int type, uint8_t *output, uint16_t buffer_len)
{
	uint32_t data_length = (type == HOST_API_EVENT_NOTIFY) ? buffer_len : 0U;

	output[0] = HOST_API_SYNC_BYTE_1;
	output[1] = HOST_API_SYNC_BYTE_2;
	output[2] = (uint8_t)type;
	output[3] = 0x00;
	sys_put_le32(data_length, &output[4]);
}

static void print_info(int type, uint8_t *buffer, int data_len)
{
	switch (type) {
	case PRINT_HEADER:
		LOG_DEBUG(LOG_MOD_HOST_API,
			  "Header received = 0x%02x%02x 0x%02x%02x 0x%02x%02x 0x%02x%02x\n",
			  buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5],
			  buffer[6], buffer[7]);
		break;
	case PRINT_DATA:
		LOG_DEBUG(LOG_MOD_HOST_API, "Data received =");
		for (int i = (int)HEADER_LENGTH; i < ((int)HEADER_LENGTH + data_len); i++) {
			LOG_DEBUG(LOG_MOD_HOST_API, " 0x%02x", buffer[i]);
		}
		LOG_DEBUG(LOG_MOD_HOST_API, " END\n");
		break;
	case PRINT_ACK:
		LOG_DEBUG(LOG_MOD_HOST_API,
			  "Send ack: 0x%02x%02x 0x%02x%02x 0x%02x%02x 0x%02x%02x\n",
			  buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5],
			  buffer[6], buffer[7]);
		break;
	case PRINT_RESPONSE:
		LOG_DEBUG(LOG_MOD_HOST_API, "Send response len=%u\n",
			  (unsigned int)sys_get_le32(&buffer[4]));
		break;
	case PRINT_NACK:
		LOG_DEBUG(LOG_MOD_HOST_API,
			  "Send nack: 0x%02x%02x 0x%02x%02x 0x%02x%02x 0x%02x%02x\n",
			  buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5],
			  buffer[6], buffer[7]);
		break;
	default:
		break;
	}
}

static void clean_buffers(void)
{
	if (request_buf_size > 0U) {
		memset(request_buffer, 0, request_buf_size);
	}

	memset(response_buffer, 0, sizeof(response_buffer));
	memset(ack_buffer, 0, sizeof(ack_buffer));
}

static void read_pending_message(uint8_t *output)
{
	uint8_t *event_buffer;
	uint32_t data_length;

	if (output == NULL) {
		return;
	}

	if (event_manager.num_of_events == 0U) {
		LOG_INFO(LOG_MOD_HOST_API, "No pending messages for the host now.\n");
		fill_response(HOST_API_ERROR_READ_PENDING_MESSAGE, output, NO_PAYLOAD);
		return;
	}

	event_buffer = event_manager.events[event_manager.read_index].event_buffer;

	if (k_sem_take(&event_manager.event_sem, K_NO_WAIT) == 0) {
		event_manager.num_of_events--;
		k_sem_give(&event_manager.event_sem);
	} else {
		LOG_ERROR(LOG_MOD_HOST_API,
			  "read_pending_message() failed, semaphore was not acquired.\n");
		fill_response(HOST_API_ERROR_READ_PENDING_MESSAGE, output, NO_PAYLOAD);
		return;
	}

	event_manager.read_index = (uint8_t)((event_manager.read_index + 1U) % MAX_EVENTS);
	copy_header(false, event_buffer, header_buffer);
	data_length = sys_get_le32(&event_buffer[4]);
	memcpy(output, event_buffer, HEADER_LENGTH + data_length);
	memset(event_buffer, 0, MAX_EVENT_BUFFER_SIZE);
}

void host_api_set_active_interface(uint32_t interface_type)
{
	switch (interface_type) {
	case ACTIVE_INTERFACE_USB:
		if (host_api_usb_init() == HOST_API_RC_OK) {
			interface_ctx.active = ACTIVE_INTERFACE_USB;
		} else {
			LOG_ERROR(LOG_MOD_HOST_API, "Error changing interface to USB\n");
		}
		break;
	default:
		LOG_ERROR(LOG_MOD_HOST_API, "Unsupported interface ID %u\n",
			  (unsigned int)interface_type);
		break;
	}

	LOG_DEBUG(LOG_MOD_HOST_API, "Current active interface ID = %d\n", interface_ctx.active);
}

uint32_t h_api_get_active_interface(void)
{
	return (uint32_t)interface_ctx.active;
}

void host_api_send_pending_message(uint8_t *p_output)
{
	read_pending_message(p_output);
}

void host_api_toggle_crc_check(void)
{
	/* Kept as a protocol-compatibility stub for the system service. */
}

h_api_request_handler_t h_api_find_handler(uint8_t id, const h_api_handler_entry_t *list,
			      size_t list_len)
{
	for (size_t i = 0; i < list_len; i++) {
		if (list[i].id == id) {
			return list[i].handler;
		}
	}

	return NULL;
}

static void find_queue(uint8_t id, queue_entry_t *list, int list_len)
{
	for (int i = 0; i < list_len; i++) {
		if (list[i].id == id) {
			service_queue = list[i].queue;
			break;
		}
	}
}

static void parse_header(uint8_t *buffer, request_metadata_t *metadata)
{
	metadata->ack = (buffer[2] >> 7) & 0x1U;
	metadata->response = ((buffer[2] >> 6) & 0x1U) ? 0U : 1U;
	metadata->service_id = buffer[2] & 0x3FU;
	metadata->opcode = buffer[3];
	metadata->data_length = sys_get_le32(&buffer[4]);
}

static int32_t host_api_usb_init(void)
{
	static const struct device *const cdc_dev =
		DEVICE_DT_GET_OR_NULL(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_cdc_acm_uart));
	int ret;

	if (usb_ctx.initialized) {
		return HOST_API_RC_OK;
	}

	if (cdc_dev == NULL) {
		LOG_ERROR(LOG_MOD_HOST_API, "No CDC ACM device found for Host API\n");
		return HOST_API_RC_ERROR;
	}

	if (!device_is_ready(cdc_dev)) {
		LOG_ERROR(LOG_MOD_HOST_API, "CDC ACM device is not ready\n");
		return HOST_API_RC_ERROR;
	}

	usb_ctx.cdc_dev = cdc_dev;
	k_sem_init(&usb_ctx.tx_done_sem, 0, 1);
	k_sem_init(&usb_ctx.rx_ready_sem, 0, K_SEM_MAX_LIMIT);
	ring_buf_init(&usb_ctx.rx_ringbuf, sizeof(usb_ctx.rx_ring_storage), usb_ctx.rx_ring_storage);
	usb_ctx.line_signals_asserted = false;
	usb_ctx.dtr_was_high = false;
	usb_ctx.wait_online_logged = false;

	ret = uart_irq_callback_user_data_set(usb_ctx.cdc_dev, host_api_usb_irq, &usb_ctx);
	if (ret != 0) {
		LOG_ERROR(LOG_MOD_HOST_API, "Failed to set CDC ACM IRQ callback: %d\n", ret);
		return HOST_API_RC_ERROR;
	}

	uart_irq_tx_disable(usb_ctx.cdc_dev);
	uart_irq_rx_enable(usb_ctx.cdc_dev);
	usb_ctx.initialized = true;

	return HOST_API_RC_OK;
}

static int32_t host_api_init(void)
{
	memset(&event_manager, 0, sizeof(event_manager));

	k_sem_init(&event_manager.event_sem, 1, 1);
	k_msgq_init(&host_api_queue, host_api_queue_buffer, sizeof(int32_t), QUEUE_LENGTH);
	host_api_reset_delay_ms = 0U;
	host_api_reset_pending = false;

	interface_ctx.active = CONFIG_HOST_API_ACTIVE_INTERFACE;
	interface_ctx.send = send;
	interface_ctx.receive = receive;

	if (interface_ctx.active == ACTIVE_INTERFACE_USB) {
		if (host_api_usb_init() != HOST_API_RC_OK) {
			return HOST_API_RC_ERROR;
		}
	}

	LOG_DEBUG(LOG_MOD_HOST_API, "------------------------------------------\n");
	LOG_DEBUG(LOG_MOD_HOST_API, "       Host API Router task               \n");
	LOG_DEBUG(LOG_MOD_HOST_API, "------------------------------------------\n");
	if (interface_ctx.active != ACTIVE_INTERFACE_LAST) {
		LOG_INFO(LOG_MOD_HOST_API, "Active interface is %s\n",
			 interfaces[interface_ctx.active]);
	}

	LOG_INFO(LOG_MOD_HOST_API, "Zephyr SDK version is: %d.%d.%d\n",
		 CONFIG_HOST_API_SDK_VER_MAJOR, CONFIG_HOST_API_SDK_VER_MINOR,
		 CONFIG_HOST_API_SDK_VER_REVISION);
	LOG_INFO(LOG_MOD_HOST_API, "Host API version is: %d.%d.%d\n",
		 HOST_API_VERSION_MAJOR, HOST_API_VERSION_MINOR, HOST_API_VERSION_PATCH);

	return HOST_API_RC_OK;
}

static void handle_single_request(void)
{
	request_metadata_t metadata;
	h_api_request_handler_t service_cb;
	bool empty;
	int32_t rc;
	uint16_t allowed;
	uint32_t total;
	uint32_t response_length;

	parse_header(header_buffer, &metadata);
	empty = is_header_empty(header_buffer);
	if (empty) {
		LOG_DEBUG(LOG_MOD_HOST_API, "--- Header is invalid ---\n");
		print_info(PRINT_HEADER, header_buffer, NO_PAYLOAD);
		fill_response(HOST_API_ERROR_CRC, ack_buffer, NO_PAYLOAD);
		interface_ctx.send(ack_buffer, HEADER_LENGTH);
		print_info(PRINT_NACK, ack_buffer, NO_PAYLOAD);
		return;
	}

	LOG_DEBUG(LOG_MOD_HOST_API, "--- Header is valid! len %u ---\n",
		  (unsigned int)metadata.data_length);
	print_info(PRINT_HEADER, header_buffer, NO_PAYLOAD);

	allowed = get_allowed_req_size(metadata.service_id);
	if (metadata.data_length > allowed) {
		LOG_DEBUG(LOG_MOD_HOST_API,
			  "Data length too large for service %d: %u > %u\n",
			  metadata.service_id, (unsigned int)metadata.data_length,
			  (unsigned int)allowed);
		return;
	}

	total = HEADER_LENGTH + metadata.data_length;
	if (total > sizeof(request_buffer)) {
		LOG_ERROR(LOG_MOD_HOST_API, "Request buffer overflow: %u\n", (unsigned int)total);
		return;
	}

	request_buf_size = (uint16_t)total;
	memcpy(request_buffer, header_buffer, HEADER_LENGTH);

	if (metadata.ack) {
		copy_header(true, ack_buffer, request_buffer);
		interface_ctx.send(ack_buffer, HEADER_LENGTH);
		print_info(PRINT_ACK, ack_buffer, NO_PAYLOAD);
	}

	LOG_DEBUG(LOG_MOD_HOST_API, "Requested Service ID: %d and Opcode: %d\n",
		  metadata.service_id, metadata.opcode);

	copy_header(false, response_buffer, request_buffer);

	service_cb = h_api_find_handler(metadata.service_id, services, ARRAY_SIZE(services));
	if (service_cb != NULL) {
		if (metadata.data_length > 0U) {
			interface_ctx.receive(&request_buffer[HEADER_LENGTH], metadata.data_length);
			print_info(PRINT_DATA, request_buffer, (int)metadata.data_length);
		}

		service_queue = NULL;
		find_queue(metadata.service_id, service_queues, MAX_SERVICES);
		rc = service_cb(metadata.opcode, request_buffer, response_buffer);

		/*
		 * Built-in services run synchronously in this router. Only services
		 * registered with h_api_register_service() have a worker queue and must
		 * signal completion through h_api_response_ready(). UC manager is service
		 * 6 but is still built-in.
		 */
		if (service_queue != NULL) {
			(void)k_msgq_get(&host_api_queue, &rc, K_FOREVER);
		}

		if (rc < 0) {
			fill_response(HOST_API_ERROR_OPCODE, response_buffer, NO_PAYLOAD);
		}
	} else {
		fill_response(HOST_API_ERROR_SERVICE_ID, response_buffer, NO_PAYLOAD);
	}

	if (metadata.response) {
		response_length = sys_get_le32(&response_buffer[4]);
		interface_ctx.send(response_buffer, HEADER_LENGTH + response_length);
		print_info(PRINT_RESPONSE, response_buffer, NO_PAYLOAD);
	}

	host_api_execute_scheduled_reset();
}

static void host_api_run_loop(void)
{
	int32_t rc;

	rc = host_api_init();
	if (rc != HOST_API_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "Host API initialization failed, rc: %d\n", rc);
		host_api_ready = false;
		return;
	}

	host_api_ready = true;

	while (true) {
		if (interface_ctx.active == ACTIVE_INTERFACE_LAST) {
			k_msleep(10);
			continue;
		}

		clean_buffers();

		rc = interface_ctx.receive(header_buffer, ONE_BYTE);
		if ((rc != HOST_API_RC_OK) || (header_buffer[0] != HOST_API_SYNC_BYTE_1)) {
			continue;
		}

		rc = interface_ctx.receive(&header_buffer[1], ONE_BYTE);
		if ((rc != HOST_API_RC_OK) || (header_buffer[1] != HOST_API_SYNC_BYTE_2)) {
			continue;
		}

		rc = interface_ctx.receive(&header_buffer[2], HEADER_LENGTH_NO_SYNC);
		if (rc != HOST_API_RC_OK) {
			continue;
		}

		handle_single_request();
	}
}

static void host_api_thread_main(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	host_api_run_loop();
}

int host_api_start(void)
{
	if (host_api_started) {
		return 0;
	}

	host_api_ready = false;
	k_thread_create(&host_api_thread, host_api_stack,
			K_THREAD_STACK_SIZEOF(host_api_stack), host_api_thread_main,
			NULL, NULL, NULL, CONFIG_HOST_API_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&host_api_thread, "host_api");
	host_api_started = true;

	return 0;
}

bool host_api_is_ready(void)
{
	return host_api_ready;
}

static void host_api_trigger_reset_now(void)
{
	LOG_INFO(LOG_MOD_HOST_API, "Triggering SR100 software reset now\n");
	sys_clear_bit(SR100_AON_POR_MASK_EVENTS_REG, SR100_AON_POR_RST_EVENT_SW_RESET_BIT);
	sys_write32(1U, SR100_GLOBAL_BASE_ADDRESS + SR100_GLOBAL_RESET_TRIGGER_OFFSET);
	LOG_ERROR(LOG_MOD_HOST_API, "SR100 software reset trigger returned unexpectedly\n");
}

static void host_api_execute_scheduled_reset(void)
{
	uint32_t delay_ms;

	if (!host_api_reset_pending) {
		return;
	}

	delay_ms = host_api_reset_delay_ms;
	host_api_reset_pending = false;
	host_api_reset_delay_ms = 0U;

	if (delay_ms > 0U) {
		LOG_INFO(LOG_MOD_HOST_API,
			 "Executing scheduled software reset after %u ms\n",
			 (unsigned int)delay_ms);
		k_busy_wait(delay_ms * 1000U);
	}

	host_api_trigger_reset_now();
}

void host_api_schedule_reset(uint32_t delay_ms)
{
	host_api_reset_delay_ms = delay_ms;
	host_api_reset_pending = true;
	LOG_INFO(LOG_MOD_HOST_API, "Software reset scheduled in %u ms after response send\n",
		 (unsigned int)delay_ms);
}
