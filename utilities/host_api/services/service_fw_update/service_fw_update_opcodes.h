/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_HOST_API_SERVICE_FW_UPDATE_OPCODES_H_
#define ZEPHYR_UTILITIES_HOST_API_SERVICE_FW_UPDATE_OPCODES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define FW_UPDATE_RC_SIZE_IN_BYTES 4U

#define FW_UPDATE_OPCODE_START 0x01
#define FW_UPDATE_OPCODE_WRITE 0x02
#define FW_UPDATE_OPCODE_FINISH 0x03
#define FW_UPDATE_OPCODE_INSTALL 0x04
#define FW_UPDATE_OPCODE_CANCEL 0x05
#define FW_UPDATE_OPCODE_REBOOT 0x06
#define FW_UPDATE_OPCODE_ACCEPT 0x07
#define FW_UPDATE_OPCODE_REJECT 0x08
#define FW_UPDATE_OPCODE_CLEAN 0x09
#define FW_UPDATE_OPCODE_GET_INFO 0x0A
#define FW_UPDATE_OPCODE_GET_STATE 0x0B
#define FW_UPDATE_OPCODE_GET_COMPONENT_STATE 0x0C
#define FW_UPDATE_OPCODE_GET_FAILURE 0x0D
#define FW_UPDATE_OPCODE_GET_COMPONENT_FAILURE 0x0E

int service_fw_update_opcode_start(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_write(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_finish(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_install(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_cancel(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_reboot(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_accept(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_reject(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_clean(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_get_info(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_get_state(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_get_component_state(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_get_failure(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_fw_update_opcode_get_component_failure(uint8_t id, uint8_t *p_input, uint8_t *p_output);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_HOST_API_SERVICE_FW_UPDATE_OPCODES_H_ */
