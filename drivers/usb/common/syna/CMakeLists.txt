# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PHY_SYNA_SL261X_USB2 phy_syna_sl261x_usb2.c)
