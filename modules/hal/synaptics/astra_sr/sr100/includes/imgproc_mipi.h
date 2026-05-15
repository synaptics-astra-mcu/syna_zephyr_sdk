/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef IMGPROC_MIPI_H
#define IMGPROC_MIPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#ifndef IMGPROC_MIPI_FRAME_READY_CB_F_DEFINED
#define IMGPROC_MIPI_FRAME_READY_CB_F_DEFINED
typedef void (*frame_ready_cb_f)(uint32_t status);
#endif

typedef enum imgproc_mipi_video_format {
	IMGPROC_MIPI_VIDEO_FORMAT_RAW8 = 0,
	IMGPROC_MIPI_VIDEO_FORMAT_RAW10,
} imgproc_mipi_video_format_t;

typedef struct imgproc_mipi_cfg {
	uint32_t width;
	uint32_t height;
	/* CSI-2 per-lane serial bitrate in kbit/s (not the lane clock). */
	uint32_t lane_rate_kbps;
	uint8_t interface_width;
	imgproc_mipi_video_format_t format;
} imgproc_mipi_cfg_t;

/*
 * Stop and release the currently configured datapath for the selected CSI
 * instance without deinitializing the shared ImgProc context.
 *
 * This clears the wrapper's per-instance configured/started state so a later
 * configure call starts from a clean datapath state.
 */
bool imgproc_mipi_release(uint8_t csi_id);
bool imgproc_mipi_init(uint8_t csi_id);
bool imgproc_mipi_deinit(uint8_t csi_id);
bool imgproc_mipi_configure(uint8_t csi_id, imgproc_mipi_cfg_t *cfg, uint32_t dst_addr);
bool imgproc_mipi_start(uint8_t csi_id, frame_ready_cb_f cb);
bool imgproc_mipi_stop(uint8_t csi_id);
bool imgproc_mipi_trigger(uint8_t csi_id);
int imgproc_mipi_status(uint8_t csi_id);

#ifdef __cplusplus
}
#endif

#endif /* IMGPROC_MIPI_H */
