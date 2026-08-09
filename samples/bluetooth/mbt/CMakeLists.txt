# Copyright (c) 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbt)

# Include paths for platform layer and mbt headers
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform_sr110
)

# Application sources
target_sources(app PRIVATE
    main.c
    btapp_mfg.c
    shell_mbt.c
)

# Platform transport (Zephyr-native HCI UART) and GPIO control
target_sources(app PRIVATE
    ../platform_sr110/pf_trans_sr110.c
    ../platform_sr110/upio_sr110.c
)

# BT firmware (SYNA) — from modules/hal_synaptics/bt/bt-firmware/
target_sources(app PRIVATE
    ../../../modules/hal_synaptics/bt/bt-firmware/syn4381/SYNA4381A1_001.001.029.1197.1230.c
)
