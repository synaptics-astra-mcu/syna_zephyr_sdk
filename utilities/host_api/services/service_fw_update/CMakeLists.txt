# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../fw_update
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../nvm
)

zephyr_library_sources_ifdef(CONFIG_SERVICE_FW_UPDATE_ENABLED
  service_fw_update.c
  service_fw_update_opcodes.c
)
