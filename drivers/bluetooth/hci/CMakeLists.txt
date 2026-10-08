# Copyright (c) 2026 Synaptics Incorporated
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BT_HCI_SYNA)
  zephyr_library_sources(hci_syna.c)
  # Patchram firmware is embedded in the driver library so that any Zephyr BT
  # sample (including upstream samples) picks it up automatically when
  # CONFIG_BT_HCI_SYNA=y without needing to add it to the app CMakeLists.
  set(SRSDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
  if(CONFIG_BT_4612)
    zephyr_library_sources(
        "${SRSDK_ROOT}/modules/hal_synaptics/bt/bt-firmware/syn4612/SYN4612A1_001.002.003.0020.0000.c"
    )
  elseif(CONFIG_BT_4381)
    zephyr_library_sources(
        "${SRSDK_ROOT}/modules/hal_synaptics/bt/bt-firmware/syn4381/SYNA4381A1_001.001.029.1197.1230.c"
    )
  else()
    message(FATAL_ERROR "Select BT firmware chip in Kconfig: enable CONFIG_BT_4381 or CONFIG_BT_4612")
  endif()
endif()

# HCI snoop logging — compiled into the Zephyr BT driver library so all BT
# samples pick it up automatically when CONFIG_BT_HCI_SNOOP=y.
zephyr_library_sources_ifdef(CONFIG_BT_HCI_SNOOP hci_snoop.c)
