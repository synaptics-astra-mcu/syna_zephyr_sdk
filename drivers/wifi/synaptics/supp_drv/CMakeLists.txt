#Copyright 2022 - 2026 Synaptics Incorporated
#SPDX-License-Identifier: Apache-2.0

set(BUILD_SYN765X_ZEPHYR ON CACHE BOOL "" FORCE)

if(DEFINED ZEPHYR_HOSTAP_MODULE_DIR AND NOT "${ZEPHYR_HOSTAP_MODULE_DIR}" STREQUAL "")
	set(HOSTAP_BASE ${ZEPHYR_HOSTAP_MODULE_DIR})
else()
	set(HOSTAP_BASE ${ZEPHYR_BASE}/modules/hostap)
endif()

set(HOSTAP_SRC_BASE ${HOSTAP_BASE}/src)

zephyr_library_include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}
	${HOSTAP_BASE}
	${HOSTAP_SRC_BASE}
	${HOSTAP_SRC_BASE}/common
	${HOSTAP_SRC_BASE}/crypto
	${HOSTAP_SRC_BASE}/drivers
	${HOSTAP_SRC_BASE}/rsn_supp
	${HOSTAP_SRC_BASE}/utils
)

if(CONFIG_WIFI_BUILD_DEVEL)
    set(MHD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/mhd)
    if(IS_DIRECTORY "${MHD_SRC}")
	file(GLOB MHD_DIR_CONTENTS "${MHD_SRC}/*")
	if(MHD_DIR_CONTENTS)
	    add_subdirectory("${MHD_SRC}")
	    zephyr_library_link_libraries(mhd_lib)
	else()
	    message(FATAL_ERROR  "RUN: git -C zephyr_srsdk/drivers/wifi/synaptics/supp_drv submodule update --init mhd")
	endif()
    else()
	message(FATAL_ERROR [=[ RUN: git clone -b main git@github.com:syna-wireless/MHD.git zephyr_srsdk/drivers/wifi/synaptics/supp_drv/mhd/]=])
    endif()
else()
    set(MHD_BASE ${ZEPHYR_BASE}/../zephyr_srsdk/modules/hal_synaptics/wifi)
    zephyr_library_include_directories(
	${MHD_BASE}/include
    )
    zephyr_library_link_libraries(${MHD_BASE}/lib/libmhd_lib.a)
    zephyr_library_link_libraries(con_boot)

endif()

zephyr_library_link_libraries(hostap)

zephyr_library_sources(
	syna_wifi.c
	syna_wifi_events.c
	supplicant_mhd_event.c
	supplicant_mhd_adaptor.c
	supplicant_mhd_transport_zephyr.c
	syna_wifi_zephyr.c
)
