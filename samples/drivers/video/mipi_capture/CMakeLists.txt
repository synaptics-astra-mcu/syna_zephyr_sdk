# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

#
# SR100 MIPI capture sample uses two devicetree profiles:
# - WQVGA uses the default SRAM layout (default)
# - FHD (`boards/sr100_rdk_m55_fhd.overlay`) reserves 2MB SHM
#
# FILE_SUFFIX does not reliably apply DT overlays in our tree, so add the
# profile overlay explicitly via DTC_OVERLAY_FILE.
#
if(DEFINED BOARD AND (BOARD STREQUAL "sr100_rdk" OR BOARD MATCHES "^sr100_rdk(/|$)"))
  # `BOARD` may include qualifiers, e.g. `sr100_rdk/sr100/m55`.
  set(_board_name "${BOARD}")
  set(_board_qual "")
  set(_board_qual_short "")
  if(BOARD MATCHES "/")
    string(REPLACE "/" ";" _board_parts "${BOARD}")
    list(GET _board_parts 0 _board_name)
    list(REMOVE_AT _board_parts 0)
    list(JOIN _board_parts "_" _board_qual)
    list(LENGTH _board_parts _board_parts_len)
    if(_board_parts_len GREATER 0)
      math(EXPR _last_idx "${_board_parts_len} - 1")
      list(GET _board_parts ${_last_idx} _board_qual_short)
    endif()
  endif()

  if(NOT DEFINED FILE_SUFFIX)
    set(FILE_SUFFIX "_wqvga" CACHE STRING "Board file suffix for overlays" FORCE)
  endif()

  # Only FHD needs an overlay (SHM reservation + SRAM carveout).
  if(FILE_SUFFIX STREQUAL "_fhd")
    if(_board_qual_short STREQUAL "")
      set(_profile_overlay "${CMAKE_CURRENT_LIST_DIR}/boards/${_board_name}${FILE_SUFFIX}.overlay")
    else()
      # Naming: only include the last qualifier (e.g. m55).
      set(_profile_overlay "${CMAKE_CURRENT_LIST_DIR}/boards/${_board_name}_${_board_qual_short}${FILE_SUFFIX}.overlay")
    endif()

    if(NOT EXISTS "${_profile_overlay}")
      message(FATAL_ERROR "Missing SR100 video profile overlay: ${_profile_overlay}")
    endif()

    if(DEFINED DTC_OVERLAY_FILE)
      set(DTC_OVERLAY_FILE "${DTC_OVERLAY_FILE};${_profile_overlay}" CACHE STRING "Devicetree overlays" FORCE)
    else()
      set(DTC_OVERLAY_FILE "${_profile_overlay}" CACHE STRING "Devicetree overlays" FORCE)
    endif()
  endif()
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mipi_capture)

target_sources(app PRIVATE src/main.c)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../../../modules/hal/synaptics/astra_sr/sr100/includes
)
