/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PERSON_DETECTION_MIPI_STREAM_H_
#define PERSON_DETECTION_MIPI_STREAM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int person_detection_mipi_stream_init(void);
int person_detection_mipi_stream_start(void);
int person_detection_mipi_stream_stop(void);
int person_detection_mipi_stream_pause(void);
int person_detection_mipi_stream_resume(void);
int person_detection_mipi_stream_process_frame(const uint8_t *frame_data,
					       size_t frame_size, uint32_t sequence);

#ifdef __cplusplus
}
#endif

#endif /* PERSON_DETECTION_MIPI_STREAM_H_ */
