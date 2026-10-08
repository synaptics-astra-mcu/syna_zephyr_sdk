/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_HOST_API_H_
#define ZEPHYR_UTILITIES_HOST_API_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

typedef enum {
	ACTIVE_INTERFACE_UART = 0,
	ACTIVE_INTERFACE_USB = 1,
	ACTIVE_INTERFACE_I2C = 2,
	ACTIVE_INTERFACE_SPI = 3,
	ACTIVE_INTERFACE_LAST = 4,
} en_host_api_interface;

#define MAX_REQUEST_BUFFER_SIZE 8192
#define MIN_REQUEST_BUFFER_SIZE 1000
#define MAX_RESPONSE_BUFFER_SIZE 272

#define EXTERNAL_SERVICE_REGISTRATION 0

#define HOST_API_SERVICE_ID_SYSTEM 0x1
#define HOST_API_SERVICE_ID_LOGGER 0x2
#define HOST_API_SERVICE_ID_FLASH_AUX 0x3
#define HOST_API_SERVICE_ID_TIMER 0x4
#define HOST_API_SERVICE_ID_WATCHDOG 0x5
#define HOST_API_SERVICE_ID_UC_MANAGER 0x6
#define HOST_API_SERVICE_ID_PINMUX 0x7
#define HOST_API_SERVICE_ID_GPIO 0x8
#define HOST_API_SERVICE_ID_I2S 0x9
#define HOST_API_SERVICE_ID_IMG_SENSOR 0xA
#define HOST_API_SERVICE_ID_FW_UPDATE 0xB
#define HOST_API_SERVICE_ID_USB_BOOT 0xD

#define HOST_API_ERROR_CRC 0x1
#define HOST_API_ERROR_SERVICE_ID 0x2
#define HOST_API_ERROR_OPCODE 0x3
#define HOST_API_ERROR_READ_PENDING_MESSAGE 0x4
#define HOST_API_ERROR_FIFO_OVERFLOW 0x5

#define HOST_API_EVENT_NOTIFY 0xA

#define HOST_API_RC_OK 0
#define HOST_API_RC_ERROR -1
#define HOST_API_RC_ALREADY_REGISTERED -1
#define HOST_API_RC_TOO_MANY_SERVICES -2
#define HOST_API_RC_TIMEOUT -1
#define HOST_API_RC_NULL_POINTER -1
#define HOST_API_RC_BUFFER_LEN_0 -2
#define HOST_API_RC_GPIO_FAILURE -3
#define HOST_API_RC_TOO_MANY_EVENTS -4
#define HOST_API_RC_DATA_TOO_LONG -5
#define HOST_API_RC_SEMAPHORE_TIMEOUT -6

int host_api_start(void);
bool host_api_is_ready(void);

int32_t h_api_register_service(struct k_msgq *queue, uint32_t service_id);
int32_t h_api_response_ready(int32_t *rc);
int32_t h_api_event_notify(uint8_t *data_buffer, uint16_t buffer_len);
uint32_t h_api_get_active_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_HOST_API_H_ */
