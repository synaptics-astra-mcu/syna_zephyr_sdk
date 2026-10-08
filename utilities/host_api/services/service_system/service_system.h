/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_HOST_API_SERVICE_SYSTEM_H_
#define ZEPHYR_UTILITIES_HOST_API_SERVICE_SYSTEM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "host_api_utils.h"
#include "service_system_opcodes.h"

int service_system_handler(uint8_t opcode_id, uint8_t *p_input, uint8_t *p_output);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_HOST_API_SERVICE_SYSTEM_H_ */
