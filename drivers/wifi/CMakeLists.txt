# Copyright 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_property(ALLOW_EMPTY TRUE)
add_subdirectory_ifdef(CONFIG_WIFI              synaptics)
