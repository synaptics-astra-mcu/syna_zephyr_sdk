# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

zephyr_library_sources_ifdef(CONFIG_SERVICE_SYSTEM_ENABLED
  service_system.c
  service_system_opcodes.c
)
