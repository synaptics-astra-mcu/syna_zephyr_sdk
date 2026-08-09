# Copyright 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/hal_synaptics/)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi_subsys_test)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
