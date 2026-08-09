/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_SERVICE_FLASH_AUX_H_
#define ZEPHYR_UTILITIES_SERVICE_FLASH_AUX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

int service_flash_aux_handler(uint8_t opcode_id, uint8_t *p_input, uint8_t *p_output);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_SERVICE_FLASH_AUX_H_ */
