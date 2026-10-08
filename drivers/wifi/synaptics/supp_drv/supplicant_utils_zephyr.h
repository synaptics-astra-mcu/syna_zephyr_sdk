/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#ifndef SUPPLICANT_UTILS_ZEPHYR_H_
#define SUPPLICANT_UTILS_ZEPHYR_H_

#ifdef ZEPHYR_OS

#include "drivers/driver.h"
#include "mhd_wifi_api.h"

struct hostapd_hw_modes;

static inline enum chan_width map_chanspec_bw_to_chan_width(chanspec_bw_t bw)
{
	switch (bw) {
	case MHD_CHANSPEC_BW_20:
		return CHAN_WIDTH_20;
	case MHD_CHANSPEC_BW_40:
		return CHAN_WIDTH_40;
	case MHD_CHANSPEC_BW_80:
		return CHAN_WIDTH_80;
	case MHD_CHANSPEC_BW_160:
		return CHAN_WIDTH_160;
	case MHD_CHANSPEC_BW_8080:
		return CHAN_WIDTH_80P80;
	default:
		return CHAN_WIDTH_UNKNOWN;
	}
}

int chan_width_to_mhz(enum chan_width width);
void supplicant_dump_ht_capab(const char *prefix,
			      const struct hostapd_hw_modes *mode, unsigned int band_idx);
void supplicant_dump_vht_capab(const char *prefix,
			       const struct hostapd_hw_modes *mode, unsigned int band_idx);
void supplicant_dump_he_capab(const char *prefix,
			      const struct hostapd_hw_modes *mode, unsigned int band_idx);
void supplicant_dump_eht_capab(const char *prefix,
			       const struct hostapd_hw_modes *mode, unsigned int band_idx);

#endif

#endif