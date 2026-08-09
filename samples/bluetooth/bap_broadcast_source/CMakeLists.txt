# Copyright (c) 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bap_broadcast_source)

target_sources(app PRIVATE
	$ENV{ZEPHYR_BASE}/samples/bluetooth/bap_broadcast_source/src/main.c
)
