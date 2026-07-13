# Copyright (c) 2025 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PINCTRL_SYNA_SL261X pinctrl_syna_sl261x.c)
