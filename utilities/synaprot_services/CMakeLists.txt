# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})

zephyr_library_sources_ifdef(CONFIG_MODULE_SPROT_SERVICES_SRW1500
    synaprot_service_srw1500.c
)

zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/mbox
)
