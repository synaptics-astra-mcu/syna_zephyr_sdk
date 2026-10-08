# SPDX-License-Identifier: Apache-2.0
# Module: USB CDC driver build integration

zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR})
zephyr_library_sources_ifdef(CONFIG_USB_CDC_LAYER usb_cdc_stack_service.c)
