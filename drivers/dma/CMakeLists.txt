# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_DMA_ARM350 dma_arm350.c)
set_property(TARGET drivers__dma PROPERTY ALLOW_EMPTY TRUE)
