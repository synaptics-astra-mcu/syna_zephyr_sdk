/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_LP_ENC_H_
#define SYNA_LP_ENC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*
 * LP JPEG encoder configuration interface.
 *
 * This header is encoder-focused (LP JPEG) and intentionally avoids exposing
 * broader LP Sense pipeline types.
 */

#ifndef SYNA_LP_ENC_CB_F_DEFINED
#define SYNA_LP_ENC_CB_F_DEFINED
typedef void (*lp_enc_cb_f)(uint32_t lps_status, uint32_t lps_dma,
			    uint32_t dma_status, uint32_t fvf_status1);
#endif /* SYNA_LP_ENC_CB_F_DEFINED */

struct lp_enc_frame_cfg {
	uint16_t width;
	uint16_t height;
	uint32_t raw_offset;
	uint32_t jpeg_size_limit;
};

struct lp_enc_mipi_input_cfg {
	/* CSI receiver instance id. */
	uint32_t csi_id;
	/* CSI receiver format code (e.g. RAW8 vs RAW10 selection). */
	uint32_t code;
	/* Number of CSI data lanes. */
	uint32_t lanes;
	/* CSI lane rate in kbps. */
	uint32_t lane_rate_kbps;
	/* CSI timing selector (SoC-specific). */
	uint32_t timing;
	/* CSI interface selector (SoC-specific). */
	uint32_t interface;
	/* CSI virtual channel. */
	uint32_t virt_ch;
	/* CSI auto-flush enable (0/1). */
	uint32_t auto_flush;
};

struct lp_enc_memory_buffer_cfg {
	uint32_t src_offset;
	uint32_t dst_offset;
};

struct lp_enc_mipi_cfg {
	struct lp_enc_frame_cfg frame;
	lp_enc_cb_f callback;
	struct lp_enc_mipi_input_cfg input;
};

struct lp_enc_mem_cfg {
	struct lp_enc_frame_cfg frame;
	lp_enc_cb_f callback;
	struct lp_enc_memory_buffer_cfg memory;
};

bool lp_enc_is_valid_size(uint32_t width, uint32_t height);
bool lp_enc_init(void);

/*
 * `lp_enc_stop()` stops active processing/streaming but keeps configuration
 * state intact so a subsequent `lp_enc_start()` can resume.
 *
 * `lp_enc_deinit()` fully tears down the LP encoder datapath and discards the
 * active LP encoder configuration that was previously set up by init/config.
 */
bool lp_enc_deinit(void);
bool lp_enc_config_input(const struct lp_enc_mipi_cfg *cfg);
bool lp_enc_config_memory(const struct lp_enc_mem_cfg *cfg);
bool lp_enc_start(void);
bool lp_enc_stop(void);

/*
 * Retrigger is only valid after `lp_enc_config_memory()` because the hardware
 * retrigger path is used for LP memory-loop mode.
 */
bool lp_enc_retrigger(void);

#ifdef __cplusplus
}
#endif

#endif /* SYNA_LP_ENC_H_ */
