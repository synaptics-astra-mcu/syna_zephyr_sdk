/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "nvm.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>

#include <logger.h>

#ifndef LOG_MOD_NVM
#define LOG_MOD_NVM "NVM"
#endif

#if !DT_HAS_CHOSEN(zephyr_flash_controller)
#error "zephyr,flash-controller chosen node is required for FW update NVM support"
#endif

#define NVM_FLASH_NODE DT_CHOSEN(zephyr_flash_controller)
#define NVM_FLASH_DEV DEVICE_DT_GET(NVM_FLASH_NODE)
#define NVM_PAGE_SIZE_IN_BYTES 256U
#define NVM_PROGRAM_CHUNK_SIZE NVM_PAGE_SIZE_IN_BYTES
#define NVM_READ_CHUNK_SIZE 16U
#define NVM_WRITE_RETRIES 3

BUILD_ASSERT(sizeof(st_nvm_data) == 456U, "Unexpected FW update NVM data size");
BUILD_ASSERT(sizeof(st_nvm_fw) == 460U, "Unexpected FW update NVM record size");

/*
 * NVM commits are programmed through the XSPI flash driver, which can use DMA
 * for page-sized writes. Keep the staging buffer in static aligned storage
 * instead of on the thread stack so the DMA source lives in normal SRAM.
 */
static uint8_t s_nvm_program_pages[2U * NVM_PAGE_SIZE_IN_BYTES] __aligned(32);

static void nvm_log_verify_mismatch(off_t offset, int attempt, const st_nvm_fw *expected,
				       const st_nvm_fw *actual)
{
	const uint8_t *exp = (const uint8_t *)expected;
	const uint8_t *act = (const uint8_t *)actual;
	size_t mismatch_index = sizeof(*expected);

	for (size_t i = 0U; i < sizeof(*expected); i++) {
		if (exp[i] != act[i]) {
			mismatch_index = i;
			break;
		}
	}

	if (mismatch_index < sizeof(*expected)) {
		LOG_WARN(LOG_MOD_NVM,
			 "NVM verify mismatch off=0x%lx attempt=%d byte=%u exp=0x%02x act=0x%02x\n",
			 (unsigned long)offset, attempt, (unsigned int)mismatch_index,
			 exp[mismatch_index], act[mismatch_index]);
	} else {
		LOG_WARN(LOG_MOD_NVM,
			 "NVM verify mismatch off=0x%lx attempt=%d but first differing byte not found\n",
			 (unsigned long)offset, attempt);
	}

	#if defined(CONFIG_SYNA_LOG_DEBUG)
	{
		size_t dump_bytes = MIN((size_t)16U, sizeof(*expected));

		LOG_WARN(LOG_MOD_NVM,
			 "NVM expected head: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			 exp[0], exp[1], exp[2], exp[3], exp[4], exp[5], exp[6], exp[7],
			 exp[8], exp[9], exp[10], exp[11], exp[12], exp[13], exp[14], exp[15]);
		LOG_WARN(LOG_MOD_NVM,
			 "NVM actual   head: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			 act[0], act[1], act[2], act[3], act[4], act[5], act[6], act[7],
			 act[8], act[9], act[10], act[11], act[12], act[13], act[14], act[15]);

		if (mismatch_index < sizeof(*expected)) {
			size_t window_start = (mismatch_index >= 8U) ? (mismatch_index - 8U) : 0U;
			size_t window_end = MIN(window_start + dump_bytes, sizeof(*expected));

			LOG_WARN(LOG_MOD_NVM,
				 "NVM mismatch window: bytes %u..%u\n",
				 (unsigned int)window_start, (unsigned int)(window_end - 1U));
			for (size_t i = window_start; i < window_end; i++) {
				LOG_WARN(LOG_MOD_NVM, "  [%03u] exp=0x%02x act=0x%02x\n",
					 (unsigned int)i, exp[i], act[i]);
			}
		}
	}
#endif
}

static uint32_t nvm_calc_crc(const st_nvm_data *data)
{
	return crc32_ieee((const uint8_t *)data, sizeof(*data));
}

static int nvm_get_flash_device(const struct device **flash_dev)
{
	if (flash_dev == NULL) {
		return -EINVAL;
	}

	*flash_dev = NVM_FLASH_DEV;
	if (!device_is_ready(*flash_dev)) {
		LOG_ERROR(LOG_MOD_NVM, "Flash controller is not ready\n");
		return -ENODEV;
	}

	return 0;
}

static int nvm_read_record(const struct device *flash_dev, off_t offset, st_nvm_fw *record)
{
	uint8_t *dst = (uint8_t *)record;
	size_t remaining = sizeof(*record);
	off_t read_offset = offset;
	int ret;

	if ((flash_dev == NULL) || (record == NULL)) {
		return -EINVAL;
	}

	while (remaining > 0U) {
		size_t chunk = MIN(remaining, (size_t)NVM_READ_CHUNK_SIZE);

		ret = flash_read(flash_dev, read_offset, dst, chunk);
		if (ret != 0) {
			return ret;
		}

		dst += chunk;
		read_offset += (off_t)chunk;
		remaining -= chunk;
	}

	return 0;
}

static int32_t nvm_validate_record(const st_nvm_fw *record)
{
	uint32_t crc32_val;

	if (record == NULL) {
		return NVM_RC_RD_FLASH_ERR;
	}

	if (record->data.magic_number != FW_NVM_MAGIC_NUM) {
		return NVM_RC_MAGIC_NUM_ERR;
	}

	crc32_val = nvm_calc_crc(&record->data);
	if (crc32_val != record->crc32) {
		return NVM_RC_CRC_ERR;
	}

	return NVM_RC_OK;
}

static int nvm_write_pages(const struct device *flash_dev, off_t offset, const void *buffer, size_t size)
{
	const uint8_t *src = buffer;
	size_t remaining = size;
	off_t write_offset = offset;
	int ret;

	while (remaining > 0U) {
		size_t chunk = MIN(remaining, (size_t)NVM_PROGRAM_CHUNK_SIZE);

		ret = flash_write(flash_dev, write_offset, src, chunk);
		if (ret != 0) {
			return ret;
		}

		src += chunk;
		write_offset += (off_t)chunk;
		remaining -= chunk;
	}

	return 0;
}

static int nvm_write_record(const struct device *flash_dev, off_t offset, st_nvm_fw *record)
{
	st_nvm_fw read_back = { 0 };
	int ret;
	int last_ret = -EIO;

	record->crc32 = nvm_calc_crc(&record->data);
	memset(s_nvm_program_pages, 0xFF, sizeof(s_nvm_program_pages));
	memcpy(s_nvm_program_pages, record, sizeof(*record));

	for (int attempt = 0; attempt < NVM_WRITE_RETRIES; attempt++) {
		if (attempt > 0) {
			k_msleep((attempt == 1) ? 50 : 200);
		}

		ret = flash_erase(flash_dev, offset, FW_NVM_SECTOR_SIZE_IN_BYTES);
		if (ret != 0) {
			last_ret = ret;
			LOG_WARN(LOG_MOD_NVM,
				 "NVM erase failed off=0x%lx attempt=%d ret=%d\n",
				 (unsigned long)offset, attempt + 1, ret);
			continue;
		}

		k_msleep(5);

		ret = nvm_write_pages(flash_dev, offset, s_nvm_program_pages,
				      sizeof(s_nvm_program_pages));
		if (ret != 0) {
			last_ret = ret;
			LOG_WARN(LOG_MOD_NVM,
				 "NVM write failed off=0x%lx attempt=%d ret=%d\n",
				 (unsigned long)offset, attempt + 1, ret);
			continue;
		}

		ret = nvm_read_record(flash_dev, offset, &read_back);
		if (ret != 0) {
			last_ret = ret;
			LOG_WARN(LOG_MOD_NVM,
				 "NVM read-back failed off=0x%lx attempt=%d ret=%d\n",
				 (unsigned long)offset, attempt + 1, ret);
			continue;
		}

		if (memcmp(&read_back, record, sizeof(read_back)) == 0) {
			return 0;
		}

		last_ret = -EIO;
		nvm_log_verify_mismatch(offset, attempt + 1, record, &read_back);
	}

	return last_ret;
}

static void nvm_prepare_default_record(st_nvm_fw *record)
{
	static const st_nvm_fw default_record = {
		.data = {
			.magic_number = FW_NVM_MAGIC_NUM,
			.fw_nv_size = sizeof(st_nvm_data),
			.apbl_slot = 0U,
			.image_offset = {
				.SDK_image_A_offset = 0x00050000U,
				.SDK_image_B_offset = 0x00340000U,
				.App_Image_A_offset = UINT32_MAX,
				.App_image_B_offset = UINT32_MAX,
				.Model_A_offset = 0x00629000U,
				.Model_B_offset = UINT32_MAX,
				.reserved_1 = UINT32_MAX,
				.reserved_2 = UINT32_MAX,
			},
			.security = {
				.num_of_defined_sections = 3U,
				.section_1 = {
					.control = 1U,
					.key = 0U,
					.start_offset = 0x00050000U,
					.end_offset = 0x0033FFFFU,
					.crypto_offset = 0U,
				},
				.section_2 = {
					.control = 1U,
					.key = 0U,
					.start_offset = 0x00340000U,
					.end_offset = 0x00628FFFU,
					.crypto_offset = 0xFFD10000U,
				},
				.section_3 = {
					.control = 0U,
					.key = 0U,
					.start_offset = 0x00629000U,
					.end_offset = 0x00A10FFFU,
					.crypto_offset = 0U,
				},
				.section_4 = {
					.control = UINT32_MAX,
					.key = UINT32_MAX,
					.start_offset = UINT32_MAX,
					.end_offset = UINT32_MAX,
					.crypto_offset = UINT32_MAX,
				},
				.section_5 = {
					.control = UINT32_MAX,
					.key = UINT32_MAX,
					.start_offset = UINT32_MAX,
					.end_offset = UINT32_MAX,
					.crypto_offset = UINT32_MAX,
				},
				.section_6 = {
					.control = UINT32_MAX,
					.key = UINT32_MAX,
					.start_offset = UINT32_MAX,
					.end_offset = UINT32_MAX,
					.crypto_offset = UINT32_MAX,
				},
				.section_7 = {
					.control = UINT32_MAX,
					.key = UINT32_MAX,
					.start_offset = UINT32_MAX,
					.end_offset = UINT32_MAX,
					.crypto_offset = UINT32_MAX,
				},
				.section_8 = {
					.control = UINT32_MAX,
					.key = UINT32_MAX,
					.start_offset = UINT32_MAX,
					.end_offset = UINT32_MAX,
					.crypto_offset = UINT32_MAX,
				},
			},
			.sw_update = {
				.state = 0U,
				.reset_cause = 0U,
				.failure_cause = 0U,
				.num_components = 4U,
				.components = {
					{
						.state = 0U,
						.failure_cause = 0U,
						.max_size = 0U,
						.num_slots = 2U,
						.primary_slot = 0U,
						.secondary_slot = 1U,
						.slots = {
							{ 0x00002000U, 1U, 1U },
							{ 0x00026000U, 1U, 1U },
						},
					},
					{
						.state = 0U,
						.failure_cause = 0U,
						.max_size = 0U,
						.num_slots = 2U,
						.primary_slot = 0U,
						.secondary_slot = 1U,
						.slots = {
							{ 0x00016000U, 1U, 1U },
							{ 0x0003A000U, 1U, 1U },
						},
					},
					{
						.state = 0U,
						.failure_cause = 0U,
						.max_size = 0U,
						.num_slots = 2U,
						.primary_slot = 0U,
						.secondary_slot = 1U,
						.slots = {
							{ 0x00050000U, 1U, 1U },
							{ 0x00340000U, 1U, 1U },
						},
					},
					{
						.state = 0U,
						.failure_cause = 0U,
						.max_size = 0U,
						.num_slots = 2U,
						.primary_slot = 0U,
						.secondary_slot = 1U,
						.slots = {
							{ 0x00629000U, 1U, 1U },
							{ 0x00629000U, 1U, 1U },
						},
					},
					{
						.state = 0U,
						.failure_cause = 0U,
						.max_size = 0U,
						.num_slots = 2U,
						.primary_slot = 0U,
						.secondary_slot = 1U,
						.slots = {
							{ UINT32_MAX, 1U, 1U },
							{ UINT32_MAX, 1U, 1U },
						},
					},
				},
			},
			.tracking = { 0 },
		},
		.crc32 = 0U,
	};

	*record = default_record;
	record->crc32 = nvm_calc_crc(&record->data);
}

static int32_t nvm_set_default_record(const struct device *flash_dev)
{
	st_nvm_fw default_record;
	int ret;

	nvm_prepare_default_record(&default_record);
	ret = nvm_write_record(flash_dev, FW_NVM_DATA_A, &default_record);
	if (ret != 0) {
		return NVM_SE_ERR;
	}

	ret = nvm_write_record(flash_dev, FW_NVM_DATA_B, &default_record);
	if (ret != 0) {
		return NVM_SE_ERR;
	}

	return NVM_RC_OK;
}

int32_t nvm_get_data(st_nvm_fw *p_st_nvm)
{
	const struct device *flash_dev;
	st_nvm_fw record = { 0 };
	int ret;
	int32_t rc;

	if (p_st_nvm == NULL) {
		return NVM_RC_RD_FLASH_ERR;
	}

	ret = nvm_get_flash_device(&flash_dev);
	if (ret != 0) {
		return NVM_RC_RD_FLASH_ERR;
	}

	ret = nvm_read_record(flash_dev, FW_NVM_DATA_A, &record);
	if (ret == 0) {
		rc = nvm_validate_record(&record);
		if (rc == NVM_RC_OK) {
			*p_st_nvm = record;
			return NVM_RC_OK;
		}
	}

	ret = nvm_read_record(flash_dev, FW_NVM_DATA_B, &record);
	if (ret == 0) {
		rc = nvm_validate_record(&record);
		if (rc == NVM_RC_OK) {
			*p_st_nvm = record;
			LOG_WARN(LOG_MOD_NVM, "Recovered NVM data from backup copy B\n");
			ret = nvm_write_record(flash_dev, FW_NVM_DATA_A, p_st_nvm);
			if (ret != 0) {
				LOG_WARN(LOG_MOD_NVM, "Failed to restore NVM A from backup B: %d\n", ret);
			}
			return NVM_RC_OK;
		}
	}

	nvm_prepare_default_record(p_st_nvm);
	(void)nvm_set_default_record(flash_dev);
	return NVM_RC_NVM_A_B_ERR;
}

int32_t nvm_set_data(st_nvm_fw *p_st_nvm)
{
	const struct device *flash_dev;
	int ret;

	if (p_st_nvm == NULL) {
		return NVM_PP_ERR;
	}

	ret = nvm_get_flash_device(&flash_dev);
	if (ret != 0) {
		return NVM_PP_ERR;
	}

	ret = nvm_write_record(flash_dev, FW_NVM_DATA_A, p_st_nvm);
	if (ret != 0) {
		LOG_ERROR(LOG_MOD_NVM, "Failed to write NVM A: %d\n", ret);
		return NVM_SE_ERR;
	}

	ret = nvm_write_record(flash_dev, FW_NVM_DATA_B, p_st_nvm);
	if (ret != 0) {
		LOG_ERROR(LOG_MOD_NVM, "Failed to write NVM B: %d\n", ret);
		return NVM_SE_ERR;
	}

	return NVM_RC_OK;
}
