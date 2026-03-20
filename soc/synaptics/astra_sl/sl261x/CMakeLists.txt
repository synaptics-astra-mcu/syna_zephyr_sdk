# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SOC_SL2619_A55)
  zephyr_include_directories(a55)
  zephyr_sources(a55/soc.c)
  zephyr_sources_ifdef(CONFIG_ARM_MMU a55/mmu_regions.c)
  set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm64/scripts/linker.ld CACHE INTERNAL "")
elseif(CONFIG_SOC_SL2619_M52)
  zephyr_include_directories(${ZEPHYR_BASE}/drivers)
  zephyr_include_directories(m52)

  zephyr_sources(m52/soc.c)
  zephyr_library_sources_ifdef(CONFIG_ASOC m52/asoc.c)

  set(SOC_LINKER_SCRIPT
    ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld
    CACHE INTERNAL ""
  )

  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  # ------------------------------------------------------------------
  # Locate image generator script (portable: Linux + macOS)
  # ------------------------------------------------------------------
  execute_process(
    COMMAND find "${ZEPHYR_BASE}/../" -type f -name "build_preboot_mcu.py" -print -quit
    OUTPUT_VARIABLE SL261X_IMGGEN
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    )

  # ------------------------------------------------------------------
  # Locate optional A55 image
  # ------------------------------------------------------------------
  find_file(SL261X_A55_IMG
    NAMES zephyr.bin
    PATHS ${CMAKE_BINARY_DIR}/${SL261X_A55_OUT}/zephyr/
  )

  message(STATUS "SL261X image generator: ${SL261X_IMGGEN}")
  message(STATUS "SL261X A55 image: ${SL261X_A55_IMG}")

# ------------------------------------------------------------------
# Post-build step
# ------------------------------------------------------------------
  if(NOT SL261X_IMGGEN)
    message(WARNING "SL261X image generator not found; skipping image generation step")
  else()
    if(NOT SL261X_A55_IMG)
      set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
        COMMAND ${Python3_EXECUTABLE} ${SL261X_IMGGEN}
            --outdir ${CMAKE_BINARY_DIR}/
      )
    else()
      set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
        COMMAND ${Python3_EXECUTABLE} ${SL261X_IMGGEN}
            --outdir ${CMAKE_BINARY_DIR}/
            --a55_image ${SL261X_A55_IMG}
      )
    endif()
  endif()
endif()
