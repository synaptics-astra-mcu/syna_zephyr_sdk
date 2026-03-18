# Copyright 2025 Synaptics, Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(${ZEPHYR_BASE}/drivers/flash/)
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_FLASH_CAD_XSPI flash_cadence_xspi.c)
