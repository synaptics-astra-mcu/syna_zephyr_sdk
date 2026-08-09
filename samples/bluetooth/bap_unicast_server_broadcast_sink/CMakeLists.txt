# Copyright (c) 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bap_unicast_server_broadcast_sink)

# src/ must be FIRST so local stub headers (lc3.h, usb.h) shadow upstream ones
target_include_directories(app PRIVATE
	src/
	$ENV{ZEPHYR_BASE}/samples/bluetooth/bap_unicast_server/src/
	$ENV{ZEPHYR_BASE}/samples/bluetooth/bap_broadcast_sink/src/
)

target_sources(app PRIVATE
	src/main.c
	# Unicast server TX (not used for SNK-only, but required by stream_tx.c)
	$ENV{ZEPHYR_BASE}/samples/bluetooth/bap_unicast_server/src/stream_tx.c
	src/stream_lc3.c
	# Broadcast sink RX (ISO packet counting / LC3 stub decode)
	$ENV{ZEPHYR_BASE}/samples/bluetooth/bap_broadcast_sink/src/stream_rx.c
	# SR110 stubs (shadow upstream hw_codec.c / lc3.c / usb.c)
	src/lc3.c
	src/hw_codec.c
	src/usb.c
)
