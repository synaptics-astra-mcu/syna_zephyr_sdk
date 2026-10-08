/**
 * Copyright (c) 2026 Synaptics Incorporated
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file syna_wifi_sc_filter.h
 * contains scan-result filter API
 */

#ifndef DRIVER_SYNA_SC_FILTER_H
#define DRIVER_SYNA_SC_FILTER_H

#include "syna_wifi.h" /* drv_scan_result_t, MAX_BAND_SCAN_RESULTS,
                          * struct wpa_driver_scan_params (via mhd_wifi_api.h /
                          * drivers/driver.h transitively included) */
#include "syna_wifi_msg.h" /* drv_scan_result_t, MAX_BAND_SCAN_RESULTS,
                               * MAX_SCAN_RESULTS */

/*
 * Queue result type -- always defined so both paths share the enum
 */

typedef enum {
    SC_QUEUE_ADDED,      /* Entry inserted into list; caller must sc_result_cnt++ */
    SC_QUEUE_EVICTED,    /* Band was full; new entry replaced weakest; total unchanged */
    SC_QUEUE_DISCARDED,  /* Entry freed internally (duplicate or too weak); no count change */
} sc_queue_result_t;

/*
 * Filter result type -- always defined so both paths share the enum
 */

typedef enum {
	SC_FILTER_KEEP,
	SC_FILTER_DISCARD,
} sc_filter_result_t;

extern struct dl_list drv_scan_results_list;

static inline void sc_filter_free_entry(drv_scan_result_t *entry)
{
    if (!entry)
        return;
    if (entry->ssids.ssid)
        os_free((void *)entry->ssids.ssid);
    os_free(entry);
}

#ifdef SC_RESULT_FILTER

/*
 * syna_sc_filter_entry -- pre-allocation filter check.
 *
 * Tests bi against hidden-SSID, duplicate-BSSID, and per-band RSSI rules.
 * Called before allocating drv_scan_result_t so rejected entries cost no heap.
 * Returns SC_FILTER_DISCARD if the entry should be dropped silently.
 */
sc_filter_result_t syna_sc_filter_entry(const wl_bss_info_t *bi,
                                        chanspec_band_t band);

/*
 * syna_sc_queue_result -- insert scan_res into the per-band sorted list.
 *
 * Always takes ownership of scan_res.  The caller must NOT touch scan_res
 * after this call regardless of the return value.
 *
 *   SC_QUEUE_ADDED    -- inserted normally; caller increments sc_result_cnt.
 *   SC_QUEUE_EVICTED  -- band was full; new entry replaced weakest; total count
 *                       is unchanged, caller does NOT increment sc_result_cnt.
 *   SC_QUEUE_DISCARDED -- entry was weaker than the current tail and has been
 *                        freed internally; caller does NOT increment sc_result_cnt.
 */
sc_queue_result_t syna_sc_queue_result(drv_scan_result_t *scan_res,
                                       chanspec_band_t band);

/*
 * syna_sc_filter_scan_init -- reset filter state at the start of each scan.
 *
 * Clears all counters, resets per-band RSSI thresholds to defaults, and
 * drains the three per-band lists.  Reads scan->ssids to decide whether
 * the hidden-SSID filter should be suppressed for a targeted scan.
 */
void syna_sc_filter_scan_init(const struct wpa_driver_scan_params *scan);

/*
 * syna_sc_drop_tails -- sort each band list by RSSI descending, then remove
 * the weakest tail_percent percent of each list.
 * Call once at HOST_SCAN_COMPLETED / HOST_SCAN_ABORTED before flushing.
 */
void syna_sc_drop_tails(void);

/*
 * syna_sc_log_completion -- emit the per-scan summary wpa_printf lines.
 * status is HOST_SCAN_COMPLETED or HOST_SCAN_ABORTED.
 * total_kept is sc_result_cnt from the caller (entries that passed pre-filter
 * and were accepted into a band list before tail-drop).
 */
void syna_sc_log_completion(uint32_t status, uint32_t total_kept);

/*
 * syna_sc_flush_to_global -- move all per-band list entries to the shared
 * drv_scan_results_list.  Call after syna_sc_drop_tails.
 */
void syna_sc_flush_to_global(void);

#else

/*
 * syna_sc_queue_result -- duplicate-safe add to the global list.
 *
 * Checks for an existing entry with the same BSSID.  If found, frees
 * scan_res and returns SC_QUEUE_DISCARDED.  Otherwise appends to the global
 * list and returns SC_QUEUE_ADDED.
 * Always takes ownership of scan_res.
 */
static inline sc_queue_result_t syna_sc_queue_result(drv_scan_result_t *scan_res,
						      chanspec_band_t band)
{
    drv_scan_result_t *e;
    (void)band;

    dl_list_for_each(e, &drv_scan_results_list, drv_scan_result_t, list) {
        if (os_memcmp(e->bssid, scan_res->bssid, ETH_ALEN) == 0) {
            sc_filter_free_entry(scan_res);
			return SC_QUEUE_DISCARDED;
		}
	}
    dl_list_add_tail(&drv_scan_results_list, &scan_res->list);
	return SC_QUEUE_ADDED;
}

/* Always keep: hidden/RSSI/duplicate checks are skipped without filter. */
static inline sc_filter_result_t syna_sc_filter_entry(
    const wl_bss_info_t *bi, chanspec_band_t band)
{
    (void)bi; (void)band;
	return SC_FILTER_KEEP;
}

/* No-op: sc_result_cnt reset is handled directly in syna_adapt_scan. */
static inline void syna_sc_filter_scan_init(
    const struct wpa_driver_scan_params *scan)
{
	(void)scan;
}

static inline void syna_sc_drop_tails(void) { }
static inline void syna_sc_log_completion(uint32_t status, uint32_t total_kept)
    { (void)status; (void)total_kept; }
static inline void syna_sc_flush_to_global(void) { }

#endif /* SC_RESULT_FILTER */

#endif /* DRIVER_SYNA_SC_FILTER_H */