/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_FLASH_CADENCE_XSPI_SOC_H_
#define ZEPHYR_INCLUDE_DRIVERS_FLASH_CADENCE_XSPI_SOC_H_

#include <stdint.h>

struct cadence_xspi_phy_config {
	uint32_t dll_ctrl;
	uint32_t dq_timing;
	uint32_t dqs_timing;
	uint32_t gate_lpbk_ctrl;
	uint32_t dll_master_ctrl;
	uint32_t dll_slave_ctrl;
};

int cadence_xspi_soc_pre_init(uintptr_t reg_base);
const struct cadence_xspi_phy_config *cadence_xspi_soc_phy_config(void);

#endif /* ZEPHYR_INCLUDE_DRIVERS_FLASH_CADENCE_XSPI_SOC_H_ */
