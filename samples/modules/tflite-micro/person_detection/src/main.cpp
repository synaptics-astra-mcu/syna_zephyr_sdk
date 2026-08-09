/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Person detection sample: run TFLite Micro inference and print detections
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"

extern "C" {
#include "infer_utils.h"
}

LOG_MODULE_REGISTER(person_detection, LOG_LEVEL_INF);

/* Model and input flash addresses derived from Kconfig */
#define MODEL_FLASH_ADDRESS CONFIG_PERSON_DETECTION_MODEL_FLASH_ADDR
#define INPUT_FLASH_ADDRESS CONFIG_PERSON_DETECTION_INPUT_FLASH_ADDR
#define MODEL_SIZE          CONFIG_PERSON_DETECTION_MODEL_SIZE

#define INPUT_WIDTH  480
#define INPUT_HEIGHT 256

/* Tensor arena placed in noinit section to host tensors and weights */
const int kTensorArenaSize = 600 * 1024;
uint8_t tensor_arena[kTensorArenaSize] __attribute__((section(".noinit.tensor_arena")));

static uint8_t input_buf[INPUT_HEIGHT * INPUT_WIDTH * 3] __attribute__((section(".noinit.tensor_arena")));

static uint8_t model_sram_copy[MODEL_SIZE] __attribute__((aligned(16), section(".noinit.tensor_arena")));

int main(void)
{
    memcpy(model_sram_copy, reinterpret_cast<const void *>(MODEL_FLASH_ADDRESS), MODEL_SIZE);
    const uint8_t *model_data = model_sram_copy;

    const tflite::Model *model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        LOG_ERR("model schema version %d does not match supported version %d",
                model->version(), TFLITE_SCHEMA_VERSION);
        return -1;
    }

    tflite::MicroMutableOpResolver<1> resolver;
    resolver.AddEthosU();

    tflite::MicroInterpreter interpreter(
        model, resolver, tensor_arena, kTensorArenaSize);

    if (interpreter.AllocateTensors() != kTfLiteOk) {
        LOG_ERR("AllocateTensors() failed");
        return -1;
    }

    TfLiteTensor *input = interpreter.input(0);
    if (!input) {
        LOG_ERR("no input tensor");
        return -1;
    }

    if (input->dims->size != 4 ||
        input->dims->data[0] != 1 ||
        input->dims->data[1] != INPUT_HEIGHT ||
        input->dims->data[2] != INPUT_WIDTH ||
        input->dims->data[3] != 3) {
        LOG_WRN("unexpected input shape, proceeding");
    }

    if (input->type != kTfLiteInt8) {
        LOG_WRN("expected int8 input tensor (type=%d)", input->type);
    }

    size_t input_bytes = INPUT_HEIGHT * INPUT_WIDTH * 3;
    memcpy(input_buf, reinterpret_cast<const void *>(INPUT_FLASH_ADDRESS), sizeof(input_buf));
    infer_preprocess(input_buf, INPUT_WIDTH, INPUT_HEIGHT);
    memcpy(input->data.int8, input_buf, input_bytes);

    LOG_INF("running inference");
    uint32_t t0 = k_uptime_get_32();
    if (interpreter.Invoke() != kTfLiteOk) {
        LOG_ERR("Invoke() failed");
        return -1;
    }
    uint32_t t1 = k_uptime_get_32();
    LOG_INF("Invoke done in %u ms", (unsigned)(t1 - t0));

    TfLiteTensor *out_box1   = interpreter.output(0);
    TfLiteTensor *out_scores = interpreter.output(1);
    TfLiteTensor *out_box2   = interpreter.output(2);

    detection_t detections[MAX_DETECTIONS];
    int det_count = 0;
    int ret = infer_postprocess(
        out_box1->data.int8,   (size_t)out_box1->bytes,
        out_scores->data.int8, (size_t)out_scores->bytes,
        out_box2->data.int8,   (size_t)out_box2->bytes,
        MAX_DETECTIONS, detections, &det_count);

    if (ret != 0) {
        LOG_ERR("infer_postprocess failed: %d", ret);
        return -1;
    }

    LOG_INF("Person Detections: %d", det_count);
    for (int i = 0; i < det_count; ++i) {
        LOG_INF("class=%d score=%.3f box=[%.1f,%.1f,%.1f,%.1f]",
                detections[i].class_id, (double)detections[i].score,
                (double)detections[i].x, (double)detections[i].y,
                (double)detections[i].w, (double)detections[i].h);
    }
    if (det_count == 0) {
        LOG_INF("No persons detected (confidence threshold 0.25)");
    }

    return 0;
}
