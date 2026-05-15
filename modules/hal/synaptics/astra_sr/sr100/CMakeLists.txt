# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/includes)
if(CONFIG_VIDEO_SYNA_MIPI OR CONFIG_VIDEO_SYNA_ENC)
  zephyr_library_sources(sdk_hooks.c)
endif()
zephyr_library_property(ALLOW_EMPTY TRUE)
