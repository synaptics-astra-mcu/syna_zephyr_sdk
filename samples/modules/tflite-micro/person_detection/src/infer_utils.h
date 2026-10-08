/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Person-detection inference utilities: preprocess and postprocess API
 */

#ifndef INFER_UTILS_H_
#define INFER_UTILS_H_

#include <stddef.h>

/**
 * @brief Maximum number of detections the postprocessor will return
 */
#define MAX_DETECTIONS   16

/**
 * @brief Maximum number of candidate boxes produced by the network
 */
#define MAX_BOXES        600

/**
 * @brief Maximum number of boxes to keep during non-max suppression
 */
#define NMS_MAX_KEEP     6

/**
 * @struct detection_t
 * @brief Single detection result in pixel coordinates
 *
 * - `class_id` currently always 0 for the person model
 * - `score` is the confidence (0.0 - 1.0)
 * - `x, y` are the top-left pixel coordinates
 * - `w, h` are the width and height in pixels
 */
typedef struct {
    int   class_id;
    float score;
    float x;
    float y;
    float w;
    float h;
} detection_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Preprocess an input image in-place for the model.
 *
 * The sample stores inputs in flash as interleaved RGB uint8. The model
 * expects int8 with a zero-point conversion performed by XOR 0x80. This
 * function applies that conversion in-place.
 *
 * @param addr Pointer to image buffer (must be writable). The buffer layout
 *             is interleaved RGB, row-major, size = width * height * 3 bytes.
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @return 0 on success, negative on error (invalid args).
 */
int infer_preprocess(void *addr, int width, int height);

/**
 * @brief Postprocess raw network outputs into detection_t array.
 *
 * The function decodes the model's three output tensors into bounding boxes
 * and scores, applies non-max suppression, and fills the provided output
 * array with up to `max_out` detections.
 *
 * @param box1_addr Pointer to first box tensor data (raw int8/uint8 as produced by the model)
 * @param box1_size Size in bytes of the first box tensor.
 * @param cls_addr  Pointer to class/score tensor data.
 * @param cls_size  Size in bytes of the class/score tensor.
 * @param box2_addr Pointer to second box tensor data (if applicable).
 * @param box2_size Size in bytes of the second box tensor.
 * @param max_out Maximum number of detections to write into `out_dets`.
 * @param out_dets Pointer to caller-allocated array of `detection_t` of length >= max_out.
 * @param out_count Pointer to int updated with the number of detections written.
 * @return 0 on success, negative on error.
 */
int infer_postprocess(const void *box1_addr, size_t box1_size,
                      const void *cls_addr,  size_t cls_size,
                      const void *box2_addr, size_t box2_size,
                      int max_out, detection_t *out_dets, int *out_count);

#ifdef __cplusplus
}
#endif

#endif /* INFER_UTILS_H_ */
