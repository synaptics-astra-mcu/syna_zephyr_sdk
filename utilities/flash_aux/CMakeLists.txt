# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
)

zephyr_library_sources_ifdef(CONFIG_MODULE_FLASH_AUX_ENABLED
  flash_aux_syna_sr100.c
)
