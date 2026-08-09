# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_sources(
  soc.c
  )

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOC_LINKER_SCRIPT
    "${ZEPHYR_BASE}/../zephyr_srsdk/soc/syna/astra_sr/srw1500/linker.ld"
    CACHE INTERNAL "")

if(CONFIG_SOC_SRW1500)
  # --- Generate full image ---
  set(SRSDK_TOOLS_DIR ${ZEPHYR_BASE}/../srsdk_tools)
  set(SRW1500_TOOLS_DIR ${SRSDK_TOOLS_DIR}/srw1500_tools)
  set(SRW1500_INPUT_FILES_DIR ${SRW1500_TOOLS_DIR}/input_files)
  set(OUT_DIR ${CMAKE_BINARY_DIR}/zephyr)

  dt_chosen(SRW1500_FLASH_CTRL_PATH PROPERTY "zephyr,flash-controller")
  dt_nodelabel(SRW1500_XSPI_PATH NODELABEL "xspi")
  set(SRW1500_IS_XSPI_BUILD FALSE)
  if(DEFINED SRW1500_FLASH_CTRL_PATH AND DEFINED SRW1500_XSPI_PATH AND
     "${SRW1500_FLASH_CTRL_PATH}" STREQUAL "${SRW1500_XSPI_PATH}")
    set(SRW1500_IS_XSPI_BUILD TRUE)
  endif()

  if(SRW1500_IS_XSPI_BUILD)
    set(FLASH_TOOLS_DIR ${SRW1500_TOOLS_DIR})
    set(FLASH_INPUT_DIR ${SRW1500_INPUT_FILES_DIR})
    set(FLASH_SIGN_SCRIPT ${FLASH_TOOLS_DIR}/srw1500_image_gen.py)
    set(FLASH_STRIP_SCRIPT ${FLASH_TOOLS_DIR}/strip_command.py)
    set(FLASH_CREATE_SCRIPT ${FLASH_TOOLS_DIR}/createflashbin.py)

    if(NOT EXISTS ${FLASH_SIGN_SCRIPT})
      message(FATAL_ERROR "Missing ${FLASH_SIGN_SCRIPT}. Set up srw1500_tools/")
    endif()

    if (CONFIG_OTP_PROVISIONED_ENABLED)
    set(SPK_FLASH_BIN ${FLASH_INPUT_DIR}/v0.0.1-rc5-SPK_K1a_noopcodeheader.bin)
    set(KEYS_FLASH_BIN ${FLASH_INPUT_DIR}/Keys_NoHeader.bin)
    set(APBL_FLASH_BIN ${FLASH_INPUT_DIR}/srw1500_flash_apbl_signed.bin)
    elseif(CONFIG_OTP_PROVISIONED_V2_ENABLED)
    set(SPK_FLASH_BIN ${FLASH_INPUT_DIR}/v0.0.1-rc5-SPK_otp_k1a_signed_noopcodeheader.bin)
    set(KEYS_FLASH_BIN ${FLASH_INPUT_DIR}/Keys_hsm_OTP_K0_signed_NoHeader.bin)
    set(APBL_FLASH_BIN ${FLASH_INPUT_DIR}/srw1500_flash_apbl_signed_otp_k1b.bin)
    else()
    set(SPK_FLASH_BIN ${FLASH_INPUT_DIR}/v0.0.1-rc5-SPK_rom_k0_signed_noopcodeheader.bin)
    set(KEYS_FLASH_BIN ${FLASH_INPUT_DIR}/Keys_NoHeader.bin)
    set(APBL_FLASH_BIN ${FLASH_INPUT_DIR}/srw1500_flash_apbl_signed.bin)
    endif()

    set(BT_FW_BIN ${FLASH_INPUT_DIR}/srw1500-asic-patch_ram-slna-combo-ctr.meta)
    set(FLASH_SIGNED_BIN ${FLASH_TOOLS_DIR}/srw1500_signed.bin)
    set(FLASH_APP_SIGNED_BIN ${FLASH_TOOLS_DIR}/srw1500_flash_app_signed.bin)
    set(FLASH_HASH_ARTIFACTS
        ${FLASH_TOOLS_DIR}/SPK_SHA512.bin
        ${FLASH_TOOLS_DIR}/SPK_SHA256_in.bin
        ${FLASH_TOOLS_DIR}/SPK_SHA256_out.bin
        ${FLASH_TOOLS_DIR}/SPK_Signature.bin
    )
    set(XSPI_OUTPUT_BIN ${OUT_DIR}/srw1500_fw_flash_zephyr.bin)

    add_custom_command(
        OUTPUT ${FLASH_SIGNED_BIN}
        COMMAND ${CMAKE_COMMAND} -E echo "Generating srw1500_signed.bin for FLASH destination ..."
        COMMAND ${Python3_EXECUTABLE} ${FLASH_SIGN_SCRIPT}
            -f ${OUT_DIR}/${KERNEL_NAME}.bin
            -private ${FLASH_TOOLS_DIR}/priv.pem
            -public ${FLASH_TOOLS_DIR}/pub.pem
            -i APP
            -d FLASH
            -a ${OUT_DIR}/${KERNEL_NAME}.elf
        DEPENDS ${OUT_DIR}/${KERNEL_NAME}.bin ${OUT_DIR}/${KERNEL_NAME}.elf
        COMMENT "Generating srw1500_signed.bin"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    add_custom_command(
        OUTPUT ${FLASH_APP_SIGNED_BIN}
        COMMAND ${Python3_EXECUTABLE}
            ${FLASH_STRIP_SCRIPT}
            ${FLASH_SIGNED_BIN}
            ${FLASH_APP_SIGNED_BIN}
        DEPENDS ${FLASH_SIGNED_BIN}
        COMMENT "Stripping opcode header for srw1500_flash_app_signed.bin"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    add_custom_command(
        OUTPUT ${XSPI_OUTPUT_BIN}
        COMMAND ${Python3_EXECUTABLE} ${FLASH_CREATE_SCRIPT} ${XSPI_OUTPUT_BIN}
            ${FLASH_INPUT_DIR}/partition_attr_flash_2000.bin 0x2000
            ${FLASH_INPUT_DIR}/attr_flash_0000.bin 0x0
            ${KEYS_FLASH_BIN} 0x4000
            ${SPK_FLASH_BIN} 0xB000
            ${APBL_FLASH_BIN} 0x2B000
            ${FLASH_APP_SIGNED_BIN} 0x72000
            ${BT_FW_BIN} 0x600000
        DEPENDS
            ${TARGET}
            ${FLASH_APP_SIGNED_BIN}
            ${KEYS_FLASH_BIN}
            ${SPK_FLASH_BIN}
            ${APBL_FLASH_BIN}
            ${BT_FW_BIN}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating ${XSPI_OUTPUT_BIN}"
    )

    add_custom_target(m52_fw_xspi_bin ALL DEPENDS ${XSPI_OUTPUT_BIN})
    add_dependencies(m52_fw_xspi_bin ${TARGET})
    add_custom_command(
        TARGET m52_fw_xspi_bin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${FLASH_APP_SIGNED_BIN} ${OUT_DIR}/srw1500_m52_flash.bin
        COMMENT "Copying m52 flash image to out dir"
    )
    add_custom_command(
        TARGET m52_fw_xspi_bin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f
            ${FLASH_SIGNED_BIN}
            ${FLASH_APP_SIGNED_BIN}
            ${FLASH_HASH_ARTIFACTS}
        COMMENT "Cleaning temporary flash signing artifacts"
    )

    set_property(TARGET runners_yaml_props_target PROPERTY bin_file srw1500_fw_flash_zephyr.bin)
  else()
    set(TOOLS_DIR ${SRW1500_TOOLS_DIR})
    set(INPUT_DIR ${SRW1500_INPUT_FILES_DIR})

    if (CONFIG_OTP_PROVISIONED_ENABLED)
    set(SPK_BIN ${INPUT_DIR}/v0.0.1-rc5-SPK_K1a.bin)
    set(SPK_FLASH_BIN ${INPUT_DIR}/v0.0.1-rc5-SPK_K1a_noopcodeheader.bin)
    set(KEYS_FLASH_BIN ${INPUT_DIR}/Keys_NoHeader.bin)
    set(KEYS_BIN ${INPUT_DIR}/keys.bin)
    elseif(CONFIG_OTP_PROVISIONED_V2_ENABLED)
    set(SPK_BIN ${INPUT_DIR}/v0.0.1-rc5-SPK_otp_k1a_signed.bin)
    set(SPK_FLASH_BIN ${INPUT_DIR}/v0.0.1-rc5-SPK_otp_k1a_signed_noopcodeheader.bin)
    set(KEYS_FLASH_BIN ${INPUT_DIR}/Keys_hsm_OTP_K0_signed_NoHeader.bin)
    set(KEYS_BIN ${INPUT_DIR}/Keys_hsm_OTP_K0_signed.bin)
    else()
    set(SPK_BIN ${INPUT_DIR}/v0.0.1-rc5-SPK_rom_k0_signed.bin)
    set(SPK_FLASH_BIN ${INPUT_DIR}/v0.0.1-rc5-SPK_rom_k0_signed_noopcodeheader.bin)
    set(KEYS_FLASH_BIN ${INPUT_DIR}/Keys_NoHeader.bin)
    set(KEYS_BIN ${INPUT_DIR}/keys.bin)
    endif()

    add_custom_command(
        OUTPUT ${TOOLS_DIR}/srw1500_app_signed.bin
        COMMAND ${CMAKE_COMMAND} -E echo "Generating srw1500_app_signed.bin ..."
        COMMAND ${Python3_EXECUTABLE} ${TOOLS_DIR}/srw1500_image_gen_sram.py
            -e ${CMAKE_BINARY_DIR}/zephyr/${KERNEL_NAME}.elf
            -private ${TOOLS_DIR}/priv.pem
            -public ${TOOLS_DIR}/pub.pem
            -i APP
            --readelf ${CMAKE_READELF}
            --objdump ${CMAKE_OBJDUMP}
            --objcopy ${CMAKE_OBJCOPY}
            -o ${TOOLS_DIR}/srw1500_app_signed.bin
        DEPENDS ${CMAKE_BINARY_DIR}/zephyr/${KERNEL_NAME}.elf
        COMMENT "Generating srw1500_app_signed.bin"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    if (CONFIG_OTP_PROVISIONED_V2_ENABLED)
        set(INPUT_BIN_FILES
            ${KEYS_BIN}
            ${SPK_BIN}
            ${KEYS_BIN}
            ${INPUT_DIR}/srw1500_apbl_signed_otp_k1b.bin
            ${TOOLS_DIR}/srw1500_app_signed.bin
        )

        set(INPUT_BIN_DEPENDS
            ${KEYS_BIN}
            ${SPK_BIN}
            ${INPUT_DIR}/srw1500_apbl_signed_otp_k1b.bin
        )
    elseif (CONFIG_OTP_PROVISIONED_ENABLED)
        set(INPUT_BIN_FILES
            ${KEYS_BIN}
            ${SPK_BIN}
            ${KEYS_BIN}
            ${INPUT_DIR}/srw1500_apbl_signed.bin
            ${TOOLS_DIR}/srw1500_app_signed.bin
        )

        set(INPUT_BIN_DEPENDS
            ${KEYS_BIN}
            ${SPK_BIN}
            ${INPUT_DIR}/srw1500_apbl_signed.bin
        )
    else()
        set(INPUT_BIN_FILES
            ${SPK_BIN}
            ${INPUT_DIR}/srw1500_apbl_signed.bin
            ${TOOLS_DIR}/srw1500_app_signed.bin
        )

        set(INPUT_BIN_DEPENDS
            ${SPK_BIN}
            ${INPUT_DIR}/srw1500_apbl_signed.bin
        )
    endif()

    add_custom_command(
        OUTPUT ${OUT_DIR}/m52_fw_RAM.bin
        COMMAND cat ${INPUT_BIN_FILES} > ${OUT_DIR}/m52_fw_RAM.bin
        COMMAND ${CMAKE_COMMAND} -E rm -f ${TOOLS_DIR}/srw1500_app_signed.bin
        DEPENDS ${TARGET} ${INPUT_BIN_DEPENDS} ${TOOLS_DIR}/srw1500_app_signed.bin
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Concatenating binary files to ${OUT_DIR}/m52_fw_RAM.bin"
    )

    add_custom_target(m52_fw_ram_bin ALL DEPENDS ${OUT_DIR}/m52_fw_RAM.bin)
    add_custom_command(
        TARGET m52_fw_ram_bin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f ${TOOLS_DIR}/srw1500_app_signed.bin
        COMMENT "Cleaning temporary ${TOOLS_DIR}/srw1500_app_signed.bin"
    )

    set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
        COMMAND ${CMAKE_COMMAND} -E rm -f ${INPUT_DIR}/${TARGET}.bin ${INPUT_DIR}/${TARGET}.elf
        COMMAND ${CMAKE_COMMAND} -E rm -f ${TOOLS_DIR}/SPK_*.bin
    )
  endif()
endif()