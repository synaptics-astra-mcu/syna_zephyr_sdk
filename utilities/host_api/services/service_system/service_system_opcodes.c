/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "service_system.h"
#include "host_api_internal.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/sys_io.h>

#include <logger.h>

#ifndef LOG_MOD_GENERIC
#define LOG_MOD_GENERIC "GENERIC"
#endif

static int loaded_apps_reported_once;

#if defined(CONFIG_I2C)
#if CONFIG_HOST_API_SYSTEM_I2C_BUS == 0
#define HOST_API_SYSTEM_I2C_NODE DT_NODELABEL(i2c0)
#elif CONFIG_HOST_API_SYSTEM_I2C_BUS == 1
#define HOST_API_SYSTEM_I2C_NODE DT_NODELABEL(i2c1)
#else
#error "Unsupported CONFIG_HOST_API_SYSTEM_I2C_BUS value"
#endif
#endif

__attribute__((weak)) void host_api_loaded_apps_report(void)
{
}

int system_get_host_api_version(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint8_t *payload = &p_output[sizeof(h_api_header_t)];

	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	sys_put_le32(0x4U, &p_output[4]);
	payload[0] = 0;
	payload[1] = CONFIG_HOST_API_SDK_VER_MAJOR;
	payload[2] = CONFIG_HOST_API_SDK_VER_MINOR;
	payload[3] = CONFIG_HOST_API_SDK_VER_REVISION;
	k_msleep(10);

	LOG_INFO(LOG_MOD_GENERIC, "Zephyr SDK version is: %d.%d.%d\n",
		 CONFIG_HOST_API_SDK_VER_MAJOR, CONFIG_HOST_API_SDK_VER_MINOR,
		 CONFIG_HOST_API_SDK_VER_REVISION);
	LOG_INFO(LOG_MOD_GENERIC, "Host API version is: %d.%d.%d\n",
		 HOST_API_VERSION_MAJOR, HOST_API_VERSION_MINOR, HOST_API_VERSION_PATCH);

	if (loaded_apps_reported_once) {
		return SERVICE_SYSTEM_RC_OK;
	}

	LOG_INFO(LOG_MOD_GENERIC, "Loaded applications are:\n");
	host_api_loaded_apps_report();
	loaded_apps_reported_once = 1;

	return SERVICE_SYSTEM_RC_OK;
}

int system_read_register(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t address;
	uint32_t value;

	ARG_UNUSED(id);

	address = sys_get_le32(&p_input[sizeof(h_api_header_t)]);
	value = sys_read32(address);
	sys_put_le32(sizeof(uint32_t), &p_output[4]);
	sys_put_le32(value, &p_output[sizeof(h_api_header_t)]);

	LOG_DEBUG(LOG_MOD_GENERIC, "address = 0x%x, value = 0x%x\n", address, value);
	return SERVICE_SYSTEM_RC_OK;
}

int system_write_register(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t address;
	uint32_t value;

	ARG_UNUSED(id);

	address = sys_get_le32(&p_input[sizeof(h_api_header_t)]);
	value = sys_get_le32(&p_input[sizeof(h_api_header_t) + sizeof(uint32_t)]);
	sys_write32(value, address);
	sys_put_le32(0U, &p_output[4]);

	LOG_DEBUG(LOG_MOD_GENERIC, "address = 0x%x, value = 0x%x\n", address, value);
	return SERVICE_SYSTEM_RC_OK;
}

int system_get_loaded_apps(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	sys_put_le32(0U, &p_output[4]);
	k_msleep(10);
	LOG_INFO(LOG_MOD_GENERIC, "Loaded applications are:\n");
	host_api_loaded_apps_report();

	return SERVICE_SYSTEM_RC_OK;
}

int system_toggle_crc(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	sys_put_le32(0U, &p_output[4]);
	host_api_toggle_crc_check();

	return SERVICE_SYSTEM_RC_OK;
}

int system_read_pending_message(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	host_api_send_pending_message(p_output);

	return SERVICE_SYSTEM_RC_OK;
}

int system_config_active_interface(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	uint32_t req_interface = (uint32_t)p_input[sizeof(h_api_header_t)];

	ARG_UNUSED(id);

	sys_put_le32(0U, &p_output[4]);
	host_api_set_active_interface(req_interface);

	return SERVICE_SYSTEM_RC_OK;
}

int system_initiate_sw_reset(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
	ARG_UNUSED(id);
	ARG_UNUSED(p_input);

	LOG_INFO(LOG_MOD_GENERIC, "Software reset command received\n");
	host_api_schedule_reset(100U);
	sys_put_le32(0U, &p_output[4]);

	return SERVICE_SYSTEM_RC_OK;
}

int system_read_i2c_register(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
#if defined(CONFIG_I2C)
	uint8_t *payload = &p_output[sizeof(h_api_header_t)];
	const struct device *i2c_dev = DEVICE_DT_GET(HOST_API_SYSTEM_I2C_NODE);
	uint8_t slave_addr;
	uint8_t reg_addr;
	uint8_t value = 0U;
	int ret;

	ARG_UNUSED(id);

	slave_addr = p_input[sizeof(h_api_header_t)];
	reg_addr = p_input[sizeof(h_api_header_t) + 1U];
	sys_put_le32(sizeof(int32_t) + sizeof(uint8_t), &p_output[4]);

	if (!device_is_ready(i2c_dev)) {
		ret = -ENODEV;
		sys_put_le32((uint32_t)ret, payload);
		payload[sizeof(int32_t)] = 0U;
		LOG_ERROR(LOG_MOD_GENERIC, "I2C bus %d is not ready\n",
			  CONFIG_HOST_API_SYSTEM_I2C_BUS);
		return SERVICE_SYSTEM_RC_OK;
	}

	ret = i2c_write_read(i2c_dev, slave_addr, &reg_addr, sizeof(reg_addr),
			     &value, sizeof(value));
	sys_put_le32((uint32_t)ret, payload);
	payload[sizeof(int32_t)] = value;

	if (ret != 0) {
		LOG_ERROR(LOG_MOD_GENERIC,
			  "I2C read failed bus=%d slave=0x%02x reg=0x%02x rc=%d\n",
			  CONFIG_HOST_API_SYSTEM_I2C_BUS, slave_addr, reg_addr, ret);
		return SERVICE_SYSTEM_RC_OK;
	}

	LOG_INFO(LOG_MOD_GENERIC,
		 "I2C read bus=%d slave=0x%02x reg=0x%02x val=0x%02x\n",
		 CONFIG_HOST_API_SYSTEM_I2C_BUS, slave_addr, reg_addr, value);

	return SERVICE_SYSTEM_RC_OK;
#else
	ARG_UNUSED(id);
	ARG_UNUSED(p_input);
	ARG_UNUSED(p_output);

	return -ENOTSUP;
#endif
}

int system_write_i2c_register(uint8_t id, uint8_t *p_input, uint8_t *p_output)
{
#if defined(CONFIG_I2C)
	uint8_t *payload = &p_output[sizeof(h_api_header_t)];
	const struct device *i2c_dev = DEVICE_DT_GET(HOST_API_SYSTEM_I2C_NODE);
	uint8_t slave_addr;
	uint8_t reg_addr;
	uint8_t value;
	uint8_t write_buf[2];
	int ret;

	ARG_UNUSED(id);

	slave_addr = p_input[sizeof(h_api_header_t)];
	reg_addr = p_input[sizeof(h_api_header_t) + 1U];
	value = p_input[sizeof(h_api_header_t) + 2U];
	sys_put_le32(sizeof(int32_t), &p_output[4]);

	if (!device_is_ready(i2c_dev)) {
		ret = -ENODEV;
		sys_put_le32((uint32_t)ret, payload);
		LOG_ERROR(LOG_MOD_GENERIC, "I2C bus %d is not ready\n",
			  CONFIG_HOST_API_SYSTEM_I2C_BUS);
		return SERVICE_SYSTEM_RC_OK;
	}

	write_buf[0] = reg_addr;
	write_buf[1] = value;
	ret = i2c_write(i2c_dev, write_buf, sizeof(write_buf), slave_addr);
	sys_put_le32((uint32_t)ret, payload);

	if (ret != 0) {
		LOG_ERROR(LOG_MOD_GENERIC,
			  "I2C write failed bus=%d slave=0x%02x reg=0x%02x val=0x%02x rc=%d\n",
			  CONFIG_HOST_API_SYSTEM_I2C_BUS, slave_addr, reg_addr, value, ret);
		return SERVICE_SYSTEM_RC_OK;
	}

	LOG_INFO(LOG_MOD_GENERIC,
		 "I2C write bus=%d slave=0x%02x reg=0x%02x val=0x%02x\n",
		 CONFIG_HOST_API_SYSTEM_I2C_BUS, slave_addr, reg_addr, value);

	return SERVICE_SYSTEM_RC_OK;
#else
	ARG_UNUSED(id);
	ARG_UNUSED(p_input);
	ARG_UNUSED(p_output);

	return -ENOTSUP;
#endif
}
