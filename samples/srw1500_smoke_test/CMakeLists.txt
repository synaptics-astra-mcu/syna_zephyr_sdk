# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

add_compile_definitions(
  PINCTRL_STATE_ACTIVE=PINCTRL_STATE_PRIV_START
  PINCTRL_STATE_ACTIVE_NAME=active
)

list(APPEND ZEPHYR_EXTRA_MODULES
	${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/fs/fatfs
)

list(PREPEND DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(srw1500_smoke_test)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/fs/fatfs/include
)
