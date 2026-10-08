/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_HOST_API_INTERNAL_H_
#define ZEPHYR_UTILITIES_HOST_API_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define HOST_API_VERSION_MAJOR 0x4
#define HOST_API_VERSION_MINOR 0x0
#define HOST_API_VERSION_PATCH 0x0

void host_api_set_active_interface(uint32_t interface_type);
void host_api_send_pending_message(uint8_t *p_output);
void host_api_toggle_crc_check(void);
void host_api_schedule_reset(uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_HOST_API_INTERNAL_H_ */
