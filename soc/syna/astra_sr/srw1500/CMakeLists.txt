# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_sources(
  soc.c
  )

zephyr_library_sources_ifdef(CONFIG_CPU_HAS_CUSTOM_FIXED_SOC_MPU_REGIONS mpu_regions.c)

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    CACHE INTERNAL ""
)

if(CONFIG_SOC_SRW1500)
    # --- Generate full image ---
    set(SRSDK_TOOLS_DIR ${ZEPHYR_BASE}/../srsdk_tools)
    set(SRW1500_TOOLS_DIR ${SRSDK_TOOLS_DIR}/srw1500_tools)
    set(SRW1500_INPUT_FILES_DIR ${SRW1500_TOOLS_DIR}/input_files)
    set(OUT_DIR ${CMAKE_BINARY_DIR}/zephyr)
    set(FLASH_INPUT_DIR ${SRW1500_INPUT_FILES_DIR})

    dt_chosen(SRW1500_FLASH_CTRL_PATH PROPERTY "zephyr,flash-controller")
    dt_nodelabel(SRW1500_XSPI_PATH NODELABEL "xspi")
    set(SRW1500_IS_XSPI_BUILD FALSE)
    if(DEFINED SRW1500_FLASH_CTRL_PATH AND DEFINED SRW1500_XSPI_PATH AND
        "${SRW1500_FLASH_CTRL_PATH}" STREQUAL "${SRW1500_XSPI_PATH}")
        set(SRW1500_IS_XSPI_BUILD TRUE)
    endif()

    set(APP_SIGNED_BIN ${OUT_DIR}/${KERNEL_NAME}.signed.bin)

    set(APBL_FLASH_SIGNED_BIN ${OUT_DIR}/srw1500_flash_apbl_signed.bin)
    if (CONFIG_OTP_PROVISIONED_ENABLED)
        set(SPK_FLASH_BIN  ${FLASH_INPUT_DIR}/spk_rc6PlusZephyrMCUBoot_k1a_noopcodeheader.bin)
        set(KEYS_FLASH_BIN ${FLASH_INPUT_DIR}/Keys_NoHeader.bin)
        set(APBL_FLASH_BIN ${FLASH_INPUT_DIR}/srw1500_flash_apbl_signed.bin)
    elseif(CONFIG_OTP_PROVISIONED_V2_ENABLED)
        set(SPK_FLASH_BIN  ${FLASH_INPUT_DIR}/spk_rc6PlusZephyrMCUBoot_otp_k1a_signed_noopcodeheader.bin)
        set(KEYS_FLASH_BIN ${FLASH_INPUT_DIR}/Keys_hsm_OTP_K0_signed_NoHeader.bin)
        set(APBL_FLASH_BIN ${FLASH_INPUT_DIR}/srw1500_flash_apbl_signed_otp_k1b.bin)
    else()
        set(SPK_FLASH_BIN  ${FLASH_INPUT_DIR}/spk_rc6PlusZephyrMCUBoot_rom_k0_signed_noopcodeheader.bin)
        set(KEYS_FLASH_BIN ${FLASH_INPUT_DIR}/Keys_NoHeader.bin)
        set(APBL_FLASH_BIN ${FLASH_INPUT_DIR}/srw1500_flash_apbl_signed.bin)
    endif()

    set(XSPI_OUTPUT_BIN ${OUT_DIR}/srw1500_zephyr_full_flash.bin)

    if(CONFIG_BOOTLOADER_MCUBOOT)
        set(FLASH_TOOLS_DIR ${SRW1500_TOOLS_DIR})
        set(FLASH_SIGN_SCRIPT ${FLASH_TOOLS_DIR}/srw1500_image_gen.py)
        set(APBL_SIGNED_BIN ${OUT_DIR}/srw1500_apbl_signed.bin)
        set(FLASH_CREATE_SCRIPT ${FLASH_TOOLS_DIR}/createflashbin.py)
        set(FLASH_STRIP_SCRIPT ${FLASH_TOOLS_DIR}/strip_command.py)
        set(FLASH_WLAN_SCRIPT ${FLASH_TOOLS_DIR}/gen_wlan_flash_image.py)

        if(NOT EXISTS ${FLASH_SIGN_SCRIPT})
            message(FATAL_ERROR "Missing ${FLASH_SIGN_SCRIPT}. Set up srw1500_tools/")
        endif()

        message(STATUS "OUT_DIR: ${OUT_DIR}")
        add_custom_command(
            OUTPUT ${APBL_SIGNED_BIN}
            COMMAND ${CMAKE_COMMAND} -E echo "Generating srw1500_apbl_signed.bin ..."
            COMMAND ${Python3_EXECUTABLE} ${FLASH_SIGN_SCRIPT}
                -f ${OUT_DIR}/../../mcuboot/zephyr/zephyr.bin
                -private ${FLASH_TOOLS_DIR}/priv_4.pem
                -public ${FLASH_TOOLS_DIR}/pub_4.pem
                -i APBL
                -a ${OUT_DIR}/../../mcuboot/zephyr/zephyr.elf
                -o ${APBL_SIGNED_BIN}
            DEPENDS
                ${OUT_DIR}/../../mcuboot/zephyr/zephyr.bin
                ${OUT_DIR}/../../mcuboot/zephyr/zephyr.elf
            COMMENT "Generating srw1500_apbl_signed.bin"
        )

        if (SRW1500_IS_XSPI_BUILD)
            add_custom_command(
                OUTPUT ${APBL_FLASH_SIGNED_BIN}
                COMMAND ${Python3_EXECUTABLE}
                    ${FLASH_TOOLS_DIR}/strip_command.py
                    ${APBL_SIGNED_BIN}
                    ${APBL_FLASH_SIGNED_BIN}
                DEPENDS ${APBL_SIGNED_BIN}
                COMMENT "Stripping first 32 bytes from srw1500_apbl_signed.bin"
            )
            add_custom_target(srw1500_apbl_image ALL DEPENDS ${APBL_FLASH_SIGNED_BIN})

            set(BT_FW_BIN ${FLASH_INPUT_DIR}/srw1500-asic-patch_ram-slna-combo-ctr.meta)

            if(CONFIG_WIFI_ENABLED)
                set(WLAN_IMG_DIR ${ZEPHYR_BASE}/../zephyr_srsdk/modules/hal_synaptics/wifi/lib/syn765x/)
                set(GEN_WLAN_FLASH_IMG ${FLASH_WLAN_SCRIPT})

                # Check WLAN images and gen_wlan_flash_image.py are present
                if(EXISTS ${WLAN_IMG_DIR} AND EXISTS ${GEN_WLAN_FLASH_IMG})
                    if(CONFIG_WLAN_SECURE_BOOT)
                        file(GLOB WLAN_FW_PKG "${WLAN_IMG_DIR}/rtecdc.pkg")
                        if(NOT WLAN_FW_PKG)
                            message(FATAL_ERROR "rtecdc.pkg not found in ${WLAN_IMG_DIR}")
                        endif()
                    else()
                        file(GLOB WLAN_FW_PKG "${WLAN_IMG_DIR}/rtecdc.bin")
                        if(NOT WLAN_FW_PKG)
                            message(FATAL_ERROR "rtecdc.bin not found in ${WLAN_IMG_DIR}")
                        endif()
                    endif()

                    # Match only nvram.txt
                    file(GLOB WLAN_NVRAM "${WLAN_IMG_DIR}/nvram.txt")
                    if(NOT WLAN_NVRAM)
                        message(FATAL_ERROR "nvram.txt not found in ${WLAN_IMG_DIR}")
                    endif()

                    # Match only master.clm_blob
                    file(GLOB WLAN_CLM "${WLAN_IMG_DIR}/master.clm_blob")
                    if(NOT WLAN_CLM)
                        message(FATAL_ERROR "master.clm_blob not found in ${WLAN_IMG_DIR}")
                    endif()

                    if(CONFIG_WLAN_SECURE_BOOT)
                        set(WLAN_IMG_NAME "syn765x-secure_wlan_flash_img.bin")
                    else()
                        set(WLAN_IMG_NAME "syn765x-non_secure_wlan_flash_img.bin")
                    endif()

                    # Generate WLAN flash image (non-secure for DEV, secure for release)
                    if(CONFIG_WLAN_SECURE_BOOT)
                        set(GEN_WLAN_ARGS --secure --fw ${WLAN_FW_PKG})
                    else()
                        set(GEN_WLAN_ARGS --fw ${WLAN_FW_PKG})
                    endif()

                    set(GEN_WLAN_DEPS ${WLAN_FW_PKG} ${GEN_WLAN_FLASH_IMG})
                    list(APPEND GEN_WLAN_ARGS --nvram ${WLAN_NVRAM})
                    list(APPEND GEN_WLAN_DEPS ${WLAN_NVRAM})
                    list(APPEND GEN_WLAN_ARGS --clm ${WLAN_CLM})
                    list(APPEND GEN_WLAN_DEPS ${WLAN_CLM})

                    # Include the generated wlan_flash_img.bin with whole flash image
                    set(WLAN_BIN ${OUT_DIR}/${WLAN_IMG_NAME})

                    add_custom_command(
                        OUTPUT ${WLAN_BIN}
                        COMMAND ${Python3_EXECUTABLE}
                    ${GEN_WLAN_FLASH_IMG}
                            ${GEN_WLAN_ARGS}
                            --out ${WLAN_BIN}
                        DEPENDS ${GEN_WLAN_DEPS}
                        COMMENT "Generating WLAN flash image: ${WLAN_IMG_NAME}"
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                    )
                    message(STATUS "WLAN: Auto-generating flash image from ${WLAN_IMG_DIR}")
                else()
                    message(FATAL_ERROR "WLAN FW assets (${WLAN_IMG_DIR}) or gen_wlan_flash_image.py not found.")
                endif()
                set(WLAN_FLASH_IMG_ENTRY ${WLAN_BIN} 0x480000)

            else()
                set(WLAN_FLASH_IMG_ENTRY "")
            endif()

            add_custom_command(
                OUTPUT ${XSPI_OUTPUT_BIN}
                COMMAND ${Python3_EXECUTABLE} ${FLASH_CREATE_SCRIPT} ${XSPI_OUTPUT_BIN}
                    ${FLASH_INPUT_DIR}/partition_attr_flash_2000.bin 0x2000
                    ${FLASH_INPUT_DIR}/attr_flash_0000.bin 0x0
                    ${KEYS_FLASH_BIN} 0x4000
                    ${SPK_FLASH_BIN} 0xB000
                    ${APBL_FLASH_BIN} 0x2AD8C
                    ${APP_SIGNED_BIN} 0x72000
                    ${WLAN_FLASH_IMG_ENTRY}
                    ${BT_FW_BIN} 0x600000
                DEPENDS
                    ${APP_SIGNED_BIN}
                    ${KEYS_FLASH_BIN}
                    ${SPK_FLASH_BIN}
                    ${APBL_FLASH_BIN}
                    ${WLAN_BIN}
                    ${BT_FW_BIN}
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Generating ${XSPI_OUTPUT_BIN}"
            )

            add_custom_target(m52_fw_xspi_bin ALL DEPENDS ${XSPI_OUTPUT_BIN})
            add_custom_command(
                TARGET m52_fw_xspi_bin POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy ${APP_SIGNED_BIN} ${OUT_DIR}/srw1500_zephyr_m52_flash.bin
                COMMENT "Copying m52 flash image to out dir"
            )
            add_dependencies(m52_fw_xspi_bin srw1500_apbl_image)
            add_custom_command(
                TARGET m52_fw_xspi_bin POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E rm -f
                    ${FLASH_TOOLS_DIR}/SPK_SHA512.bin
                    ${FLASH_TOOLS_DIR}/SPK_SHA256_in.bin
                    ${FLASH_TOOLS_DIR}/SPK_SHA256_out.bin
                    ${FLASH_TOOLS_DIR}/SPK_Signature.bin
                COMMENT "Cleaning signing artifacts"
            )
            set_property(TARGET runners_yaml_props_target PROPERTY bin_file srw1500_zephyr_full_flash.bin)
        else()
            add_custom_target(srw1500_apbl_image ALL DEPENDS ${APBL_SIGNED_BIN})
        endif()
    else()
        message(STATUS "MCUBUILD should be enabled for a full build of the firmware.")
    endif()
endif()
