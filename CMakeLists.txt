#-------------------------------------------------------------------------------
# Synaptics Zephyr Example Application
#
# Copyright (c) 2025 Synaptics, Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(include)

add_subdirectory(drivers)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/)

zephyr_include_directories(${ZEPHYR_BASE}/drivers/usb/udc)
zephyr_include_directories(${ZEPHYR_BASE}/drivers/usb/common)
