# Copyright 2025 Synaptics, Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_CLOCK_CONTROL_SYNA_SR100 clock_control_syna_sr100.c)
zephyr_library_sources_ifdef(CONFIG_CLOCK_CONTROL_SYNA_SL261X clock_control_syna_sl261x.c)

set_property(TARGET drivers__clock_control PROPERTY ALLOW_EMPTY TRUE)
