# Copyright (c) 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbt)

set(SRSDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

# Include paths for platform layer and mbt headers
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${SRSDK_ROOT}/samples/bluetooth/platform_sr110"
)

# Application sources
target_sources(app PRIVATE
    main.c
    btapp_mfg.c
    shell_mbt.c
)

# Platform transport (Zephyr-native HCI UART) and GPIO control
target_sources(app PRIVATE
    "${SRSDK_ROOT}/samples/bluetooth/platform_sr110/pf_trans_sr110.c"
    "${SRSDK_ROOT}/samples/bluetooth/platform_sr110/upio_sr110.c"
)

# BT firmware (SYNA) — selected by Kconfig chip choice
if(CONFIG_BT_4612)
    target_sources(app PRIVATE
        "${SRSDK_ROOT}/modules/hal_synaptics/bt/bt-firmware/syn4612/SYN4612A1_001.002.003.0020.0000.c"
    )
elseif(CONFIG_BT_4381)
    target_sources(app PRIVATE
        "${SRSDK_ROOT}/modules/hal_synaptics/bt/bt-firmware/syn4381/SYNA4381A1_001.001.029.1197.1230.c"
    )
else()
    message(FATAL_ERROR "Select BT firmware chip in Kconfig: enable CONFIG_BT_4381 or CONFIG_BT_4612")
endif()
