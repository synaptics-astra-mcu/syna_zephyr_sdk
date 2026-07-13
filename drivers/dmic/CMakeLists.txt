#
# Copyright 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_AUDIO_DMIC_SYNA_SR100 dmic_syna_sr100.c)
