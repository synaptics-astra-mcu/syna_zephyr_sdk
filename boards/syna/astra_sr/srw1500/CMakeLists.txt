# SPDX-License-Identifier: Apache-2.0

if(CONFIG_FLASH_MAP_CUSTOM)
  zephyr_sources(${CMAKE_CURRENT_LIST_DIR}/flash_map.c)
endif()
