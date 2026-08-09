#
# Copyright 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dmic_shell)

foreach(_flag_var
    CMAKE_C_FLAGS
    CMAKE_ASM_FLAGS
    CMAKE_CXX_FLAGS
    EXTRA_CFLAGS
    EXTRA_AFLAGS
    EXTRA_CXXFLAGS)
  if(DEFINED ${_flag_var})
    string(REPLACE "-march=armv8.1-m.main" "" ${_flag_var} "${${_flag_var}}")
    string(REPLACE "-march=armv8.1-m.main+fp" "" ${_flag_var} "${${_flag_var}}")
    string(REPLACE "-march=armv8.1-m.main+mve.fp+fp.dp" "" ${_flag_var} "${${_flag_var}}")
  endif()
endforeach()

target_sources(app PRIVATE src/main.c)
