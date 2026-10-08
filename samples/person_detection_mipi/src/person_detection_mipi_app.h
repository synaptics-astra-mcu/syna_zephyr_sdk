/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PERSON_DETECTION_MIPI_APP_H_
#define PERSON_DETECTION_MIPI_APP_H_

#include <stddef.h>
#include <stdbool.h>

#include "person_detection_mipi_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

int person_detection_mipi_app_create(void);
int person_detection_mipi_app_start(void);
int person_detection_mipi_app_stop(void);
int person_detection_mipi_app_resume(void);
int person_detection_mipi_app_kill(void);
int person_detection_mipi_app_get_latest_metadata(char *dst, size_t dst_size);
int person_detection_mipi_app_get_latest_snapshot(
	struct person_detection_mipi_metadata_snapshot *snapshot);
bool person_detection_mipi_app_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* PERSON_DETECTION_MIPI_APP_H_ */
