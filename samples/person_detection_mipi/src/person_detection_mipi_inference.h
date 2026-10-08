/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PERSON_DETECTION_MIPI_INFERENCE_H_
#define PERSON_DETECTION_MIPI_INFERENCE_H_

#include <stddef.h>
#include <stdint.h>

#include "infer_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PERSON_DETECTION_MIPI_INPUT_WIDTH 480
#define PERSON_DETECTION_MIPI_INPUT_HEIGHT 256
#define PERSON_DETECTION_MIPI_INPUT_CHANNELS 3
#define PERSON_DETECTION_MIPI_INPUT_SIZE \
	(PERSON_DETECTION_MIPI_INPUT_WIDTH * PERSON_DETECTION_MIPI_INPUT_HEIGHT * \
	 PERSON_DETECTION_MIPI_INPUT_CHANNELS)

struct person_detection_mipi_inference_result {
	uint32_t invoke_time_ms;
	int detection_count;
	detection_t detections[MAX_DETECTIONS];
};

int person_detection_mipi_inference_init(void);
int person_detection_mipi_inference_run_rgb_frame(
	const uint8_t *rgb_frame, size_t rgb_frame_size,
	struct person_detection_mipi_inference_result *result);
int person_detection_mipi_inference_run_raw8_frame(
	const uint8_t *raw_frame, size_t raw_frame_size,
	struct person_detection_mipi_inference_result *result);

#ifdef __cplusplus
}
#endif

#endif /* PERSON_DETECTION_MIPI_INFERENCE_H_ */
