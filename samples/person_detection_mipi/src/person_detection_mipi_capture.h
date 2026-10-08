/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PERSON_DETECTION_MIPI_CAPTURE_H_
#define PERSON_DETECTION_MIPI_CAPTURE_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PERSON_DETECTION_MIPI_CAPTURE_WIDTH 480U
#define PERSON_DETECTION_MIPI_CAPTURE_HEIGHT 270U
#define PERSON_DETECTION_MIPI_CAPTURE_PITCH PERSON_DETECTION_MIPI_CAPTURE_WIDTH
#define PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE \
	((size_t)PERSON_DETECTION_MIPI_CAPTURE_WIDTH * \
	 (size_t)PERSON_DETECTION_MIPI_CAPTURE_HEIGHT)

/* OV02C10 provides 480x270 RAW8; the WQVGA model consumes its top 480x256 region. */
#define PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE \
	((size_t)PERSON_DETECTION_MIPI_CAPTURE_WIDTH * 256U)

struct person_detection_mipi_capture_frame {
	const uint8_t *data;
	size_t size;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t bytesused;
	uint32_t timestamp;
	uint32_t sequence;
	bool valid;
};

int person_detection_mipi_capture_init(void);
int person_detection_mipi_capture_start(void);
int person_detection_mipi_capture_stop(void);
int person_detection_mipi_capture_get_latest(
	struct person_detection_mipi_capture_frame *frame,
	uint8_t *dst, size_t dst_size);
int person_detection_mipi_capture_get_latest_full(
	struct person_detection_mipi_capture_frame *frame,
	uint8_t *dst, size_t dst_size);
uint8_t *person_detection_mipi_capture_scratch_frame_buffer(void);

#ifdef __cplusplus
}
#endif

#endif /* PERSON_DETECTION_MIPI_CAPTURE_H_ */
