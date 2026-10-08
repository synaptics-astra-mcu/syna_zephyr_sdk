/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_FW_UPDATE_H_
#define ZEPHYR_UTILITIES_FW_UPDATE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define FW_UPDATE_PAGE_SIZE_IN_BYTES 256U
#define FW_UPDATE_SECTOR_SIZE_IN_BYTES (4U * 1024U)
#define FW_UPDATE_BUFF_SIZE_IN_BYTES (FW_UPDATE_SECTOR_SIZE_IN_BYTES + 4U)
#define FW_UPDATE_CRC32_POLY 0xEDB88320U
#define FW_UPDATE_CRC32_START_VAL 0x0U
#define FW_UPDATE_NVM_MAGIC_NUM 0x46574E56U
#define FW_UPDATE_M55_SW_RST_CAUSE 0x3U
#define FW_UPDATE_MAX_COMPONENTS 5U
#define FW_UPDATE_MAX_SLOTS 2U
#define FW_UPDATE_IMG_NOT_BOOTABLE 0U
#define FW_UPDATE_IMG_BOOTABLE 1U
#define FW_UPDATE_IMG_NOT_FUNCTIONAL 0U
#define FW_UPDATE_IMG_FUNCTIONAL 1U
#define FW_UPDATE_SPK_A_ADDR 0x00002000U
#define FW_UPDATE_SPK_B_ADDR 0x00026000U
#define FW_UPDATE_APBL_A_ADDR 0x00016000U
#define FW_UPDATE_APBL_B_ADDR 0x0003A000U
#define FW_UPDATE_SDK_A_ADDR 0x00050000U
#define FW_UPDATE_SDK_B_ADDR 0x00340000U
#define FW_UPDATE_MODEL_A_ADDR 0x00629000U
#define FW_UPDATE_MODEL_B_ADDR 0xFFFFFFFFU
#define FW_UPDATE_APP_A_ADDR 0xFFFFFFFFU
#define FW_UPDATE_APP_B_ADDR 0xFFFFFFFFU

typedef enum fw_update_rc {
	FW_UPDATE_RC_OK = 0,
	FW_UPDATE_RC_START_OPCODE_BAD_STATE,
	FW_UPDATE_RC_WRITE_OPCODE_BAD_STATE,
	FW_UPDATE_RC_FINISH_OPCODE_BAD_STATE,
	FW_UPDATE_RC_INSTALL_OPCODE_BAD_STATE,
	FW_UPDATE_RC_ACCEPT_OPCODE_BAD_STATE,
	FW_UPDATE_RC_REJECT_OPCODE_BAD_STATE,
	FW_UPDATE_RC_CLEAN_OPCODE_BAD_STATE,
	FW_UPDATE_RC_DEPENDENCY_CHECK_ERR,
	FW_UPDATE_RC_FW_POST_ERR,
	FW_UPDATE_RC_APP_POST_ERR,
	FW_UPDATE_RC_GET_NVM_ERR,
	FW_UPDATE_RC_SET_NVM_ERR,
	FW_UPDATE_RC_WRITE_ERR,
	FW_UPDATE_RC_ERS_STORAGE_ERR,
	FW_UPDATE_RC_APBL_POST_ERR,
	FW_UPDATE_RC_DECRYPTION_ERR,
	FW_UPDATE_RC_BCM_LOAD_INTENDED_SPK_ERR,
	FW_UPDATE_RC_SPK_LOAD_INTENDED_APBL_ERR,
	FW_UPDATE_RC_SPK_UPDATE_INTENDED_SPK_ERR,
	FW_UPDATE_RC_SPK_UPDATE_ROLLBACK_COUNTER_ERR,
	FW_UPDATE_RC_NOT_SUPPORTED,
} en_fw_update_rc;

typedef enum fw_update_state {
	FW_UPDATE_STATE_READY = 0,
	FW_UPDATE_STATE_WRITING,
	FW_UPDATE_STATE_CANDIDATE,
	FW_UPDATE_STATE_STAGED,
	FW_UPDATE_STATE_TRIAL,
	FW_UPDATE_STATE_REJECTED,
	FW_UPDATE_STATE_FAILED,
	FW_UPDATE_STATE_UPDATED,
	FW_UPDATE_STATE_LAST,
} en_fw_update_state;

typedef enum fw_update_fail_reason {
	FW_UPDATE_FAIL_REASON_NO_FAILURE = 0,
	FW_UPDATE_FAIL_REASON_START,
	FW_UPDATE_FAIL_REASON_WRITE,
	FW_UPDATE_FAIL_REASON_FINISH,
	FW_UPDATE_FAIL_REASON_INSTALL,
	FW_UPDATE_FAIL_REASON_CANCEL,
	FW_UPDATE_FAIL_REASON_REBOOT,
	FW_UPDATE_FAIL_REASON_ACCEPT,
	FW_UPDATE_FAIL_REASON_REJECT,
	FW_UPDATE_FAIL_REASON_CLEAN,
	FW_UPDATE_FAIL_REASON_GET_INFO,
	FW_UPDATE_FAIL_REASON_GET_STATE,
	FW_UPDATE_FAIL_REASON_LAST,
} en_fw_update_fail_reason;

typedef struct fw_update_info {
	uint32_t fw_version;
	uint32_t write_max_num_bytes;
} st_fw_update_info;

typedef enum fw_update_image_type {
	FW_UPDATE_IMG_TYPE_SPK = 0,
	FW_UPDATE_IMG_TYPE_APBL,
	FW_UPDATE_IMG_TYPE_SDK,
	FW_UPDATE_IMG_TYPE_MODEL,
	//FW_UPDATE_IMG_TYPE_APP,      // Must be aligned to FW_NVM_UPDATE_MAX_COMPONENTS 4U (defined in APBL as well)
	FW_UPDATE_IMG_TYPE_LAST,
} en_fw_update_image_type;

int32_t fw_update_start(uint32_t image_id);
int32_t fw_update_write(uint32_t image_id, uint32_t num_bytes, uint8_t *p_data);
int32_t fw_update_finish(uint32_t image_id);
int32_t fw_update_install(uint32_t auto_reset);
int32_t fw_update_cancel(uint32_t image_id);
int32_t fw_update_reboot(void);
int32_t fw_update_accept(void);
int32_t fw_update_reject(uint32_t auto_reset);
int32_t fw_update_clean(uint32_t image_id);
int32_t fw_update_get_info(st_fw_update_info *p_st_info);
int32_t fw_update_get_state(uint32_t *p_state);
int32_t fw_update_get_component_state(uint32_t image_id, uint32_t *p_state);
int32_t fw_update_get_failure(uint32_t *p_failure);
int32_t fw_update_get_component_failure(uint32_t image_id, uint32_t *p_failure);

__attribute__((weak)) uint32_t fw_update_app_verify(void);
__attribute__((weak)) uint32_t fw_post_uart(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_FW_UPDATE_H_ */
