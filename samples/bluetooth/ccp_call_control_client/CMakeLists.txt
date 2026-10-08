# Copyright (c) 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ccp_call_control_client)

target_sources(app PRIVATE
  src/main.c
)

zephyr_library_include_directories($ENV{ZEPHYR_BASE}/samples/bluetooth)
