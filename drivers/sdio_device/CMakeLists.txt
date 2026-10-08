# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${CMAKE_CURRENT_SOURCE_DIR}/../../include/zephyr/drivers/sdio_device.h)

zephyr_library()
zephyr_library_sources(sdio_device.c)
zephyr_library_sources_ifdef(CONFIG_SYNA_SDIO_DEVICE syna_sdio_device.c)
