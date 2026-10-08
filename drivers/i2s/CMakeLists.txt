# Copyright (c) 2026 Synaptics, Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_I2S_SYNA_SR100
	i2s_syna_sr100.c
)

zephyr_include_directories_ifdef(CONFIG_I2S_SYNA_SR100
	${CMAKE_CURRENT_SOURCE_DIR}/../../soc/syna/astra_sr/sr100
)
