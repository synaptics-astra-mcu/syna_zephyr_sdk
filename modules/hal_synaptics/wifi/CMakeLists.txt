# Copyright 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources_ifdef(CONFIG_WIFI_4612 src/4612/wifi_fw.c)
zephyr_library_sources_ifdef(CONFIG_WIFI_4612 src/4612/wifi_nv.c)
zephyr_library_sources_ifdef(CONFIG_WIFI_4612 src/4612/wifi_clm.c)

zephyr_library_sources_ifdef(CONFIG_WIFI_4381 src/4381/wifi_fw.c)
zephyr_library_sources_ifdef(CONFIG_WIFI_4381 src/4381/wifi_nv.c)
zephyr_library_sources_ifdef(CONFIG_WIFI_4381 src/4381/wifi_clm.c)

zephyr_library_sources_ifdef(CONFIG_WIFI_43711 src/43711/wifi_fw.c)
zephyr_library_sources_ifdef(CONFIG_WIFI_43711 src/43711/wifi_nv.c)
zephyr_library_sources_ifdef(CONFIG_WIFI_43711 src/43711/wifi_clm.c)

zephyr_library_link_libraries(${CMAKE_CURRENT_SOURCE_DIR}/libmhd_test_4612a0.a)
