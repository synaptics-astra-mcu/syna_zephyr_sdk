# Copyright 2025 Synaptics, Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_HWINFO_SYNA_SR100 hwinfo_syna_sr100.c)
