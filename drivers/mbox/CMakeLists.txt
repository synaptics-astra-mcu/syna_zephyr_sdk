# Copyright (C) 2026 Synaptics Incorporated. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/mbox.h)

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_MBOX_SYNA_SR100 mbox_syna_sr100.c)

set_property(TARGET drivers__mbox PROPERTY ALLOW_EMPTY TRUE)
