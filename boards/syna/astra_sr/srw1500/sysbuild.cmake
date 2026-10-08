#
# Copyright 2026, Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0
#

if(SB_CONFIG_BOOTLOADER_MCUBOOT AND TARGET ${DEFAULT_IMAGE} AND TARGET mcuboot)
	set(mcuboot_EXTRA_CONF_FILE
			${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot.conf
			CACHE INTERNAL "Board-level MCUboot Kconfig fragment"
	)
	set(mcuboot_DTC_OVERLAY_FILE
			${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot.overlay
			CACHE INTERNAL "Board-level MCUboot DTS overlay"
	)

    add_dependencies(${DEFAULT_IMAGE} mcuboot)

    set_property(TARGET mcuboot PROPERTY MCUBOOT_FOOTER_UPDATE_IMAGES "")

endif()
