# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../flash_aux
)

zephyr_library_sources_ifdef(CONFIG_SERVICE_FLASH_AUX_ENABLED
  service_flash_aux.c
  service_flash_aux_opcodes.c
)
