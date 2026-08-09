/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "service_flash_aux.h"

#include <zephyr/sys/util.h>

#include "host_api.h"
#include "host_api_utils.h"
#include "service_flash_aux_opcodes.h"

static const h_api_handler_entry_t opcode_table[] = {
	{ FLASH_AUX_OPCODE_START, service_flash_aux_start },
	{ FLASH_AUX_OPCODE_ERASE_BLOCK, service_flash_aux_erase_block },
	{ FLASH_AUX_OPCODE_ERASE_SECTION, service_flash_aux_erase_sector },
	{ FLASH_AUX_OPCODE_WRITE, service_flash_aux_write },
	{ FLASH_AUX_OPCODE_SWITCH_IMAGE, service_flash_aux_switch_image },
	{ FLASH_AUX_OPCODE_READ_PAGE, service_flash_aux_read_page },
	{ FLASH_AUX_OPCODE_READ_FLASH_ID, service_flash_aux_read_flash_id },
	{ FLASH_AUX_OPCODE_INIT_WITHOUT_ATTR, service_flash_aux_init_without_attr },
	{ FLASH_AUX_OPCODE_COMPLETED_SUCCESSFULLY, service_flash_aux_completed_successfully },
};

int service_flash_aux_handler(uint8_t opcode_id, uint8_t *p_input, uint8_t *p_output)
{
	h_api_request_handler_t opcode;

	opcode = h_api_find_handler(opcode_id, opcode_table, ARRAY_SIZE(opcode_table));
	if (opcode != NULL) {
		return opcode(opcode_id, p_input, p_output);
	}

	return -1;
}
