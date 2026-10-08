/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "service_system.h"

#include <zephyr/sys/util.h>

static const h_api_handler_entry_t opcode_table[] = {
	{ OPCODE_HOST_API_VERSION, system_get_host_api_version },
	{ OPCODE_READ_REGISTER, system_read_register },
	{ OPCODE_WRITE_REGISTER, system_write_register },
	{ OPCODE_GET_LOADED_APPS, system_get_loaded_apps },
	{ OPCODE_TOGGLE_CRC_CHECK, system_toggle_crc },
	{ OPCODE_READ_PENDING_MESSAGE, system_read_pending_message },
	{ OPCODE_CONFIG_ACTIVE_INTERFACE, system_config_active_interface },
	{ OPCODE_INITIATE_SW_RESET, system_initiate_sw_reset },
#if defined(CONFIG_I2C)
	{ OPCODE_WRITE_I2C_REGISTER, system_write_i2c_register },
	{ OPCODE_READ_I2C_REGISTER, system_read_i2c_register },
#endif
};

int service_system_handler(uint8_t opcode_id, uint8_t *p_input, uint8_t *p_output)
{
	h_api_request_handler_t opcode;

	opcode = h_api_find_handler(opcode_id, opcode_table, ARRAY_SIZE(opcode_table));
	if (opcode != NULL) {
		return opcode(opcode_id, p_input, p_output);
	}

	return -1;
}
