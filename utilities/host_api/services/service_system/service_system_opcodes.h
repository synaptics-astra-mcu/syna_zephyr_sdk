/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_HOST_API_SERVICE_SYSTEM_OPCODES_H_
#define ZEPHYR_UTILITIES_HOST_API_SERVICE_SYSTEM_OPCODES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SERVICE_SYSTEM_RC_OK 0

#define OPCODE_TOGGLE_CRC_CHECK 0x0
#define OPCODE_HOST_API_VERSION 0x1
#define OPCODE_READ_REGISTER 0x2
#define OPCODE_WRITE_REGISTER 0x3
#define OPCODE_READ_PENDING_MESSAGE 0x4
#define OPCODE_CONFIG_ACTIVE_INTERFACE 0x5
#define OPCODE_GET_LOADED_APPS 0x6
#define OPCODE_INITIATE_SW_RESET 0x7
#define OPCODE_WRITE_I2C_REGISTER 0x8
#define OPCODE_READ_I2C_REGISTER 0x9

int system_get_host_api_version(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int system_read_register(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int system_write_register(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int system_get_loaded_apps(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int system_toggle_crc(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int system_read_pending_message(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int system_config_active_interface(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int system_initiate_sw_reset(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int system_read_i2c_register(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int system_write_i2c_register(uint8_t id, uint8_t *p_input, uint8_t *p_output);

void host_api_loaded_apps_report(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_HOST_API_SERVICE_SYSTEM_OPCODES_H_ */
