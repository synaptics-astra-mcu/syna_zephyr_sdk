/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_HOST_API_SERVICE_UC_MANAGER_INTERFACE_H_
#define ZEPHYR_UTILITIES_HOST_API_SERVICE_UC_MANAGER_INTERFACE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef int32_t (*uc_create_fn)(void);
typedef int32_t (*uc_start_fn)(void);
typedef int32_t (*uc_stop_fn)(void);
typedef int32_t (*uc_resume_fn)(void);
typedef int32_t (*uc_kill_fn)(void);

typedef struct uc_operations {
	uc_create_fn create;
	uc_start_fn start;
	uc_stop_fn stop;
	uc_resume_fn resume;
	uc_kill_fn kill;
} uc_operations_t;

int uc_manager_register_usecase(uint8_t usecase_id, const uc_operations_t *ops);
int uc_manager_unregister_usecase(uint8_t usecase_id);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_HOST_API_SERVICE_UC_MANAGER_INTERFACE_H_ */
