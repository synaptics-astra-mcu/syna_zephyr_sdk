# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_FLIGHT_CONTROL fc.c)