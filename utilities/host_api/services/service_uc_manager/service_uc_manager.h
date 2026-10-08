/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_UTILITIES_HOST_API_SERVICE_UC_MANAGER_H_
#define ZEPHYR_UTILITIES_HOST_API_SERVICE_UC_MANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "host_api_utils.h"
#include "service_uc_manager_interface.h"
#include "service_uc_manager_opcodes.h"

#define USECASE_PERSON_CLASSIFICATION 1U
#define USECASE_PERSON_DETECTION 2U
#define USECASE_PERSON_SEGMENTATION 3U
#define USECASE_PERSON_POSE_DETECTION 4U
#define USECASE_HAND_GESTURE_DETECTION 5U
#define USECASE_FID_HGD 7U
#define USECASE_JPEG_PREROLL 10U
#define USECASE_AUDIO_MIC 12U

#define UC_MANAGER_RC_OK 0
#define UC_MANAGER_RC_ERROR -1

#define MAX_TASK_PARAMETERS 4U

typedef struct uc_message {
	uint8_t command;
	uint8_t data[MAX_TASK_PARAMETERS];
} uc_message_t;

int service_uc_manager_handler(uint8_t opcode_id, uint8_t *p_input, uint8_t *p_output);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_UTILITIES_HOST_API_SERVICE_UC_MANAGER_H_ */
