/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flash_aux.h"

#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/sys/crc.h>

#include <logger.h>

#ifndef LOG_MOD_FLASH_AUX
#define LOG_MOD_FLASH_AUX "FLASH_AUX"
#endif

#if !DT_HAS_CHOSEN(zephyr_flash_controller)
#error "zephyr,flash-controller chosen node is required for flash_aux support"
#endif

#define FLASH_AUX_FLASH_NODE DT_CHOSEN(zephyr_flash_controller)
#define FLASH_AUX_FLASH_DEV DEVICE_DT_GET(FLASH_AUX_FLASH_NODE)
#define FLASH_AUX_JEDEC_ID_LEN 3U
#define FLASH_AUX_SR100_FALLBACK_JEDEC_MANUF 0xC8U
#define FLASH_AUX_SR100_FALLBACK_JEDEC_TYPE 0x60U
#define FLASH_AUX_SR100_FALLBACK_JEDEC_DENSITY 0x18U

static bool s_flash_aux_started;

static void flash_aux_normalize_jedec_id(uint8_t *jedec_id)
{
	if (jedec_id == NULL) {
		return;
	}

	/*
	 * SR100 uses a fixed external flash part. If the low-level RDID path
	 * returns only the manufacturer byte, restore the board-known
	 * type/density bytes so host tooling can resolve the flash correctly.
	 */
	if ((jedec_id[0] == FLASH_AUX_SR100_FALLBACK_JEDEC_MANUF) &&
	    (jedec_id[1] == 0U) && (jedec_id[2] == 0U)) {
		jedec_id[1] = FLASH_AUX_SR100_FALLBACK_JEDEC_TYPE;
		jedec_id[2] = FLASH_AUX_SR100_FALLBACK_JEDEC_DENSITY;
	}
}

static int flash_aux_get_flash_device(const struct device **flash_dev)
{
	if (flash_dev == NULL) {
		return -EINVAL;
	}

	*flash_dev = FLASH_AUX_FLASH_DEV;
	if (!device_is_ready(*flash_dev)) {
		LOG_ERROR(LOG_MOD_FLASH_AUX, "Flash controller is not ready\n");
		return -ENODEV;
	}

	return 0;
}

int32_t flash_aux_start(void)
{
	const struct device *flash_dev;
	uint32_t rc;
	int ret;

	ret = flash_aux_get_flash_device(&flash_dev);
	if (ret != 0) {
		ARG_UNUSED(flash_dev);
		return FLASH_AUX_RC_XSPI_NOT_INITIALIZED;
	}

	rc = flash_aux_stop_all_activities();
	if (rc != FLASH_AUX_RC_OK) {
		LOG_ERROR(LOG_MOD_FLASH_AUX, "Stop all activities failed: %u\n", rc);
		return (int32_t)rc;
	}

	s_flash_aux_started = true;
	return FLASH_AUX_RC_OK;
}

int32_t flash_aux_erase(uint8_t type, uint32_t offset)
{
	const struct device *flash_dev;
	int ret;

	if (!s_flash_aux_started) {
		return FLASH_AUX_RC_CALLED_WITHOUT_START_CMD;
	}

	if (type == FLASH_AUX_ERASE_BLOCK) {
		return FLASH_AUX_RC_NOT_SUPPORTED;
	}

	ret = flash_aux_get_flash_device(&flash_dev);
	if (ret != 0) {
		return FLASH_AUX_RC_XSPI_NOT_INITIALIZED;
	}

	ret = flash_erase(flash_dev, (off_t)(FLASH_AUX_FW_LOCATION + offset), SECTOR_SIZE_IN_BYTES);
	if (ret != 0) {
		LOG_ERROR(LOG_MOD_FLASH_AUX, "Erase failed off=0x%08x ret=%d\n", offset, ret);
		return FLASH_AUX_RC_ERASE_SECTOR_FAIL;
	}

	return FLASH_AUX_RC_OK;
}

int32_t flash_aux_write(uint32_t offset, uint16_t size, uint8_t *p_data, uint32_t input_crc32)
{
	const struct device *flash_dev;
	uint8_t read_back[FLASH_AUX_PAGE_SIZE];
	uint32_t calc_crc;
	int ret;

	if (!s_flash_aux_started) {
		return FLASH_AUX_RC_CALLED_WITHOUT_START_CMD;
	}

	if ((p_data == NULL) || (size == 0U) || (size > FLASH_AUX_PAGE_SIZE)) {
		return FLASH_AUX_RC_INVALID_DATA_SIZE;
	}

	calc_crc = crc32_ieee(p_data, size);
	if (calc_crc != input_crc32) {
		LOG_ERROR(LOG_MOD_FLASH_AUX,
			  "RX CRC mismatch off=0x%08x exp=0x%08x act=0x%08x\n",
			  offset, input_crc32, calc_crc);
		return FLASH_AUX_RC_CRC_FAIL_ON_RECEIVE;
	}

	ret = flash_aux_get_flash_device(&flash_dev);
	if (ret != 0) {
		return FLASH_AUX_RC_XSPI_NOT_INITIALIZED;
	}

	ret = flash_write(flash_dev, (off_t)(FLASH_AUX_FW_LOCATION + offset), p_data, size);
	if (ret != 0) {
		LOG_ERROR(LOG_MOD_FLASH_AUX, "Write failed off=0x%08x len=%u ret=%d\n",
			  offset, (unsigned int)size, ret);
		return FLASH_AUX_RC_PROGRAM_DATA_FAIL;
	}

	memset(read_back, 0, sizeof(read_back));
	ret = flash_read(flash_dev, (off_t)(FLASH_AUX_FW_LOCATION + offset), read_back, size);
	if (ret != 0) {
		LOG_ERROR(LOG_MOD_FLASH_AUX, "Read-back failed off=0x%08x len=%u ret=%d\n",
			  offset, (unsigned int)size, ret);
		return FLASH_AUX_RC_READ_DATA_FAIL;
	}

	calc_crc = crc32_ieee(read_back, size);
	if (calc_crc != input_crc32) {
		LOG_ERROR(LOG_MOD_FLASH_AUX,
			  "Read CRC mismatch off=0x%08x exp=0x%08x act=0x%08x\n",
			  offset, input_crc32, calc_crc);
		return FLASH_AUX_RC_CRC_FAIL_ON_READ;
	}

	return FLASH_AUX_RC_OK;
}

int32_t flash_aux_reboot(void)
{
	if (!s_flash_aux_started) {
		return FLASH_AUX_RC_CALLED_WITHOUT_START_CMD;
	}

	(void)flash_aux_resume_all_activities();
	s_flash_aux_started = false;
	return FLASH_AUX_RC_OK;
}

int32_t flash_aux_read_page(uint32_t offset, uint16_t size, uint8_t *p_page)
{
	const struct device *flash_dev;
	int ret;

	if (!s_flash_aux_started) {
		return FLASH_AUX_RC_CALLED_WITHOUT_START_CMD;
	}

	if ((p_page == NULL) || (size == 0U) || (size > FLASH_AUX_PAGE_SIZE)) {
		return FLASH_AUX_RC_INVALID_DATA_SIZE;
	}

	ret = flash_aux_get_flash_device(&flash_dev);
	if (ret != 0) {
		return FLASH_AUX_RC_XSPI_NOT_INITIALIZED;
	}

	ret = flash_read(flash_dev, (off_t)(FLASH_AUX_FW_LOCATION + offset), p_page, size);
	if (ret != 0) {
		LOG_ERROR(LOG_MOD_FLASH_AUX, "Read failed off=0x%08x len=%u ret=%d\n",
			  offset, (unsigned int)size, ret);
		return FLASH_AUX_RC_READ_DATA_FAIL;
	}

	return FLASH_AUX_RC_OK;
}

int32_t flash_aux_read_flash_id(uint32_t *id)
{
	const struct device *flash_dev;
	uint8_t jedec_id[FLASH_AUX_JEDEC_ID_LEN] = { 0 };
	int ret;

	if (id == NULL) {
		return FLASH_AUX_RC_FAIL_RW_COMMAND;
	}

	ret = flash_aux_get_flash_device(&flash_dev);
	if (ret != 0) {
		return FLASH_AUX_RC_XSPI_NOT_INITIALIZED;
	}

	ret = flash_read_jedec_id(flash_dev, jedec_id);
	if (ret != 0) {
		LOG_ERROR(LOG_MOD_FLASH_AUX, "Read JEDEC ID failed: %d\n", ret);
		return FLASH_AUX_RC_FAIL_RW_COMMAND;
	}

	flash_aux_normalize_jedec_id(jedec_id);

	*id = ((uint32_t)jedec_id[0] << 24) | ((uint32_t)jedec_id[1] << 16) |
	      ((uint32_t)jedec_id[2] << 8);
	return FLASH_AUX_RC_OK;
}

int32_t flash_aux_init_without_attributes(void)
{
	return FLASH_AUX_RC_OK;
}

int32_t flash_aux_completed_successfully(void)
{
	if (!s_flash_aux_started) {
		return FLASH_AUX_RC_CALLED_WITHOUT_START_CMD;
	}

	(void)flash_aux_resume_all_activities();
	s_flash_aux_started = false;
	return FLASH_AUX_RC_OK;
}

__attribute__((weak)) uint32_t flash_aux_stop_all_activities(void)
{
	return FLASH_AUX_RC_OK;
}

__attribute__((weak)) uint32_t flash_aux_resume_all_activities(void)
{
	return FLASH_AUX_RC_OK;
}
