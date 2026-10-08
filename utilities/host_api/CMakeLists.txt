# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../fw_update
  ${CMAKE_CURRENT_SOURCE_DIR}/../flash_aux
  ${CMAKE_CURRENT_SOURCE_DIR}/../nvm
  ${CMAKE_CURRENT_SOURCE_DIR}/services
  ${CMAKE_CURRENT_SOURCE_DIR}/services/service_flash_aux
  ${CMAKE_CURRENT_SOURCE_DIR}/services/service_fw_update
  ${CMAKE_CURRENT_SOURCE_DIR}/services/service_system
  ${CMAKE_CURRENT_SOURCE_DIR}/services/service_uc_manager
)

zephyr_library_sources_ifdef(CONFIG_SERVICE_HOST_API_ENABLED host_api_syna.c)
zephyr_library_sources_ifdef(CONFIG_HOST_API_AUTO_START host_api_startup_syna.c)

add_subdirectory(services)
