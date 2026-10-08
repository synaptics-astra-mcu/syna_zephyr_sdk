/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_SERVICE_FLASH_AUX_OPCODES_H_
#define ZEPHYR_UTILITIES_SERVICE_FLASH_AUX_OPCODES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define FLASH_AUX_RC_SIZE 4U

#define FLASH_AUX_OPCODE_START 0x01
#define FLASH_AUX_OPCODE_ERASE_BLOCK 0x02
#define FLASH_AUX_OPCODE_ERASE_SECTION 0x03
#define FLASH_AUX_OPCODE_WRITE 0x04
#define FLASH_AUX_OPCODE_SWITCH_IMAGE 0x05
#define FLASH_AUX_OPCODE_READ_PAGE 0x06
#define FLASH_AUX_OPCODE_READ_FLASH_ID 0x07
#define FLASH_AUX_OPCODE_INIT_WITHOUT_ATTR 0x08
#define FLASH_AUX_OPCODE_COMPLETED_SUCCESSFULLY 0x09

int service_flash_aux_start(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_flash_aux_erase_block(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_flash_aux_erase_sector(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_flash_aux_write(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_flash_aux_switch_image(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_flash_aux_read_page(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_flash_aux_read_flash_id(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_flash_aux_init_without_attr(uint8_t id, uint8_t *p_input, uint8_t *p_output);
int service_flash_aux_completed_successfully(uint8_t id, uint8_t *p_input, uint8_t *p_output);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_SERVICE_FLASH_AUX_OPCODES_H_ */
