# Copyright (c) 2025 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

# Out-of-tree drivers for custom classes
add_subdirectory_ifdef(CONFIG_CACHE cache)
add_subdirectory_ifdef(CONFIG_CLOCK_CONTROL clock_control)
add_subdirectory_ifdef(CONFIG_DISPLAY display)
add_subdirectory_ifdef(CONFIG_DMA dma)
add_subdirectory_ifdef(CONFIG_FLASH flash)
add_subdirectory_ifdef(CONFIG_HWINFO hwinfo)
add_subdirectory(misc)
add_subdirectory_ifdef(CONFIG_PINCTRL pinctrl)
add_subdirectory_ifdef(CONFIG_RESET reset)
add_subdirectory_ifdef(CONFIG_SDHC sdhc)
add_subdirectory(usb)
add_subdirectory_ifdef(CONFIG_VIDEO video)
add_subdirectory_ifdef(CONFIG_WATCHDOG watchdog)
