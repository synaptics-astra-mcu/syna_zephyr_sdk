/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_FW_UPDATE_CFG_H_
#define ZEPHYR_UTILITIES_FW_UPDATE_CFG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define FW_UPDATE_NUM_COMPONENTS 4
#define FW_UPDATE_APP_NUM_SLOTS 2
#define FW_UPDATE_SDK_NUM_SLOTS 2
#define FW_UPDATE_MODEL_NUM_SLOTS 1
#define FW_UPDATE_APBL_NUM_SLOTS 2
#define FW_UPDATE_SPK_NUM_SLOTS 2
#define FW_UPDATE_POST_ACCEPT_CLEAN 1
#define FW_UPDATE_POST_REJECT_CLEAN 1
#define FW_UPDATE_ERASE_ON_CLEAN 0
#define FW_UPDATE_SDK_POST_UART 0

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_FW_UPDATE_CFG_H_ */
