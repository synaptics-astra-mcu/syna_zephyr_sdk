/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#ifndef DRIVER_SYNA_PLATFORM_H_
#define DRIVER_SYNA_PLATFORM_H_

#include "syna_wifi.h"
#include "supp_drv_platform_headers.h"

struct wpa_supplicant;

#ifndef ZEPHYR_OS
#include "sup_adap_task.h"
#include "rsn_supp/wpa.h"
#endif

void *driver_syna_init_common(void *ctx, const char *ifname,
                              const void *platform_data);
void driver_syna_deinit(void *priv);

#ifdef ZEPHYR_OS
void *driver_syna_zephyr_init(void *ctx, const char *ifname,
                              struct zep_wpa_supp_dev_callbk_fns *cbs);
int sup_adap_register_mhd_event_handler(driver_syna_data_t *drv,
                                        mhd_interface_t interface);
int sup_adap_unregister_mhd_event_handler(driver_syna_data_t *drv,
                                          mhd_interface_t interface);
#else
void *driver_syna_init(void *ctx, const char *ifname);
#endif

int drv_syna_register_transport(driver_syna_data_t *drv);
int drv_syna_post_init(driver_syna_data_t *drv);
void drv_syna_pre_deinit(driver_syna_data_t *drv);
void drv_syna_notify_scan_started(driver_syna_data_t *drv);
void drv_syna_prepare_assoc_bandwidth(struct wpa_supplicant *wpa_s,
                                      const u8 *target_bssid,
                                      struct wpa_driver_associate_params *params);
int drv_syna_validate_key_install(int deleting, int is_pairwise,
                                  struct wpa_driver_set_key_params *params);

#ifdef ZEPHYR_OS
int drv_syna_store_platform_init(driver_syna_data_t *drv,
                                 const void *platform_data);
#else
static inline int drv_syna_store_platform_init(driver_syna_data_t *drv,
                                               const void *platform_data)
{
    (void)drv;
    (void)platform_data;
    return 0;
}
#endif

#endif