# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_ARM_SAU arm_sau.c)
zephyr_library_sources_ifdef(CONFIG_ARM_MPC_SIE300 arm_mpc_sie300.c)
zephyr_library_sources_ifdef(CONFIG_ARM_PPC arm_ppc.c)
zephyr_library_sources_ifdef(CONFIG_ARM_TGU_ARMV8_M arm_tgu_armv8_m.c)
