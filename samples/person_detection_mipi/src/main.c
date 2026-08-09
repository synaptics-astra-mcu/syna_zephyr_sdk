/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Host API controlled person detection MIPI sample shell
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "person_detection_mipi_app.h"
#include "service_uc_manager_interface.h"

bool host_api_is_ready(void);

void host_api_loaded_apps_report(void)
{
	printk("zephyr_person_detection_mipi\n");
}

static int register_person_detection_usecase(void)
{
	static const uc_operations_t ops = {
		.create = person_detection_mipi_app_create,
		.start = person_detection_mipi_app_start,
		.stop = person_detection_mipi_app_stop,
		.resume = person_detection_mipi_app_resume,
		.kill = person_detection_mipi_app_kill,
	};

	return uc_manager_register_usecase((uint8_t)CONFIG_PERSON_DETECTION_MIPI_USECASE_ID,
					   &ops);
}

int main(void)
{
	int ret;

	printk("person_detection_mipi: main start\n");

	ret = register_person_detection_usecase();
	if (ret != 0) {
		printk("person_detection_mipi: usecase register failed: %d\n", ret);
		return ret;
	}

	printk("person_detection_mipi: usecase registered, waiting for host api\n");

	for (int elapsed = 0; elapsed < CONFIG_PERSON_DETECTION_MIPI_HOST_API_READY_TIMEOUT_MS;
	     elapsed += 10) {
		if (host_api_is_ready()) {
			printk("person_detection_mipi: host api ready\n");
			while (true) {
				k_sleep(K_FOREVER);
			}
		}

		k_msleep(10);
	}

	printk("person_detection_mipi: host api ready timeout\n");
	return -ETIMEDOUT;
}
