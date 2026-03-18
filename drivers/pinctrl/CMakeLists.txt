# Copyright (c) 2025 Synaptics, Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PINCTRL_SYNA_SR100 pinctrl_syna_sr100.c)
zephyr_library_sources_ifdef(CONFIG_PINCTRL_SYNA_SL261X pinctrl_syna_sl261x.c)
