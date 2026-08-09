# Copyright 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
zephyr_library_sources_ifdef(CONFIG_WIFI_SYNA wifi.c)
zephyr_library_sources_ifdef(CONFIG_WIFI_SYNA wifi_hal.c)
zephyr_library_sources_ifdef(CONFIG_WIFI_SYNA wifi_shell.c)
