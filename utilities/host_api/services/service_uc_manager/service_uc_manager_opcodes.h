/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_HOST_API_SERVICE_UC_MANAGER_OPCODES_H_
#define ZEPHYR_UTILITIES_HOST_API_SERVICE_UC_MANAGER_OPCODES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define OPCODE_CREATE_USECASE 0x1
#define OPCODE_START_USECASE 0x2
#define OPCODE_STOP_USECASE 0x3
#define OPCODE_RESUME_USECASE 0x4
#define OPCODE_KILL_USECASE 0x5

int uc_manager_create_usecase(uint8_t id, uint8_t *input_data, uint8_t *output);
int uc_manager_start_usecase(uint8_t id, uint8_t *input_data, uint8_t *output);
int uc_manager_stop_usecase(uint8_t id, uint8_t *input_data, uint8_t *output);
int uc_manager_resume_usecase(uint8_t id, uint8_t *input_data, uint8_t *output);
int uc_manager_kill_usecase(uint8_t id, uint8_t *input_data, uint8_t *output);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_HOST_API_SERVICE_UC_MANAGER_OPCODES_H_ */
