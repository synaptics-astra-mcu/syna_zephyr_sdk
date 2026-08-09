# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/counter.h)

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_COUNTER_CORSTONE_SYSCOUNTER system_counter_corstone.c)
zephyr_library_sources_ifdef(CONFIG_COUNTER_CORSTONE_TIMER      timer_corstone.c)
