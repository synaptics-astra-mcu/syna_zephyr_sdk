/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usb.h"

int usb_init(void) { return 0; }

int usb_add_frame_to_usb(enum bt_audio_location chan_allocation, const int16_t *frame,
                          size_t frame_size, uint32_t ts)
{
	(void)chan_allocation; (void)frame; (void)frame_size; (void)ts;
	return 0;
}

void usb_clear_frames_to_usb(void) {}
