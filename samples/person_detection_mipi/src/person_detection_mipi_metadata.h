/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PERSON_DETECTION_MIPI_METADATA_H_
#define PERSON_DETECTION_MIPI_METADATA_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "person_detection_mipi_inference.h"

#ifdef __cplusplus
extern "C" {
#endif

int person_detection_mipi_metadata_update(
	uint32_t frame_sequence, uint8_t usecase_id,
	const struct person_detection_mipi_inference_result *result);
int person_detection_mipi_metadata_get(char *dst, size_t dst_size);

struct person_detection_mipi_metadata_snapshot {
	uint32_t frame_sequence;
	uint32_t invoke_time_ms;
	int detection_count;
	detection_t detections[MAX_DETECTIONS];
	uint8_t usecase_id;
	bool valid;
};

int person_detection_mipi_metadata_snapshot_get(
	struct person_detection_mipi_metadata_snapshot *snapshot);
void person_detection_mipi_metadata_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PERSON_DETECTION_MIPI_METADATA_H_ */
