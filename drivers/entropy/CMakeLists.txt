# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/entropy.h)

zephyr_library_sources_ifdef(CONFIG_ENTROPY_SYNA_BCM entropy_syna_bcm.c)
