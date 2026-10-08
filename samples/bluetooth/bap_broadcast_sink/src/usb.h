/* Copyright (c) 2026 Synaptics Incorporated — SR110 stub: no USB audio on SR110 */
#ifndef SAMPLE_BAP_BROADCAST_SINK_USB_H
#define SAMPLE_BAP_BROADCAST_SINK_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/audio/audio.h>

#define USB_SAMPLE_RATE_HZ 48000U

int usb_add_frame_to_usb(enum bt_audio_location chan_allocation, const int16_t *frame,
                          size_t frame_size, uint32_t ts);
void usb_clear_frames_to_usb(void);
int usb_init(void);

#endif /* SAMPLE_BAP_BROADCAST_SINK_USB_H */
