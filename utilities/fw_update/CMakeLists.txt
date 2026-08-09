# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})

zephyr_library_sources_ifdef(CONFIG_MODULE_FW_UPDATE_ENABLED
  fw_update_syna_sr100.c
  fw_update_boot_syna_sr100.c
)

zephyr_code_relocate_ifdef(CONFIG_FW_UPDATE_BOOT_HOOK_ENABLED
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/fw_update_boot_syna_sr100.c
  LOCATION RAM
)
