# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

include(ExternalProject)

if(SB_CONFIG_APP_M4)
  if(SB_CONFIG_APP_M4_SOURCE_DIR AND NOT SB_CONFIG_APP_M4_SOURCE_DIR STREQUAL "")
    set(SR100_M4_APP_DIR "${SB_CONFIG_APP_M4_SOURCE_DIR}")
  else()
    set(SR100_M4_APP_DIR "${APP_DIR}")
    message(STATUS "Synaptics M4: M4_APP_SOURCE_DIR not set, using main app: ${APP_DIR}")
  endif()

  ExternalZephyrProject_Add(
    APPLICATION sr100_m4
    SOURCE_DIR ${SR100_M4_APP_DIR}
    BOARD sr100_rdk/sr100/m4
  )

  set(SR100_IMAGEGEN_M4_PARAM "-m4_image" "${CMAKE_BINARY_DIR}/sr100_m4/zephyr/zephyr.elf")

  # Build and flash ordering for dual-core image composition.
  add_dependencies(${DEFAULT_IMAGE} sr100_m4)
  sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} sr100_m4)
  sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} sr100_m4)

  # If MCUboot is enabled, flash it before both application images.
  if(SB_CONFIG_BOOTLOADER_MCUBOOT)
    sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} mcuboot)
    sysbuild_add_dependencies(FLASH sr100_m4 mcuboot)
    set_config_int(mcuboot CONFIG_UPDATEABLE_IMAGE_NUMBER 2)
    set(sr100_m4_EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/m4_mcuboot.conf"
      CACHE INTERNAL "M4 MCUboot DTS overlay for multi-image setup"
    )
    set(SR100_IMAGEGEN_M4_PARAM "-m4_image" "${CMAKE_BINARY_DIR}/sr100_m4/zephyr/zephyr.signed.bin")

    set_config_string(sr100_m4 CONFIG_MCUBOOT_EXTRA_IMGTOOL_ARGS
      "--header-size=1024 --load-addr=0x34000000")
  endif()

  # Propagate reset policy to the M55 runtime image.
  set_config_bool(${DEFAULT_IMAGE} CONFIG_SR100_RELEASE_M4_RESET y)
endif()

set(SR100_IMAGEGEN_M55_IMG "${CMAKE_BINARY_DIR}/${DEFAULT_IMAGE}/zephyr/zephyr.elf")

if(SB_CONFIG_BOOTLOADER_MCUBOOT)
  set_config_string(${DEFAULT_IMAGE} CONFIG_MCUBOOT_EXTRA_IMGTOOL_ARGS
      "--header-size=1024 --load-addr=0x33ea0000")

  set(mcuboot_EXTRA_CONF_FILE
      "${CMAKE_CURRENT_LIST_DIR}/mcuboot.conf"
      CACHE INTERNAL "Board-level MCUboot Kconfig fragment"
  )
  set(mcuboot_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/mcuboot.overlay"
      CACHE INTERNAL "Board-level MCUboot DTS overlay"
  )

  if(SB_CONFIG_MCUBOOT_MODE_RAM_LOAD)
    set(${DEFAULT_IMAGE}_DTC_OVERLAY_FILE
      "${CMAKE_CURRENT_LIST_DIR}/app_mcuboot.overlay"
      CACHE INTERNAL "Board-level app DTS overlay for MCUboot RAM-load mode"
    )
  endif()

  if(SB_CONFIG_MCUBOOT_MODE_SINGLE_APP)
    set(SR100_IMAGEGEN_SINGLE_SLOT "-single_slot")
  endif()

  set(SR100_IMAGEGEN_MCUBOOT "-mcuboot")
  set(SR100_IMAGEGEN_APBL_ELF "${CMAKE_BINARY_DIR}/mcuboot/zephyr/zephyr.elf")
  set(SR100_IMAGEGEN_M55_IMG "${CMAKE_BINARY_DIR}/${DEFAULT_IMAGE}/zephyr/zephyr.signed.bin")
endif()

if(NOT DEFINED TC_RUNID)
  include(${CMAKE_CURRENT_LIST_DIR}/image_generation.cmake)
endif()
