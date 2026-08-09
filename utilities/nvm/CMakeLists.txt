# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})

zephyr_library_sources_ifdef(CONFIG_MODULE_NVM_ENABLED nvm_syna_sr100.c)
