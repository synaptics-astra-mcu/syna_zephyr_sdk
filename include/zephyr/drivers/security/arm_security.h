/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SECURITY_ARM_SECURITY_H_
#define ZEPHYR_INCLUDE_DRIVERS_SECURITY_ARM_SECURITY_H_

#include <zephyr/device.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum arm_sec_attr {
	ARM_SECURE = 0,
	ARM_NONSECURE = 1,
	ARM_MIXED = 2,
};

int arm_mpc_config_region(const struct device *dev, uint32_t base,
			   uint32_t limit, enum arm_sec_attr attr);
int arm_mpc_lockdown(const struct device *dev);

int arm_ppc_config_periph(const struct device *dev, uint8_t ns_pos,
			   bool nonsecure, uint8_t priv_pos, bool user_ok,
			   uint8_t sresp_pos, bool error_response);

int arm_tgu_config_region(const struct device *dev, uint32_t base,
			   uint32_t limit, enum arm_sec_attr attr);

#ifdef __cplusplus
}
#endif

#endif
