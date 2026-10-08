/*
 * Copyright 2022 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define OUTPUT_ATTR __aligned(4) __attribute__((section(".rodata.tflm_output")))

OUTPUT_ATTR const uint8_t expectedOutputData[] = {
	0x80, 0xba, 0x80, 0x81, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x45
};
