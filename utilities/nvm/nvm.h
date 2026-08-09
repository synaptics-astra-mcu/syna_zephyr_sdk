/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_NVM_H_
#define ZEPHYR_UTILITIES_NVM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define FW_NVM_SECTOR_SIZE_IN_BYTES 0x1000U
#define FW_NVM_DATA_A 0x4A000U
#define FW_NVM_DATA_B (FW_NVM_DATA_A + FW_NVM_SECTOR_SIZE_IN_BYTES)
#define FW_NVM_MAGIC_NUM 0x46574E56U
#define FW_NVM_UPDATE_MAX_COMPONENTS 4U
#define FW_NVM_UPDATE_MAX_SLOTS 2U

typedef enum nvm_rc {
	NVM_RC_OK = 0,
	NVM_RC_RD_FLASH_ERR,
	NVM_RC_MAGIC_NUM_ERR,
	NVM_RC_CRC_ERR,
	NVM_RC_NVM_A_ERR,
	NVM_RC_NVM_A_B_ERR,
	NVM_MARK_SEC_RW_ERR,
	NVM_SE_ERR,
	NVM_PP_ERR,
} en_nvm_rc;

typedef struct image_offset {
	uint32_t SDK_image_A_offset;
	uint32_t SDK_image_B_offset;
	uint32_t App_Image_A_offset;
	uint32_t App_image_B_offset;
	uint32_t Model_A_offset;
	uint32_t Model_B_offset;
	uint32_t reserved_1;
	uint32_t reserved_2;
} st_nvm_image_offset;

typedef struct section_control_block {
	uint32_t control;
	uint32_t key;
	uint32_t start_offset;
	uint32_t end_offset;
	uint32_t crypto_offset;
} st_nvm_section_control_block;

typedef struct security {
	uint32_t num_of_defined_sections;
	st_nvm_section_control_block section_1;
	st_nvm_section_control_block section_2;
	st_nvm_section_control_block section_3;
	st_nvm_section_control_block section_4;
	st_nvm_section_control_block section_5;
	st_nvm_section_control_block section_6;
	st_nvm_section_control_block section_7;
	st_nvm_section_control_block section_8;
} st_nvm_security;

typedef struct image_slot {
	uint32_t slot_address;
	uint32_t image_is_bootable;
	uint32_t image_is_functional;
} st_nvm_image_slot;

typedef struct fw_update_component {
	uint32_t state;
	uint32_t failure_cause;
	uint32_t max_size;
	uint32_t num_slots;
	uint32_t primary_slot;
	uint32_t secondary_slot;
	st_nvm_image_slot slots[FW_NVM_UPDATE_MAX_SLOTS];
} st_nvm_fw_update_component;

typedef struct fw_update_global {
	uint32_t state;
	uint32_t reset_cause;
	uint32_t failure_cause;
	uint32_t num_components;
	st_nvm_fw_update_component components[FW_NVM_UPDATE_MAX_COMPONENTS];
} st_nvm_fw_update_global;

typedef struct tracking {
	uint32_t wd_reset;
	uint32_t oom_reset;
	uint32_t fault_reset;
	uint32_t os_panic;
	uint32_t program_reset;
	uint32_t fw_update_failure;
	uint32_t app_sw_reset;
	uint32_t fw_update_reset_cause;
	uint32_t reserved_1;
	uint32_t reserved_2;
} st_nvm_tracking;

typedef struct _data {
	uint32_t magic_number;
	uint32_t fw_nv_size;
	uint32_t apbl_slot;
	st_nvm_image_offset image_offset;
	st_nvm_security security;
	st_nvm_fw_update_global sw_update;
	st_nvm_tracking tracking;
} st_nvm_data;

typedef struct _nvm {
	st_nvm_data data;
	uint32_t crc32;
} st_nvm_fw;

int32_t nvm_get_data(st_nvm_fw *p_st_nvm);
int32_t nvm_set_data(st_nvm_fw *p_st_nvm);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_NVM_H_ */
