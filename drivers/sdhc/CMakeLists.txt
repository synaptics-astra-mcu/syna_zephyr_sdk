# Copyright 2026 Synaptics, Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_SDHC_MSHC sdhc_mshc.c)

set_property(TARGET drivers__sdhc PROPERTY ALLOW_EMPTY TRUE)
