/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "service_flash_aux_opcodes.h"

#include <string.h>

#include <zephyr/sys/byteorder.h>

#include <logger.h>

#include "flash_aux.h"
#include "host_api.h"
#include "host_api_internal.h"
#include "host_api_utils.h"

#ifndef LOG_MOD_HOST_API
#define LOG_MOD_HOST_API "HOST_API"
#endif

#define HOST_API_RC_DATA_ERROR -2
#define FLASH_AUX_RESET_DELAY_MS 100U
#define CRC_SIZE 4U
#define OFFSET_SIZE 4U

static void put_rc_response(uint8_t *p_output, int32_t rc)
{
	sys_put_le32(FLASH_AUX_RC_SIZE, &p_output[4]);
	sys_put_le32((uint32_t)rc, &p_output[sizeof(h_api_header_t)]);
}

static uint32_t get_u32_arg(const uint8_t *p_input)
{
	return sys_get_le32(&p_input[sizeof(h_api_header_t)]);
}

int service_flash_aux_start(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc;

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	LOG_INFO(LOG_MOD_HOST_API, "Fwupdate starts\n");
	rc = flash_aux_start();
	put_rc_response(p_output, rc);
	if (rc != FLASH_AUX_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "FLASH_AUX START ERROR! rc=%ld\n", (long)rc);
	}

	return HOST_API_RC_OK;
}

int service_flash_aux_erase_block(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc;

	ARG_UNUSED(id);

	rc = flash_aux_erase(FLASH_AUX_ERASE_BLOCK, get_u32_arg(p_input));
	put_rc_response(p_output, rc);
	if (rc != FLASH_AUX_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "FLASH_AUX ERASE_BLOCK ERROR! rc=%ld\n", (long)rc);
	}

	return HOST_API_RC_OK;
}

int service_flash_aux_erase_sector(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc;

	ARG_UNUSED(id);

	rc = flash_aux_erase(FLASH_AUX_ERASE_SECTOR, get_u32_arg(p_input));
	put_rc_response(p_output, rc);
	if (rc != FLASH_AUX_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "FLASH_AUX ERASE_SECTION ERROR! rc=%ld\n", (long)rc);
	}

	return HOST_API_RC_OK;
}

int service_flash_aux_write(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t payload_len;
	uint32_t offset;
	uint32_t crc;
	uint8_t *data;
	int32_t rc;

	ARG_UNUSED(id);

	payload_len = sys_get_le32(&p_input[4]);
	offset = sys_get_le32(&p_input[sizeof(h_api_header_t)]);
	crc = sys_get_le32(&p_input[sizeof(h_api_header_t) + OFFSET_SIZE]);
	data = &p_input[sizeof(h_api_header_t) + OFFSET_SIZE + CRC_SIZE];

	rc = flash_aux_write(offset, (uint16_t)(payload_len - OFFSET_SIZE - CRC_SIZE), data, crc);
	put_rc_response(p_output, rc);
	if (rc != FLASH_AUX_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "FLASH_AUX WRITE ERROR! rc=%ld\n", (long)rc);
	}

	return HOST_API_RC_OK;
}

int service_flash_aux_switch_image(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc;

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	rc = flash_aux_reboot();
	put_rc_response(p_output, rc);
	if (rc != FLASH_AUX_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "FLASH_AUX SWITCH_IMAGE ERROR! rc=%ld\n", (long)rc);
	} else {
		host_api_schedule_reset(FLASH_AUX_RESET_DELAY_MS);
	}

	return HOST_API_RC_OK;
}

int service_flash_aux_read_page(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint8_t data_arr[FLASH_AUX_PAGE_SIZE];
	int32_t rc;

	ARG_UNUSED(id);

	memset(data_arr, 0, sizeof(data_arr));
	rc = flash_aux_read_page(get_u32_arg(p_input), FLASH_AUX_PAGE_SIZE, data_arr);
	if (rc != FLASH_AUX_RC_OK) {
		put_rc_response(p_output, rc);
		LOG_ERROR(LOG_MOD_HOST_API, "FLASH_AUX READ_PAGE ERROR! rc=%ld\n", (long)rc);
		return HOST_API_RC_OK;
	}

	sys_put_le32(FLASH_AUX_PAGE_SIZE, &p_output[4]);
	memcpy(&p_output[sizeof(h_api_header_t)], data_arr, FLASH_AUX_PAGE_SIZE);
	return HOST_API_RC_OK;
}

int service_flash_aux_read_flash_id(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t flash_id = 0U;
	int32_t rc;

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	rc = flash_aux_read_flash_id(&flash_id);
	sys_put_le32(FLASH_AUX_RC_SIZE, &p_output[4]);
	if (rc != FLASH_AUX_RC_OK) {
		LOG_ERROR(LOG_MOD_HOST_API, "FLASH_AUX READ_FLASH_ID ERROR! rc=%ld\n", (long)rc);
		memset(&p_output[sizeof(h_api_header_t)], 0, FLASH_AUX_RC_SIZE);
		return HOST_API_RC_OK;
	}

	p_output[sizeof(h_api_header_t)] = (uint8_t)((flash_id >> 24) & 0xFFU);
	p_output[sizeof(h_api_header_t) + 1U] = (uint8_t)((flash_id >> 16) & 0xFFU);
	p_output[sizeof(h_api_header_t) + 2U] = (uint8_t)((flash_id >> 8) & 0xFFU);
	p_output[sizeof(h_api_header_t) + 3U] = 0U;
	return HOST_API_RC_OK;
}

int service_flash_aux_init_without_attr(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc;

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	rc = flash_aux_init_without_attributes();
	put_rc_response(p_output, rc);
	return HOST_API_RC_OK;
}

int service_flash_aux_completed_successfully(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	int32_t rc;

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	rc = flash_aux_completed_successfully();
	put_rc_response(p_output, rc);
	return HOST_API_RC_OK;
}
