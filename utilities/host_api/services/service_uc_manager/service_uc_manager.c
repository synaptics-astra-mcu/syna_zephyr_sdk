/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "service_uc_manager.h"

#include <zephyr/sys/util.h>

static h_api_handler_entry_t commands[] = {
	{ OPCODE_CREATE_USECASE, uc_manager_create_usecase },
	{ OPCODE_START_USECASE, uc_manager_start_usecase },
	{ OPCODE_STOP_USECASE, uc_manager_stop_usecase },
	{ OPCODE_RESUME_USECASE, uc_manager_resume_usecase },
	{ OPCODE_KILL_USECASE, uc_manager_kill_usecase },
};

int service_uc_manager_handler(uint8_t opcode_id, uint8_t *p_input, uint8_t *p_output)
{
	h_api_request_handler_t opcode;

	opcode = h_api_find_handler(opcode_id, commands, ARRAY_SIZE(commands));
	if (opcode != NULL) {
		return opcode(opcode_id, p_input, p_output);
	}

	return UC_MANAGER_RC_ERROR;
}
