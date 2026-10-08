#Copyright (c) 2026 Synaptics Incorporated
#SPDX-License-Identifier: Apache-2.0

if(CONFIG_WIFI_SYNA)
	zephyr_library_named(syna_wifi)
	zephyr_library_compile_definitions(ZEPHYR_OS)
	add_subdirectory_ifdef(CONFIG_SYNA_WIFI_BACKEND_NATIVE_API native_api)
	add_subdirectory_ifdef(CONFIG_SYNA_WIFI_BACKEND_SUPPLICANT supp_drv)

endif()

# Use this to move critical funtion into RAM
#set(QUICK_ACCESS_CODE_AREA RAM_TEXT)
#zephyr_code_relocate(FILES src/driver_syna.c
#                     FILTER ".*\\.driver_init|.*\\.drv_syna_assoc"
#                     LOCATION ${QUICK_ACCESS_CODE_AREA} NOKEEP)
