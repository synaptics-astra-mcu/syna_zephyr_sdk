/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "service_fw_update.h"

#include <string.h>

#include <zephyr/sys/byteorder.h>

#include <logger.h>

#include "fw_update.h"
#include "host_api.h"
#include "host_api_internal.h"
#include "host_api_utils.h"

#ifndef LOG_MOD_HOST_API
#define LOG_MOD_HOST_API "HOST_API"
#endif

#define RESET_DELAY_MS 100U

static void put_rc_response(uint8_t *p_output, int32_t rc)
{
	sys_put_le32(FW_UPDATE_RC_SIZE_IN_BYTES, &p_output[4]);
	sys_put_le32((uint32_t)rc, &p_output[sizeof(h_api_header_t)]);
}

static uint32_t get_u32_arg(const uint8_t *p_input)
{
	return sys_get_le32(&p_input[sizeof(h_api_header_t)]);
}

int service_fw_update_opcode_start(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc = fw_update_start(get_u32_arg(p_input));

	ARG_UNUSED(id);
	put_rc_response(p_output, rc);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "START ERROR! rc=%ld\n", (long)rc);
	}

	return HOST_API_RC_OK;
}

int service_fw_update_opcode_write(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t image_id = get_u32_arg(p_input);
	uint32_t num_bytes = sys_get_le32(&p_input[4]) - sizeof(uint32_t);
	uint8_t *data = &p_input[sizeof(h_api_header_t) + sizeof(uint32_t)];
	int32_t rc = fw_update_write(image_id, num_bytes, data);

	ARG_UNUSED(id);
	put_rc_response(p_output, rc);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "WRITE ERROR! rc=%ld\n", (long)rc);
	}

	return HOST_API_RC_OK;
}

int service_fw_update_opcode_finish(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc = fw_update_finish(get_u32_arg(p_input));

	ARG_UNUSED(id);
	put_rc_response(p_output, rc);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "FINISH ERROR! rc=%ld\n", (long)rc);
	}

	return HOST_API_RC_OK;
}

int service_fw_update_opcode_install(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t auto_reset = get_u32_arg(p_input);
	int32_t rc = fw_update_install(auto_reset);

	ARG_UNUSED(id);
	put_rc_response(p_output, rc);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "INSTALL ERROR! rc=%ld\n", (long)rc);
	} else if (auto_reset != 0U) {
		host_api_schedule_reset(RESET_DELAY_MS);
	}

	return HOST_API_RC_OK;
}

int service_fw_update_opcode_cancel(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc = fw_update_cancel(get_u32_arg(p_input));

	ARG_UNUSED(id);
	put_rc_response(p_output, rc);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "CANCEL ERROR! rc=%ld\n", (long)rc);
	}

	return HOST_API_RC_OK;
}

int service_fw_update_opcode_reboot(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc;

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	rc = fw_update_reboot();
	put_rc_response(p_output, rc);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "REBOOT ERROR! rc=%ld\n", (long)rc);
	} else {
		host_api_schedule_reset(RESET_DELAY_MS);
	}

	return HOST_API_RC_OK;
}

int service_fw_update_opcode_accept(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc;

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	rc = fw_update_accept();
	put_rc_response(p_output, rc);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "ACCEPT ERROR! rc=%ld\n", (long)rc);
	}

	return HOST_API_RC_OK;
}

int service_fw_update_opcode_reject(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t auto_reset = get_u32_arg(p_input);
	int32_t rc = fw_update_reject(auto_reset);

	ARG_UNUSED(id);
	put_rc_response(p_output, rc);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "REJECT ERROR! rc=%ld\n", (long)rc);
	} else if (auto_reset != 0U) {
		host_api_schedule_reset(RESET_DELAY_MS);
	}

	return HOST_API_RC_OK;
}

int service_fw_update_opcode_clean(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc = fw_update_clean(get_u32_arg(p_input));

	ARG_UNUSED(id);
	put_rc_response(p_output, rc);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "CLEAN ERROR! rc=%ld\n", (long)rc);
	}

	return HOST_API_RC_OK;
}

int service_fw_update_opcode_get_info(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	st_fw_update_info st_info = { 0 };
	int32_t rc;

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	sys_put_le32(sizeof(st_fw_update_info), &p_output[4]);
	rc = fw_update_get_info(&st_info);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "GET INFO ERROR! rc=%ld\n", (long)rc);
	}

	memcpy(&p_output[sizeof(h_api_header_t)], &st_info, sizeof(st_info));
	return HOST_API_RC_OK;
}

int service_fw_update_opcode_get_state(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t state = 0U;
	int32_t rc;

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	sys_put_le32(FW_UPDATE_RC_SIZE_IN_BYTES, &p_output[4]);
	rc = fw_update_get_state(&state);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "GET STATE ERROR! rc=%ld\n", (long)rc);
	}

	sys_put_le32(state, &p_output[sizeof(h_api_header_t)]);
	return HOST_API_RC_OK;
}

int service_fw_update_opcode_get_component_state(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t state = 0U;
	int32_t rc = fw_update_get_component_state(get_u32_arg(p_input), &state);

	ARG_UNUSED(id);
	sys_put_le32(FW_UPDATE_RC_SIZE_IN_BYTES, &p_output[4]);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "GET COMPONENT STATE ERROR! rc=%ld\n", (long)rc);
	}

	sys_put_le32(state, &p_output[sizeof(h_api_header_t)]);
	return HOST_API_RC_OK;
}

int service_fw_update_opcode_get_failure(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t failure = 0U;
	int32_t rc;

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	sys_put_le32(FW_UPDATE_RC_SIZE_IN_BYTES, &p_output[4]);
	rc = fw_update_get_failure(&failure);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "GET FAILURE ERROR! rc=%ld\n", (long)rc);
	}

	sys_put_le32(failure, &p_output[sizeof(h_api_header_t)]);
	return HOST_API_RC_OK;
}

int service_fw_update_opcode_get_component_failure(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t failure = 0U;
	int32_t rc = fw_update_get_component_failure(get_u32_arg(p_input), &failure);

	ARG_UNUSED(id);
	sys_put_le32(FW_UPDATE_RC_SIZE_IN_BYTES, &p_output[4]);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "GET COMPONENT FAILURE ERROR! rc=%ld\n", (long)rc);
	}

	sys_put_le32(failure, &p_output[sizeof(h_api_header_t)]);
	return HOST_API_RC_OK;
}
