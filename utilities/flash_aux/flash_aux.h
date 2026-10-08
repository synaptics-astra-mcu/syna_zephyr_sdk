/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_FLASH_AUX_H_
#define ZEPHYR_UTILITIES_FLASH_AUX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define FLASH_AUX_FW_LOCATION 0x0U
#define SECTOR_SIZE_IN_BYTES (4U * 1024U)
#define FLASH_AUX_START_SECTOR_NUM (FLASH_AUX_FW_LOCATION / SECTOR_SIZE_IN_BYTES)

#define FLASH_AUX_ERASE_BLOCK 0U
#define FLASH_AUX_ERASE_SECTOR 1U

#define FLASH_AUX_PAGE_SIZE 256U

enum {
	FLASH_AUX_RC_OK = 0,
	FLASH_AUX_RC_CALLED_WITHOUT_START_CMD = 1,
	FLASH_AUX_RC_OFFSET_OUT_OF_RANGE = 2,
	FLASH_AUX_RC_XSPI_NOT_INITIALIZED = 3,
	FLASH_AUX_RC_INVALID_DATA_SIZE = 4,
	FLASH_AUX_RC_CRC_FAIL_ON_RECEIVE = 5,
	FLASH_AUX_RC_CRC_FAIL_ON_READ = 6,
	FLASH_AUX_RC_FAIL_RW_COMMAND = 7,
	FLASH_AUX_RC_ERASE_SECTOR_FAIL = 8,
	FLASH_AUX_RC_PROGRAM_DATA_FAIL = 9,
	FLASH_AUX_RC_READ_DATA_FAIL = 10,
	FLASH_AUX_RC_NOT_SUPPORTED = 11,
};

int32_t flash_aux_start(void);
int32_t flash_aux_erase(uint8_t type, uint32_t offset);
int32_t flash_aux_write(uint32_t offset, uint16_t size, uint8_t *p_data, uint32_t input_crc32);
int32_t flash_aux_reboot(void);
int32_t flash_aux_read_page(uint32_t offset, uint16_t size, uint8_t *p_page);
int32_t flash_aux_read_flash_id(uint32_t *id);
int32_t flash_aux_init_without_attributes(void);
int32_t flash_aux_completed_successfully(void);

__attribute__((weak)) uint32_t flash_aux_stop_all_activities(void);
__attribute__((weak)) uint32_t flash_aux_resume_all_activities(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_FLASH_AUX_H_ */
