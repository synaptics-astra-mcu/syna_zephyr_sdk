/*
 * Copyright (c) 2026 Synaptics Incorporated
 * 
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_FLIGHT_CONTROL_H_
#define ZEPHYR_DRIVERS_FLIGHT_CONTROL_H_
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   en_fault_id - IDs for specific faults.
 */
typedef enum
{
    WDOG0_FAULT = 5,
    WDOG1_FAULT = 6,
    WDOG2_FAULT = 7,
    WDOG_SLOWCLK_FAULT = 33,
} en_fault_id;

/**
 * @brief	Configures the number of faults required before causing system reset.
 * @param	dev - pointer to device structure.
 * @param	num_faults - Number of faults required before causing system reset.
 * @return	None
 */
void fc_configure_num_faults_for_reset(const struct device *dev, uint32_t num_faults);

/**
 * @brief	Enables reset0 for the specific fault id.
 * @param	dev - pointer to device structure.
 * @param	fault_id - id of fault for which reset0 should be triggered.
 * @return	None
 */
void fc_enable_reset0_on_fault(const struct device *dev, en_fault_id fault_id);

/**
 * @brief	Disables reset0 for the specific fault id.
 * @param	dev - pointer to device structure.
 * @param	fault_id - id of fault for which reset0 should be triggered.
 * @return	None
 */
void fc_disable_reset0_on_fault(const struct device *dev, en_fault_id fault_id);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_FLIGHT_CONTROL_H_ */
