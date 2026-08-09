/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Reusable TFLite Micro runtime for person detection.
 */

#include "person_detection_mipi_inference.h"
#include "person_detection_mipi_capture.h"

#include <cstdint>
#include <errno.h>
#include <new>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#define MODEL_FLASH_ADDRESS CONFIG_PERSON_DETECTION_MODEL_FLASH_ADDR
#define MODEL_SIZE          CONFIG_PERSON_DETECTION_MODEL_SIZE

#define MIPI_DEV_NODE DT_NODELABEL(video_syna0)
#define CAPTURE_MEM_NODE DT_PHANDLE(MIPI_DEV_NODE, memory_region)
#define CAPTURE_MEM_BASE DT_REG_ADDR(CAPTURE_MEM_NODE)
#define CAPTURE_MEM_SIZE DT_REG_SIZE(CAPTURE_MEM_NODE)

static constexpr int kTensorArenaSize = 600 * 1024;
/* Reserve the driver's RAW8 MIPI DMA staging frame at the pool base. */
static constexpr size_t kCaptureDmaStagingOffset = 0U;
static constexpr size_t kCaptureLatestFrameOffset =
	kCaptureDmaStagingOffset + (2U * PERSON_DETECTION_MIPI_CAPTURE_FRAME_SIZE);
static constexpr size_t kCaptureScratchFrameOffset =
	kCaptureLatestFrameOffset + PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE;
static constexpr size_t kInputBufOffset =
	kCaptureScratchFrameOffset + PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE;
static constexpr size_t kTensorArenaOffset = kInputBufOffset + PERSON_DETECTION_MIPI_INPUT_SIZE;
static constexpr size_t kReservedBytesNeeded = kTensorArenaOffset + kTensorArenaSize;

static_assert(CAPTURE_MEM_SIZE >= kReservedBytesNeeded,
	      "Capture reserved memory is too small for inference buffers");
static_assert(PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE ==
	      (PERSON_DETECTION_MIPI_INPUT_WIDTH * PERSON_DETECTION_MIPI_INPUT_HEIGHT),
	      "Capture crop dimensions must match the model input dimensions");

static uint8_t *const tensor_arena =
	reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(CAPTURE_MEM_BASE + kTensorArenaOffset));
static uint8_t *const input_buf =
	reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(CAPTURE_MEM_BASE + kInputBufOffset));

static tflite::MicroMutableOpResolver<1> resolver;
static tflite::MicroInterpreter *interpreter;
static bool inference_initialized;

static uint8_t interpreter_storage[sizeof(tflite::MicroInterpreter)]
	__attribute__((aligned(__alignof__(tflite::MicroInterpreter))));

static int validate_input_tensor(const TfLiteTensor *input)
{
	if (input == nullptr) {
		return -ENODEV;
	}

	if (input->dims->size != 4 ||
	    input->dims->data[0] != 1 ||
	    input->dims->data[1] != PERSON_DETECTION_MIPI_INPUT_HEIGHT ||
	    input->dims->data[2] != PERSON_DETECTION_MIPI_INPUT_WIDTH ||
	    input->dims->data[3] != PERSON_DETECTION_MIPI_INPUT_CHANNELS) {
		return -EINVAL;
	}

	if (input->type != kTfLiteInt8) {
		return -ENOTSUP;
	}

	if (input->bytes < PERSON_DETECTION_MIPI_INPUT_SIZE) {
		return -ENOMEM;
	}

	return 0;
}

static int expand_raw8_to_rgb(const uint8_t *raw_frame, size_t raw_frame_size)
{
	const size_t pixel_count =
		(size_t)PERSON_DETECTION_MIPI_INPUT_WIDTH * (size_t)PERSON_DETECTION_MIPI_INPUT_HEIGHT;

	if (raw_frame_size < pixel_count) {
		return -EINVAL;
	}

	for (size_t i = 0U, j = 0U; i < pixel_count; i++, j += 3U) {
		uint8_t v = raw_frame[i];

		input_buf[j + 0U] = v;
		input_buf[j + 1U] = v;
		input_buf[j + 2U] = v;
	}

	return 0;
}

int person_detection_mipi_inference_init(void)
{
	if (inference_initialized) {
		return 0;
	}

	const tflite::Model *model =
		tflite::GetModel(reinterpret_cast<const void *>(MODEL_FLASH_ADDRESS));
	if (model->version() != TFLITE_SCHEMA_VERSION) {
		printk("person_detection_mipi: model schema mismatch at 0x%08x: got %d, expected %d\n",
		       (uint32_t)MODEL_FLASH_ADDRESS, model->version(), TFLITE_SCHEMA_VERSION);
		return -EINVAL;
	}

	resolver.AddEthosU();

	interpreter = new (interpreter_storage)
		tflite::MicroInterpreter(model, resolver, tensor_arena, kTensorArenaSize);

	if (interpreter->AllocateTensors() != kTfLiteOk) {
		printk("person_detection_mipi: AllocateTensors failed (arena %u bytes)\n",
		       (uint32_t)kTensorArenaSize);
		return -ENOMEM;
	}

	int ret = validate_input_tensor(interpreter->input(0));
	if (ret != 0) {
		printk("person_detection_mipi: model input validation failed: %d\n", ret);
		return ret;
	}

	inference_initialized = true;

	return 0;
}

int person_detection_mipi_inference_run_rgb_frame(
	const uint8_t *rgb_frame, size_t rgb_frame_size,
	struct person_detection_mipi_inference_result *result)
{
	if (rgb_frame == nullptr || result == nullptr) {
		return -EINVAL;
	}

	if (rgb_frame_size < PERSON_DETECTION_MIPI_INPUT_SIZE) {
		return -EINVAL;
	}

	int ret = person_detection_mipi_inference_init();
	if (ret != 0) {
		return ret;
	}

	TfLiteTensor *input = interpreter->input(0);
	ret = validate_input_tensor(input);
	if (ret != 0) {
		return ret;
	}

	memcpy(input_buf, rgb_frame, PERSON_DETECTION_MIPI_INPUT_SIZE);
	ret = infer_preprocess(input_buf, PERSON_DETECTION_MIPI_INPUT_WIDTH,
			       PERSON_DETECTION_MIPI_INPUT_HEIGHT);
	if (ret != 0) {
		return ret;
	}

	memcpy(input->data.int8, input_buf, PERSON_DETECTION_MIPI_INPUT_SIZE);

	uint32_t start_ms = k_uptime_get_32();
	if (interpreter->Invoke() != kTfLiteOk) {
		return -EIO;
	}
	uint32_t end_ms = k_uptime_get_32();

	TfLiteTensor *out_box1 = interpreter->output(0);
	TfLiteTensor *out_scores = interpreter->output(1);
	TfLiteTensor *out_box2 = interpreter->output(2);

	if (out_box1 == nullptr || out_scores == nullptr || out_box2 == nullptr) {
		return -ENODEV;
	}

	result->invoke_time_ms = end_ms - start_ms;
	result->detection_count = 0;

	ret = infer_postprocess(out_box1->data.int8, (size_t)out_box1->bytes,
				out_scores->data.int8, (size_t)out_scores->bytes,
				out_box2->data.int8, (size_t)out_box2->bytes,
				MAX_DETECTIONS, result->detections,
				&result->detection_count);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

int person_detection_mipi_inference_run_raw8_frame(
	const uint8_t *raw_frame, size_t raw_frame_size,
	struct person_detection_mipi_inference_result *result)
{
	int ret;

	if (raw_frame == nullptr || result == nullptr) {
		return -EINVAL;
	}

	ret = expand_raw8_to_rgb(raw_frame, raw_frame_size);
	if (ret != 0) {
		return ret;
	}

	ret = person_detection_mipi_inference_init();
	if (ret != 0) {
		return ret;
	}

	TfLiteTensor *input = interpreter->input(0);
	ret = validate_input_tensor(input);
	if (ret != 0) {
		return ret;
	}

	ret = infer_preprocess(input_buf, PERSON_DETECTION_MIPI_INPUT_WIDTH,
			       PERSON_DETECTION_MIPI_INPUT_HEIGHT);
	if (ret != 0) {
		return ret;
	}

	memcpy(input->data.int8, input_buf, PERSON_DETECTION_MIPI_INPUT_SIZE);

	uint32_t start_ms = k_uptime_get_32();
	if (interpreter->Invoke() != kTfLiteOk) {
		return -EIO;
	}
	uint32_t end_ms = k_uptime_get_32();

	TfLiteTensor *out_box1 = interpreter->output(0);
	TfLiteTensor *out_scores = interpreter->output(1);
	TfLiteTensor *out_box2 = interpreter->output(2);

	if (out_box1 == nullptr || out_scores == nullptr || out_box2 == nullptr) {
		return -ENODEV;
	}

	result->invoke_time_ms = end_ms - start_ms;
	result->detection_count = 0;

	ret = infer_postprocess(out_box1->data.int8, (size_t)out_box1->bytes,
				out_scores->data.int8, (size_t)out_scores->bytes,
				out_box2->data.int8, (size_t)out_box2->bytes,
				MAX_DETECTIONS, result->detections,
				&result->detection_count);
	if (ret != 0) {
		return ret;
	}

	return 0;
}
