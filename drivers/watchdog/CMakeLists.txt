# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/watchdog.h)

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_WDT_ARM_APB wdt_arm_apb.c)
zephyr_library_sources_ifdef(CONFIG_WDOG_CMSDK_SYNA wdt_cmsdk_syna.c)
zephyr_library_sources_ifdef(CONFIG_WDOG_CORSTONE wdt_corstone.c)
zephyr_library_sources_ifdef(CONFIG_WDT_SYNA wdt_syna.c)
