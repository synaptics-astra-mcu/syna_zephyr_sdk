# Copyright 2026, Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_CAN_SYNA can_syna.c)
zephyr_library_property(ALLOW_EMPTY TRUE)
