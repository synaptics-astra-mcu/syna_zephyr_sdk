/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "host_api.h"
#include "service_fw_update.h"

LOG_MODULE_REGISTER(host_api_system, LOG_LEVEL_INF);

static volatile uint32_t host_api_validation_reg = 0x13572468U;

void host_api_loaded_apps_report(void)
{
	LOG_INF("zephyr_host_api_system");
}

int main(void)
{
	int ret = 0;

	LOG_INF("Host API system sample start");

	for (int elapsed = 0; elapsed < 5000; elapsed += 10) {
		if (host_api_is_ready()) {
			ret = 0;
			break;
		}

		ret = -ETIMEDOUT;
		k_msleep(10);
	}

	if (ret != 0) {
		LOG_ERR("Host API did not become ready: %d", ret);
		return ret;
	}

#if defined(CONFIG_SERVICE_FW_UPDATE_ENABLED)
	for (int elapsed = 0; elapsed < 5000; elapsed += 10) {
		if (service_fw_update_is_ready()) {
			ret = 0;
			break;
		}

		ret = -ETIMEDOUT;
		k_msleep(10);
	}

	if (ret != 0) {
		LOG_ERR("FW update service did not become ready: %d", ret);
		return ret;
	}
#endif

	LOG_INF("Host API is ready; open the Host API port on host and send Host API commands");
	LOG_INF("Validation register @ 0x%08lx = 0x%08lx",
		(unsigned long)(uintptr_t)&host_api_validation_reg,
		(unsigned long)host_api_validation_reg);

	while (true) {
		k_sleep(K_FOREVER);
	}
}
