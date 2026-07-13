# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/reset.h)

zephyr_library()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_RESET_SYNA_SL reset_syna_sl.c)
zephyr_library_sources_ifdef(CONFIG_RESET_SYNA_SL261X reset_syna_sl261x.c)
# zephyr-keep-sorted-stop

set_property(TARGET drivers__reset PROPERTY ALLOW_EMPTY TRUE)
