# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mipi_capture_to_enc)

if(CONFIG_USB_TRANSPORT_CDC_ACM OR CONFIG_USB_TRANSPORT_UVC)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

target_sources(app PRIVATE src/main.c)

if(CONFIG_USB_TRANSPORT_CDC_ACM)
  target_sources(app PRIVATE src/usb_cdc_transport.c)
endif()

if(CONFIG_USB_TRANSPORT_UVC)
  target_sources(app PRIVATE src/usb_uvc_transport.c)
endif()
