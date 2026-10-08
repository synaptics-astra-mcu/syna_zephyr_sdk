/**
 * Copyright (c) 2026 Synaptics Incorporated
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file syna_wifi_sc_filter.c
 *
 * Scan-result filter pipeline for the Synaptics
 * (MHD) wpa_supplicant port.
 *
 * Compiled only when SC_RESULT_FILTER is defined.  When the symbol is absent
 * the lightweight inline stubs in syna_wifi_sc_filter.h are used instead
 * and this file need not be linked (control via CMakeLists.txt).
 */

#ifdef SC_RESULT_FILTER

#include "includes.h"
#include "syna_wifi.h"
#include "syna_wifi_sc_filter.h"

/* MACDBG / MAC2STRDBG may already be defined via a common header; guard
 * against redefinition.
 */
#ifndef MACDBG
#define MACDBG "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STRDBG(ea)  ((const uint8 *)(ea))[0], \
                        ((const uint8 *)(ea))[1], \
                        ((const uint8 *)(ea))[2], \
                        ((const uint8 *)(ea))[3], \
                        ((const uint8 *)(ea))[4], \
                        ((const uint8 *)(ea))[5]
#endif

/* Global scan results list -- defined in supplicant_mhd_adaptor.c */
extern struct dl_list drv_scan_results_list;

/* ------------------------------------------------------------------ */
/* Runtime-adjustable filter thresholds                               */
/* ------------------------------------------------------------------ */

/*
 * Per-band RSSI thresholds (dBm).  Entries below the threshold are
 * discarded before allocation.
 *
 * 2.4G: -75  cap never saturates; -75 dBm is below MCS1 usability.
 * 5G:   -75  sort-evict already floors the kept list near -72 dBm;
 *             pre-filter saves band-list work for entries that would be
 *             discarded anyway.
 * 6G:   -85  fewest APs per scan (~5/run); observed floor is -78 dBm;
 *             -85 gives 7 dBm safety margin.
 */
#define DEFAULT_SCAN_FILTER_RSSI_2G (-75)
#define DEFAULT_SCAN_FILTER_RSSI_5G (-75)
#define DEFAULT_SCAN_FILTER_RSSI_6G (-85)

/* Array indexed [0]=2.4G [1]=5G [2]=6G */
static int32_t sc_filter_rssi_per_band[3] = {
    DEFAULT_SCAN_FILTER_RSSI_2G,
    DEFAULT_SCAN_FILTER_RSSI_5G,
    DEFAULT_SCAN_FILTER_RSSI_6G,
};

#define DEFAULT_SCAN_FILTER_HIDDEN_SSID      1
#define DEFAULT_SCAN_RSSI_TAIL_DROP_PERCENT 30

static int          sc_filter_hidden_ssid     = DEFAULT_SCAN_FILTER_HIDDEN_SSID;
static unsigned int sc_rssi_tail_drop_percent = DEFAULT_SCAN_RSSI_TAIL_DROP_PERCENT;

/* Per-scan counters */
static uint32_t sc_filtered_hidden_ssid_cnt = 0;
static uint32_t sc_filtered_duplicate_cnt   = 0;
static uint32_t sc_filtered_low_rssi_cnt    = 0;
static uint32_t sc_dropped_tail_rssi_cnt    = 0;

/* Captured during syna_sc_drop_tails() for use in syna_sc_log_completion() */
static unsigned int sc_pre_drop_count[3]; /* [0]=2G [1]=5G [2]=6G */
static unsigned int sc_drop_count[3];

/*
 * Per-band scan result lists
 */

static DEFINE_DL_LIST(syna_scan_results_2g);
static DEFINE_DL_LIST(syna_scan_results_5g);
static DEFINE_DL_LIST(syna_scan_results_6g);

/*
 * Internal helpers
 */

static inline int syna_band_rssi_idx(chanspec_band_t band)
{
    switch (band) {
    case MHD_CHANSPEC_BAND_2G: return 0;
    case MHD_CHANSPEC_BAND_5G: return 1;
    case MHD_CHANSPEC_BAND_6G: return 2;
    default:                   return 1; /* unknown -- treat as 5G */
    }
}

static struct dl_list *syna_scan_band_list(chanspec_band_t band, int freq)
{
    switch (band) {
    case MHD_CHANSPEC_BAND_2G: return &syna_scan_results_2g;
    case MHD_CHANSPEC_BAND_5G: return &syna_scan_results_5g;
    case MHD_CHANSPEC_BAND_6G: return &syna_scan_results_6g;
    default:
        if (freq >= 2412 && freq <= 2484) return &syna_scan_results_2g;
        if (freq >= 5955 && freq <= 7125) return &syna_scan_results_6g;
        return &syna_scan_results_5g;
    }
}

static void syna_scan_clear_list(struct dl_list *list)
{
    drv_scan_result_t *entry;

    while ((entry = dl_list_first(list, drv_scan_result_t, list)) != NULL) {
        dl_list_del(&entry->list);
        sc_filter_free_entry(entry);
    }
}

static int syna_scan_list_has_bssid(struct dl_list *list, const u8 *bssid)
{
    drv_scan_result_t *e;

    dl_list_for_each(e, list, drv_scan_result_t, list)
        if (os_memcmp(e->bssid, bssid, ETH_ALEN) == 0)
            return 1;
    return 0;
}

static void syna_scan_add_sorted_by_rssi(struct dl_list *list,
                                         drv_scan_result_t *scan_res)
{
    drv_scan_result_t *existing;

    dl_list_for_each(existing, list, drv_scan_result_t, list) {
        if (scan_res->level > existing->level) {
            dl_list_add(existing->list.prev, &scan_res->list);
            return;
        }
    }
    dl_list_add_tail(list, &scan_res->list);
}

static void syna_scan_move_list_to_global(struct dl_list *list)
{
    drv_scan_result_t *entry;

    while ((entry = dl_list_first(list, drv_scan_result_t, list)) != NULL) {
        dl_list_del(&entry->list);
        dl_list_add_tail(&drv_scan_results_list, &entry->list);
    }
}

static unsigned int syna_scan_drop_tail_percent(const char *band_name,
                                                struct dl_list *list,
                                                unsigned int percent)
{
    drv_scan_result_t *entry, *tmp;
    unsigned int count = dl_list_len(list);
    unsigned int drop;
    unsigned int keep;
    unsigned int index = 0;
    unsigned int dropped = 0;

    if (percent > 100)
        percent = 100;

    drop = (percent == 0 || count <= 1) ? 0 : count * percent / 100;
    if (drop >= count && count > 0)
        drop = count - 1;
    keep = count - drop;

    wpa_printf(MSG_MSGDUMP,
               "sc_filter: %s sorted count=%u keep=%u drop=%u tail_pct=%u",
               band_name, count, keep, drop, percent);

    dl_list_for_each(entry, list, drv_scan_result_t, list) {
        if (drop > 0 && index == keep)
            wpa_printf(MSG_MSGDUMP,
                       "sc_filter: %s ------- dropped below --------",
                       band_name);
        wpa_printf(MSG_MSGDUMP,
                   "sc_filter: %s [%u] SSID=\"%.*s\" BSSID=" MACDBG
                   " freq=%d RSSI=%d",
                   band_name, index,
                   (int)entry->ssids.ssid_len,
                   entry->ssids.ssid ? (const char *)entry->ssids.ssid : "",
                   MAC2STRDBG(entry->bssid),
                   entry->freq, entry->level);
        index++;
    }

    if (drop == 0)
        return 0;

    index = 0;
    dl_list_for_each_safe(entry, tmp, list, drv_scan_result_t, list) {
        index++;
        if (index <= keep)
            continue;
        dl_list_del(&entry->list);
        sc_filter_free_entry(entry);
        dropped++;
    }
    return dropped;
}

static int syna_scan_is_hidden_ssid(const u8 *ssid, size_t ssid_len)
{
    size_t i;

    if (ssid_len == 0 || !ssid)
        return 1;
    for (i = 0; i < ssid_len; i++)
        if (ssid[i] != '\0')
            return 0;
    return 1;
}

static int syna_scan_has_specific_ssid(const struct wpa_driver_scan_params *scan)
{
    size_t i;

    if (!scan)
        return 0;
    for (i = 0; i < scan->num_ssids && i < WPAS_MAX_SCAN_SSIDS; i++)
        if (scan->ssids[i].ssid && scan->ssids[i].ssid_len > 0)
            return 1;
    return 0;
}

sc_filter_result_t syna_sc_filter_entry(const wl_bss_info_t *bi,
                                        chanspec_band_t band)
{
    size_t ssid_len = (size_t)MIN(bi->SSID_len, sizeof(bi->SSID));
    int32_t filter_rssi = sc_filter_rssi_per_band[syna_band_rssi_idx(band)];

    if (sc_filter_hidden_ssid &&
        syna_scan_is_hidden_ssid((const u8 *)bi->SSID, ssid_len)) {
        sc_filtered_hidden_ssid_cnt++;
        wpa_printf(MSG_MSGDUMP,
                   "sc_filter: hidden SSID BSSID=" MACDBG
                   " RSSI=%d filtered_hidden=%lu",
                   MAC2STRDBG(&bi->BSSID), bi->RSSI,
                   (unsigned long)sc_filtered_hidden_ssid_cnt);
        return SC_FILTER_DISCARD;
    }

    if (syna_scan_list_has_bssid(&syna_scan_results_2g, (const u8 *)&bi->BSSID) ||
        syna_scan_list_has_bssid(&syna_scan_results_5g, (const u8 *)&bi->BSSID) ||
        syna_scan_list_has_bssid(&syna_scan_results_6g, (const u8 *)&bi->BSSID)) {
        sc_filtered_duplicate_cnt++;
        wpa_printf(MSG_MSGDUMP,
                   "sc_filter: duplicate SSID=\"%.*s\" BSSID=" MACDBG
                   " RSSI=%d filtered_dup=%lu",
                   (int)ssid_len, bi->SSID,
                   MAC2STRDBG(&bi->BSSID), bi->RSSI,
                   (unsigned long)sc_filtered_duplicate_cnt);
        return SC_FILTER_DISCARD;
    }

    if (filter_rssi && bi->RSSI < filter_rssi) {
        sc_filtered_low_rssi_cnt++;
        wpa_printf(MSG_MSGDUMP,
                   "sc_filter: low RSSI SSID=\"%.*s\" BSSID=" MACDBG
                   " RSSI=%d threshold=%ld filtered=%lu",
                   (int)ssid_len, bi->SSID,
                   MAC2STRDBG(&bi->BSSID), bi->RSSI,
                   (long)filter_rssi,
                   (unsigned long)sc_filtered_low_rssi_cnt);
        return SC_FILTER_DISCARD;
    }

    return SC_FILTER_KEEP;
}

sc_queue_result_t syna_sc_queue_result(drv_scan_result_t *scan_res,
                                       chanspec_band_t band)
{
    struct dl_list *band_list = syna_scan_band_list(band, scan_res->freq);

    if ((unsigned int)dl_list_len(band_list) >= MAX_BAND_SCAN_RESULTS) {
        /*
         * Band list is full.  Compare the new entry against the weakest
         * currently kept entry (list tail, sorted strongest-first).
         *
         * New AP stronger  -> evict tail, insert new.  Total count unchanged.
         * New AP not stronger -> discard new entry silently.
         */
        drv_scan_result_t *weakest = dl_list_last(band_list, drv_scan_result_t, list);

        if (!weakest || scan_res->level <= weakest->level) {
            wpa_printf(MSG_MSGDUMP,
                       "sc_filter: band full (weaker) SSID=\"%.*s\""
                       " BSSID=" MACDBG " RSSI=%d weakest_kept=%d"
                       " band_count=%u band_max=%u",
                       (int)scan_res->ssids.ssid_len,
                       scan_res->ssids.ssid
                           ? (const char *)scan_res->ssids.ssid : "",
                       MAC2STRDBG(scan_res->bssid),
                       scan_res->level,
                       weakest ? weakest->level : 0,
                       (unsigned int)dl_list_len(band_list),
                       MAX_BAND_SCAN_RESULTS);
            sc_filter_free_entry(scan_res);
            return SC_QUEUE_DISCARDED;
        }

        wpa_printf(MSG_MSGDUMP,
                   "sc_filter: band evict weakest SSID=\"%.*s\""
                   " BSSID=" MACDBG " RSSI=%d evicted_rssi=%d"
                   " band_count=%u band_max=%u",
                   (int)scan_res->ssids.ssid_len,
                   scan_res->ssids.ssid
                       ? (const char *)scan_res->ssids.ssid : "",
                   MAC2STRDBG(scan_res->bssid),
                   scan_res->level, weakest->level,
                   (unsigned int)dl_list_len(band_list),
                   MAX_BAND_SCAN_RESULTS);
        dl_list_del(&weakest->list);
        sc_filter_free_entry(weakest);
        syna_scan_add_sorted_by_rssi(band_list, scan_res);
        return SC_QUEUE_EVICTED;
    }

    syna_scan_add_sorted_by_rssi(band_list, scan_res);
    return SC_QUEUE_ADDED;
}

void syna_sc_filter_scan_init(const struct wpa_driver_scan_params *scan)
{
    sc_filtered_hidden_ssid_cnt = 0;
    sc_filtered_duplicate_cnt   = 0;
    sc_filtered_low_rssi_cnt    = 0;
    sc_dropped_tail_rssi_cnt    = 0;

    sc_filter_rssi_per_band[0] = DEFAULT_SCAN_FILTER_RSSI_2G;
    sc_filter_rssi_per_band[1] = DEFAULT_SCAN_FILTER_RSSI_5G;
    sc_filter_rssi_per_band[2] = DEFAULT_SCAN_FILTER_RSSI_6G;
    sc_rssi_tail_drop_percent  = DEFAULT_SCAN_RSSI_TAIL_DROP_PERCENT;

    sc_filter_hidden_ssid = syna_scan_has_specific_ssid(scan)
                          ? 0 : DEFAULT_SCAN_FILTER_HIDDEN_SSID;

    syna_scan_clear_list(&syna_scan_results_2g);
    syna_scan_clear_list(&syna_scan_results_5g);
    syna_scan_clear_list(&syna_scan_results_6g);

    os_memset(sc_pre_drop_count, 0, sizeof(sc_pre_drop_count));
    os_memset(sc_drop_count, 0, sizeof(sc_drop_count));
}

/*
 * Tail-drop is only activated when the total kept count exceeds
 * MAX_SCAN_RESULTS * (100 - drop_pct) / 100.
 *
 * A single knob (sc_rssi_tail_drop_percent) controls both the gate and
 * the amount dropped:
 *   drop_pct=30  =>  keep threshold = 64 * 70% = 44
 *                    if total > 44: drop 30% tail from each band
 *                    if total <= 44: memory budget is fine, keep all
 */
void syna_sc_drop_tails(void)
{
    unsigned int total;
    unsigned int threshold;

    sc_pre_drop_count[0] = dl_list_len(&syna_scan_results_2g);
    sc_pre_drop_count[1] = dl_list_len(&syna_scan_results_5g);
    sc_pre_drop_count[2] = dl_list_len(&syna_scan_results_6g);
    total = sc_pre_drop_count[0] + sc_pre_drop_count[1] + sc_pre_drop_count[2];
    threshold = MAX_SCAN_RESULTS * (100 - sc_rssi_tail_drop_percent) / 100;

    if (total <= threshold) {
        /* Clean environment: total fits within memory budget, keep all. */
        wpa_printf(MSG_MSGDUMP,
                   "sc_filter: drop_tails skipped"
                   " total=%u <= threshold=%u (keep all)",
                   total, threshold);
        return;
    }

    wpa_printf(MSG_MSGDUMP,
               "sc_filter: drop_tails active"
               " total=%u > threshold=%u drop_pct=%u",
               total, threshold, sc_rssi_tail_drop_percent);

    sc_drop_count[0] = syna_scan_drop_tail_percent("2G", &syna_scan_results_2g,
                                                    sc_rssi_tail_drop_percent);
    sc_drop_count[1] = syna_scan_drop_tail_percent("5G", &syna_scan_results_5g,
                                                    sc_rssi_tail_drop_percent);
    sc_drop_count[2] = syna_scan_drop_tail_percent("6G", &syna_scan_results_6g,
                                                    sc_rssi_tail_drop_percent);

    sc_dropped_tail_rssi_cnt += sc_drop_count[0] + sc_drop_count[1] + sc_drop_count[2];
}

void syna_sc_log_completion(uint32_t status, uint32_t total_kept)
{
    unsigned int report_2g    = dl_list_len(&syna_scan_results_2g);
    unsigned int report_5g    = dl_list_len(&syna_scan_results_5g);
    unsigned int report_6g    = dl_list_len(&syna_scan_results_6g);
    unsigned int report_total = report_2g + report_5g + report_6g;

    if (status == HOST_SCAN_COMPLETED)
        wpa_printf(MSG_MSGDUMP,
                   "sup_adap_scan_cb: scan completed total=%u 2G=%u 5G=%u 6G=%u",
                   report_total, report_2g, report_5g, report_6g);
    else if (status == HOST_SCAN_ABORTED)
        wpa_printf(MSG_MSGDUMP,
                   "sup_adap_scan_cb: scan aborted total=%u 2G=%u 5G=%u 6G=%u",
                   report_total, report_2g, report_5g, report_6g);

    wpa_printf(MSG_MSGDUMP,
               "sup_adap_scan_cb: scan detail"
               " keep=%lu"
               " band_2g=%u band_5g=%u band_6g=%u"
               " tail_drop_pct=%u"
               " tail_drop_2g=%u tail_drop_5g=%u tail_drop_6g=%u"
               " report_2g=%u report_5g=%u report_6g=%u"
               " filtered_hidden=%lu filtered_duplicate=%lu"
               " filtered_low_rssi=%lu dropped_tail_rssi=%lu",
               (unsigned long)total_kept,
               sc_pre_drop_count[0], sc_pre_drop_count[1], sc_pre_drop_count[2],
               sc_rssi_tail_drop_percent,
               sc_drop_count[0], sc_drop_count[1], sc_drop_count[2],
               report_2g, report_5g, report_6g,
               (unsigned long)sc_filtered_hidden_ssid_cnt,
               (unsigned long)sc_filtered_duplicate_cnt,
               (unsigned long)sc_filtered_low_rssi_cnt,
               (unsigned long)sc_dropped_tail_rssi_cnt);
}

void syna_sc_flush_to_global(void)
{
    syna_scan_move_list_to_global(&syna_scan_results_2g);
    syna_scan_move_list_to_global(&syna_scan_results_5g);
    syna_scan_move_list_to_global(&syna_scan_results_6g);
}

#endif /* SC_RESULT_FILTER */
