# Copyright (C) 2025 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_HWINFO_SYNA_SR100 hwinfo_syna_sr100.c)
zephyr_library_sources_ifdef(CONFIG_HWINFO_SYNA_SL261X hwinfo_syna_sl261x.c)
