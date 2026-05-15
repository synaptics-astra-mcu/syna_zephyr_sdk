# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/watchdog.h)

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_WDT_ARM_APB wdt_arm_apb.c)
zephyr_library_sources_ifdef(CONFIG_WDT_SYNA wdt_syna.c)
