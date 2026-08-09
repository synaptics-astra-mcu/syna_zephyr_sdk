/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>
/* hw_codec.h is found via the include path set in CMakeLists.txt */
#include "hw_codec.h"

int hw_codec_open(void) { return 0; }
int hw_codec_cfg(uint32_t samplerate) { (void)samplerate; return 0; }
uint32_t hw_codec_write_data(const uint8_t *data, uint32_t len) { (void)data; return len; }
int hw_codec_close(void) { return 0; }
