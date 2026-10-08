/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "service_fw_update.h"

#include <stdbool.h>

#include <zephyr/sys/util.h>

#include <logger.h>

#include "fw_update_internal.h"
#include "host_api.h"

#ifndef LOG_MOD_GENERIC
#define LOG_MOD_GENERIC "GENERIC"
#endif

#define FW_UPDATE_MAX_QUEUE_MESSAGES 10

static const h_api_handler_entry_t opcode_table[] = {
	{ FW_UPDATE_OPCODE_START, service_fw_update_opcode_start },
	{ FW_UPDATE_OPCODE_WRITE, service_fw_update_opcode_write },
	{ FW_UPDATE_OPCODE_FINISH, service_fw_update_opcode_finish },
	{ FW_UPDATE_OPCODE_INSTALL, service_fw_update_opcode_install },
	{ FW_UPDATE_OPCODE_CANCEL, service_fw_update_opcode_cancel },
	{ FW_UPDATE_OPCODE_REBOOT, service_fw_update_opcode_reboot },
	{ FW_UPDATE_OPCODE_ACCEPT, service_fw_update_opcode_accept },
	{ FW_UPDATE_OPCODE_REJECT, service_fw_update_opcode_reject },
	{ FW_UPDATE_OPCODE_CLEAN, service_fw_update_opcode_clean },
	{ FW_UPDATE_OPCODE_GET_INFO, service_fw_update_opcode_get_info },
	{ FW_UPDATE_OPCODE_GET_STATE, service_fw_update_opcode_get_state },
	{ FW_UPDATE_OPCODE_GET_COMPONENT_STATE, service_fw_update_opcode_get_component_state },
	{ FW_UPDATE_OPCODE_GET_FAILURE, service_fw_update_opcode_get_failure },
	{ FW_UPDATE_OPCODE_GET_COMPONENT_FAILURE, service_fw_update_opcode_get_component_failure },
};

K_MSGQ_DEFINE(service_fw_update_queue, sizeof(h_api_message_t), FW_UPDATE_MAX_QUEUE_MESSAGES, 4);
K_THREAD_STACK_DEFINE(service_fw_update_stack, CONFIG_SERVICE_FW_UPDATE_STACK_SIZE);

static struct k_thread service_fw_update_thread;
static bool service_fw_update_started;
static volatile bool service_fw_update_ready;

static void service_fw_update_thread_main(void *arg1, void *arg2, void *arg3)
{
	h_api_message_t msg;
	h_api_request_handler_t opcode_cb;
	int32_t rc = HOST_API_RC_OK;
	int service_slot;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	(void)fw_update_post();

	service_slot = h_api_register_service(&service_fw_update_queue, HOST_API_SERVICE_ID_FW_UPDATE);
	if (service_slot < 0) {
		LOG_ERROR(LOG_MOD_GENERIC,
			  "FW UPDATE service Host API registration failed, rc = %d.\n", service_slot);
		return;
	}

	service_fw_update_ready = true;

	LOG_DEBUG(LOG_MOD_GENERIC, "FW UPDATE TASK (srv_id = %d)\n", service_slot);

	while (true) {
		if (k_msgq_get(&service_fw_update_queue, &msg, K_FOREVER) == 0) {
			opcode_cb = h_api_find_handler(msg.opcode_id, opcode_table,
						      ARRAY_SIZE(opcode_table));
			if (opcode_cb != NULL) {
				rc = opcode_cb(msg.opcode_id, msg.p_input, msg.p_output);
			} else {
				rc = -1;
			}

			(void)h_api_response_ready(&rc);
		}
	}
}

void service_fw_update_task_create(void)
{
	if (service_fw_update_started) {
		return;
	}

	service_fw_update_ready = false;

	k_thread_create(&service_fw_update_thread, service_fw_update_stack,
			K_THREAD_STACK_SIZEOF(service_fw_update_stack),
			service_fw_update_thread_main, NULL, NULL, NULL,
			CONFIG_SERVICE_FW_UPDATE_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&service_fw_update_thread, "service_fw_update");
	service_fw_update_started = true;
}

bool service_fw_update_is_ready(void)
{
	return service_fw_update_ready;
}
