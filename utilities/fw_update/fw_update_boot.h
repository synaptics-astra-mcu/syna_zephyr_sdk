/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_FW_UPDATE_BOOT_H_
#define ZEPHYR_UTILITIES_FW_UPDATE_BOOT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

int32_t fw_update_boot_mark_trial(void);
int32_t fw_update_boot_try_handoff(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_FW_UPDATE_BOOT_H_ */
