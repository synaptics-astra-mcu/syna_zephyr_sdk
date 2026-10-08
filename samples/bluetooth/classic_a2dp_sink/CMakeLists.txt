# Copyright (c) 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(LIBSBC_MODULE_PATH "$ENV{ZEPHYR_BASE}/../modules/lib/libsbc")
if(EXISTS "${LIBSBC_MODULE_PATH}/zephyr/module.yml")
  list(APPEND ZEPHYR_EXTRA_MODULES "${LIBSBC_MODULE_PATH}")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(classic_a2dp_sink)

target_sources(app PRIVATE
  src/main.c
)
