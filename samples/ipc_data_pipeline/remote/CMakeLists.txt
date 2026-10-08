#
# Copyright (c) 2025 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(BOARD STREQUAL "sr100_rdk" AND BOARD_QUALIFIERS MATCHES "^sr100/[^/]+$")
  message(STATUS "${BOARD}/${BOARD_QUALIFIERS} compile as CLIENT in this sample")
else()
  message(FATAL_ERROR "${BOARD}/${BOARD_QUALIFIERS} is not supported for this sample")
endif()

project(ipc_data_pipeline_remote)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_sources(app PRIVATE src/main.c)
