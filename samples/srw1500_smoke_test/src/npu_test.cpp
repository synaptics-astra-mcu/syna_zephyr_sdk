/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "npu_test.h"

#include "inference_process.hpp"

#include <errno.h>
#include <stdio.h>
#include <string>
#include <vector>

#include <zephyr/kernel.h>

#include "input.h"
#include "model.h"
#include "output.h"

using namespace InferenceProcess;

#define NUM_INFERENCE_TASKS 1

#define TENSOR_ARENA_SIZE_PER_INFERENCE TENSOR_ARENA_SIZE

namespace
{
constexpr size_t arenaSize = TENSOR_ARENA_SIZE_PER_INFERENCE;

TENSOR_ARENA_ATTR uint8_t tensorArena[arenaSize];
}

extern "C" int npu_tflm_ethosu_test(void)
{
	InferenceJob job(modelName,
			 DataPtr(const_cast<uint8_t *>(networkModelData), sizeof(networkModelData)),
			 { DataPtr(const_cast<uint8_t *>(inputData), sizeof(inputData)) }, {},
			 { DataPtr(const_cast<uint8_t *>(expectedOutputData),
				   sizeof(expectedOutputData)) });
	InferenceProcess::InferenceProcess inferenceProcess(tensorArena, arenaSize);

	printf("Running TFLM Ethos-U model: %s\n", modelName);

	if (inferenceProcess.runJob(job)) {
		printf("TFLM Ethos-U inference failed\n");
		return -EIO;
	}

	printf("TFLM Ethos-U inference passed\n");
	return 0;
}
