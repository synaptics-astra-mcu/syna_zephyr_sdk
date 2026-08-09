# Copyright (c) 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bap_broadcast_sink)

# Our local src/ must come FIRST so local lc3.h / usb.h shadow the upstream
# versions that include USB headers (not available on SR110).
target_include_directories(app PRIVATE
	src/
	$ENV{ZEPHYR_BASE}/samples/bluetooth/bap_broadcast_sink/src/
)

target_sources(app PRIVATE
	$ENV{ZEPHYR_BASE}/samples/bluetooth/bap_broadcast_sink/src/main.c
	$ENV{ZEPHYR_BASE}/samples/bluetooth/bap_broadcast_sink/src/stream_rx.c
	src/lc3.c
	src/hw_codec.c
	src/usb.c
)
