# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/includes)
zephyr_library_sources_ifdef(CONFIG_VIDEO_SYNA_MIPI sdk_hooks.c)
zephyr_library_property(ALLOW_EMPTY TRUE)
