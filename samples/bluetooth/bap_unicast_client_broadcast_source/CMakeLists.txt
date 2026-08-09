# Copyright (c) 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bap_unicast_client_broadcast_source)

target_include_directories(app PRIVATE
	$ENV{ZEPHYR_BASE}/samples/bluetooth/bap_unicast_client/src/
)

target_sources(app PRIVATE
	src/main.c
	$ENV{ZEPHYR_BASE}/samples/bluetooth/bap_unicast_client/src/stream_tx.c
	src/stream_lc3.c
)
