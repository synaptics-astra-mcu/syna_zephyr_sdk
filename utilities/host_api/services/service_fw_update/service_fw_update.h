/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_HOST_API_SERVICE_FW_UPDATE_H_
#define ZEPHYR_UTILITIES_HOST_API_SERVICE_FW_UPDATE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "host_api_utils.h"
#include "service_fw_update_opcodes.h"

void service_fw_update_task_create(void);
bool service_fw_update_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_HOST_API_SERVICE_FW_UPDATE_H_ */
