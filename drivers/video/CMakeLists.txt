# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/video)
zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../include)
zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../modules/hal/synaptics/astra_sr/sr100/includes)
zephyr_library_sources_ifdef(CONFIG_VIDEO_OV02C10 ov02c10.c)
zephyr_library_sources_ifdef(CONFIG_VIDEO_SYNA_MIPI video_syna_sr100_mipi.c)
zephyr_library_sources_ifdef(CONFIG_VIDEO_SYNA_ENC video_syna_sr100_enc.c)

if(CONFIG_VIDEO_SYNA_MIPI OR CONFIG_VIDEO_SYNA_ENC)
  set(LP_SENSE_LIB_DIR
    ${CMAKE_CURRENT_LIST_DIR}/../../modules/hal/synaptics/astra_sr/sr100/lib)

  add_library(libjpeg_syna_a STATIC IMPORTED GLOBAL)
  set_target_properties(libjpeg_syna_a PROPERTIES
    IMPORTED_LOCATION ${LP_SENSE_LIB_DIR}/libjpeg_syna.a
  )
  zephyr_library_link_libraries(libjpeg_syna_a)
endif()

if(CONFIG_VIDEO_SYNA_MIPI OR CONFIG_VIDEO_SYNA_ENC)
  add_library(syna_mipi STATIC IMPORTED GLOBAL)
  set_target_properties(syna_mipi PROPERTIES
    IMPORTED_LOCATION
      ${CMAKE_CURRENT_LIST_DIR}/../../modules/hal/synaptics/astra_sr/sr100/lib/libmipi_syna.a
  )
  zephyr_library_link_libraries(syna_mipi)
endif()

if(CONFIG_VIDEO_SYNA_MIPI)
  zephyr_link_libraries(-Wl,--wrap=printf)
  # SRSDK sources may emit `.nonCached*` input sections (e.g. osal_shm.c).
  zephyr_linker_sources(SECTIONS
    ${CMAKE_CURRENT_LIST_DIR}/../../modules/hal/synaptics/astra_sr/sr100/linker/video-imgproc-noncached-sections.ld
  )
endif()
zephyr_library_property(ALLOW_EMPTY TRUE)
