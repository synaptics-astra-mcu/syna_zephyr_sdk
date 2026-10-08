# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_property(ALLOW_EMPTY TRUE)

zephyr_library_sources_ifdef(CONFIG_ETH_DWMAC_SYNA_SL261X eth_dwmac_syna_sl261x.c)

zephyr_include_directories(${ZEPHYR_BASE}/drivers/ethernet)
