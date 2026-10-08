/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_HOST_API_UTILS_H_
#define ZEPHYR_UTILITIES_HOST_API_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define HOST_API_NEW_FORMAT 1

#define HOST_API_SYNC_BYTE_1 0x5B
#define HOST_API_SYNC_BYTE_2 0x5A

typedef int (*h_api_request_handler_t)(uint8_t id, uint8_t *p_input, uint8_t *p_output);

typedef struct handler_entry {
	uint16_t id;
	h_api_request_handler_t handler;
} h_api_handler_entry_t;

typedef struct header {
	uint8_t sync_byte1;
	uint8_t sync_byte2;
	uint8_t flags_service;
	uint8_t opcode;
	uint32_t data_length;
} h_api_header_t;

typedef struct h_api_message {
	uint8_t opcode_id;
	uint8_t *p_input;
	uint8_t *p_output;
} h_api_message_t;

static inline uint8_t h_api_get_service_id(const h_api_header_t *header)
{
	return header->flags_service & 0x3FU;
}

static inline uint8_t h_api_get_no_response(const h_api_header_t *header)
{
	return (header->flags_service >> 6) & 0x1U;
}

static inline uint8_t h_api_get_ack(const h_api_header_t *header)
{
	return (header->flags_service >> 7) & 0x1U;
}

static inline uint8_t h_api_build_flags(uint8_t service_id, uint8_t no_response, uint8_t ack)
{
	return (service_id & 0x3FU) | ((no_response & 0x1U) << 6) | ((ack & 0x1U) << 7);
}

h_api_request_handler_t h_api_find_handler(uint8_t id, const h_api_handler_entry_t *list,
					      size_t list_len);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_HOST_API_UTILS_H_ */
