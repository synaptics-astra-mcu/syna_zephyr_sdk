# Copyright 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_ARM_SSE_TIMER arm_sse_timer.c)
zephyr_library_property(ALLOW_EMPTY TRUE)
