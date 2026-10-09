#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Synaptics Incorporated
#

cmake_minimum_required(VERSION 3.15.6)

if(NOT DEFINED CON_BOOT_PLATFORM)
	if(DEFINED ZEPHYR_BASE)
		set(CON_BOOT_PLATFORM zephyr CACHE STRING "con_boot platform adapter (zephyr or freertos)")
	else()
		set(CON_BOOT_PLATFORM freertos CACHE STRING "con_boot platform adapter (zephyr or freertos)")
	endif()
	set_property(CACHE CON_BOOT_PLATFORM PROPERTY STRINGS zephyr freertos)
endif()

if(NOT CON_BOOT_PLATFORM)
	message(FATAL_ERROR "CON_BOOT_PLATFORM must be set to zephyr or freertos")
endif()

if(CON_BOOT_PLATFORM STREQUAL "zephyr")
	set(CON_BOOT_PLATFORM_DEFINE CON_BOOT_PLATFORM_ZEPHYR)

elseif(CON_BOOT_PLATFORM STREQUAL "freertos")
	set(CON_BOOT_PLATFORM_DEFINE CON_BOOT_PLATFORM_FREERTOS)

else()
	message(FATAL_ERROR "Unsupported CON_BOOT_PLATFORM: ${CON_BOOT_PLATFORM}")
endif()

set(CON_BOOT_PLATFORM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/${CON_BOOT_PLATFORM})
set(CON_BOOT_PLATFORM_SOURCE ${CON_BOOT_PLATFORM_DIR}/con_boot_platform.c)

if(NOT DEFINED CON_BOOT_PLATFORM_INCLUDE_DIRS)
	if(CON_BOOT_PLATFORM STREQUAL "zephyr" AND DEFINED ZEPHYR_BASE)
		set(CON_BOOT_PLATFORM_INCLUDE_DIRS
			"${ZEPHYR_BASE}/../modules/hal/cmsis_6/CMSIS/RTOS2/Include"
			CACHE STRING "Platform SDK include directories required by con_boot")
	else()
		set(CON_BOOT_PLATFORM_INCLUDE_DIRS
			${CMAKE_SOURCE_DIR}/soc/SYN765x/cm52/include
			${CMAKE_SOURCE_DIR}/soc
			${CMAKE_SOURCE_DIR}/soc/SYN765x/cm52
			${CMAKE_SOURCE_DIR}/soc/SYN765x/cm52/config
			${CMAKE_SOURCE_DIR}/soc/SYN765x/cm52/device
			${CMAKE_SOURCE_DIR}/soc/SYN765x/cm52/include/registers
			${CMAKE_SOURCE_DIR}/drivers/romlib_lld/include
			${CMAKE_SOURCE_DIR}/drivers/romlib_lld/include/xspi
			${CMAKE_SOURCE_DIR}/third_party/cmsis/repo/CMSIS/Core/Include
			${CMAKE_SOURCE_DIR}/third_party/cmsis/repo/CMSIS/RTOS2/Include
			CACHE STRING "Platform SDK include directories required by con_boot")
	endif()
endif()

add_library(con_boot_platform OBJECT ${CON_BOOT_PLATFORM_SOURCE})
target_include_directories(con_boot_platform PRIVATE
	${CON_BOOT_PLATFORM_INCLUDE_DIRS}
	${CON_BOOT_PLATFORM_DIR}
)

add_library(con_boot_bus OBJECT src/common/con_boot_bus.c)
add_library(con_boot_transfer OBJECT src/common/con_boot_transfer.c)
add_library(con_boot_time OBJECT src/common/con_boot_time.c)
add_library(con_boot_windows OBJECT src/chip/srw1500/con_boot_windows.c)

set(CON_BOOT_TARGETS con_boot_platform con_boot_bus con_boot_transfer con_boot_windows
	con_boot_time)
set(CON_BOOT_OBJECTS
	$<TARGET_OBJECTS:con_boot_platform>
	$<TARGET_OBJECTS:con_boot_bus>
	$<TARGET_OBJECTS:con_boot_transfer>
	$<TARGET_OBJECTS:con_boot_time>
	$<TARGET_OBJECTS:con_boot_windows>)

# Select subsystem code from the owning platform's existing build options.
if((CON_BOOT_PLATFORM STREQUAL "zephyr" AND CONFIG_WIFI_SYNA) OR
	(CON_BOOT_PLATFORM STREQUAL "freertos" AND CONFIG_WIFI_ENABLED))
	add_library(con_boot_wlan OBJECT src/chip/srw1500/subsystem/wlan/con_boot_wlan.c)
	target_compile_definitions(con_boot_wlan PRIVATE CON_BOOT_TRACE_LOG)
	if(CONFIG_WLAN_SECURE_BOOT)
		target_compile_definitions(con_boot_wlan PRIVATE CONFIG_WLAN_SECURE_BOOT)
	endif()
	list(APPEND CON_BOOT_TARGETS con_boot_wlan)
	list(APPEND CON_BOOT_OBJECTS $<TARGET_OBJECTS:con_boot_wlan>)
endif()

if((CON_BOOT_PLATFORM STREQUAL "zephyr" AND CONFIG_BT_SYNA) OR
	(CON_BOOT_PLATFORM STREQUAL "freertos" AND CONFIG_BLUETOOTH_ENABLED))
	add_library(con_boot_bt OBJECT src/chip/srw1500/subsystem/bluetooth/con_boot_bt.c)
	target_compile_definitions(con_boot_bt PRIVATE CON_BOOT_TRACE_LOG)
	if(CONFIG_BT_SECURE_BOOT)
		target_compile_definitions(con_boot_bt PRIVATE CONFIG_BT_SECURE_BOOT)
	endif()
	list(APPEND CON_BOOT_TARGETS con_boot_bt)
	list(APPEND CON_BOOT_OBJECTS $<TARGET_OBJECTS:con_boot_bt>)
endif()

foreach(con_boot_target IN ITEMS ${CON_BOOT_TARGETS})
	target_compile_definitions(${con_boot_target} PRIVATE ${CON_BOOT_PLATFORM_DEFINE})
	target_include_directories(${con_boot_target}
		PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/include
			${CMAKE_CURRENT_SOURCE_DIR}/src
			src/common
			src/chip/srw1500
			${CMAKE_CURRENT_SOURCE_DIR}/src/platform
			${CON_BOOT_PLATFORM_INCLUDE_DIRS}
	)

	if(TARGET zephyr_interface)
		target_link_libraries(${con_boot_target} PRIVATE zephyr_interface)
	endif()

endforeach()

add_library(con_boot STATIC ${CON_BOOT_OBJECTS})

if(CON_BOOT_PLATFORM STREQUAL "zephyr")
	set_source_files_properties(
		${CMAKE_CURRENT_SOURCE_DIR}/src/common/con_boot_time.c
		PROPERTIES OBJECT_DEPENDS
		${CMAKE_BINARY_DIR}/zephyr/include/generated/zephyr/heap_constants.h
	)
endif()

target_include_directories(con_boot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
