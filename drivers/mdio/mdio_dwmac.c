/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT snps_dwmac_mdio

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/mdio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(snps_dwmac_mdio, CONFIG_MDIO_LOG_LEVEL);

#define MDIO_READ_CMD  (3u)
#define MDIO_WRITE_CMD (1u)

#define CORE_MDIO_SINGLE_COMMAND_ADDRESS_OFFSET		0x200
#define CORE_MDIO_SINGLE_COMMAND_ADDRESS_RA_SET(value)	(((value) << 16) & 0x001f0000)
#define CORE_MDIO_SINGLE_COMMAND_ADDRESS_PA_SET(value)	(((value) << 21) & 0x03e00000)
#define CORE_MDIO_SINGLE_COMMAND_ADDRESS_GOC_SET(value)	(((value) <<  2) & 0x0000000c)
#define CORE_MDIO_SINGLE_COMMAND_ADDRESS_CR_SET(value)	(((value) <<  8) & 0x00000f00)
#define CORE_MDIO_SINGLE_COMMAND_ADDRESS_BUSY		0x1

#define CORE_MDIO_SINGLE_COMMAND_DATA_OFFSET		0x204

struct mdio_dwmac_dev_data {
	DEVICE_MMIO_RAM;
	struct k_mutex mdio_transfer_lock;
};

struct mdio_dwmac_dev_config {
	DEVICE_MMIO_ROM;
	const struct pinctrl_dev_config *pcfg;
	uint32_t clk_range;
};

static inline int mdio_busy_wait(uint32_t reg_addr, uint32_t bit_msk)
{
	uint32_t delay_us = CONFIG_MDIO_DWMAC_STATUS_BUSY_CHECK_TIMEOUT;
	bool ret;

	ret = WAIT_FOR(!(sys_read32(reg_addr) & bit_msk), delay_us, k_msleep(1));
	if (ret == false) {
		return -ETIMEDOUT;
	}

	return 0;
}

static int mdio_transfer(const struct device *dev, uint8_t prtad, uint8_t devad, uint8_t rw,
			 uint16_t data_in, uint16_t *data_out)
{
	const struct mdio_dwmac_dev_config *const cfg = dev->config;
	struct mdio_dwmac_dev_data *const data = (struct mdio_dwmac_dev_data *)dev->data;
	mem_addr_t ioaddr = (mem_addr_t)DEVICE_MMIO_GET(dev);
	uint32_t mdio_addr;
	int ret;

	ret = mdio_busy_wait((ioaddr + CORE_MDIO_SINGLE_COMMAND_ADDRESS_OFFSET),
			     CORE_MDIO_SINGLE_COMMAND_ADDRESS_BUSY);
	if (ret) {
		LOG_ERR("%s: MDIO device busy wait timed out", dev->name);
		return ret;
	}

	(void)k_mutex_lock(&data->mdio_transfer_lock, K_FOREVER);

	sys_write32(data_in, ioaddr + CORE_MDIO_SINGLE_COMMAND_DATA_OFFSET);

	mdio_addr = CORE_MDIO_SINGLE_COMMAND_ADDRESS_RA_SET(devad) |
		    CORE_MDIO_SINGLE_COMMAND_ADDRESS_PA_SET(prtad) |
		    CORE_MDIO_SINGLE_COMMAND_ADDRESS_GOC_SET(rw) |
		    CORE_MDIO_SINGLE_COMMAND_ADDRESS_CR_SET(cfg->clk_range) |
		    CORE_MDIO_SINGLE_COMMAND_ADDRESS_BUSY;
	sys_write32(mdio_addr, ioaddr + CORE_MDIO_SINGLE_COMMAND_ADDRESS_OFFSET);

	k_busy_wait(10);

	ret = mdio_busy_wait(ioaddr + CORE_MDIO_SINGLE_COMMAND_ADDRESS_OFFSET,
			     CORE_MDIO_SINGLE_COMMAND_ADDRESS_BUSY);

	if (ret) {
		LOG_ERR("%s: transfer timed out", dev->name);
	} else {
		if (data_out) {
			*data_out = sys_read32(ioaddr + CORE_MDIO_SINGLE_COMMAND_DATA_OFFSET) &
				    0xffff;
		}
	}

	(void)k_mutex_unlock(&data->mdio_transfer_lock);

	return ret;
}

static int mdio_dwmac_read(const struct device *dev, uint8_t prtad, uint8_t regad, uint16_t *data)
{
	return mdio_transfer(dev, prtad, regad, MDIO_READ_CMD, 0, data);
}

static int mdio_dwmac_write(const struct device *dev, uint8_t prtad, uint8_t regad, uint16_t data)
{
	return mdio_transfer(dev, prtad, regad, MDIO_WRITE_CMD, data, NULL);
}

static int mdio_dwmac_initialize(const struct device *dev)
{
	struct mdio_dwmac_dev_data *const data = (struct mdio_dwmac_dev_data *)dev->data;
	const struct mdio_dwmac_dev_config *const cfg = dev->config;
	mem_addr_t ioaddr;
	int ret = 0;

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}

	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);
	ioaddr = (mem_addr_t)DEVICE_MMIO_GET(dev);

	k_mutex_init(&data->mdio_transfer_lock);

	return 0;
}

static DEVICE_API(mdio, mdio_dwmac_driver_api) = {
	.read = mdio_dwmac_read,
	.write = mdio_dwmac_write,
};

#define MDIO_DWMAC_CONFIG(n)                                                            \
	static const struct mdio_dwmac_dev_config mdio_dwmac_dev_config_##n = {         \
		DEVICE_MMIO_ROM_INIT(DT_PARENT(DT_DRV_INST(n))),                        \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                              \
		.clk_range = DT_INST_PROP(n, csr_clock_index),                          \
	};

#define MDIO_DWMAC_DEVICE(n)                                                            \
	PINCTRL_DT_INST_DEFINE(n);                                                      \
	MDIO_DWMAC_CONFIG(n);                                                           \
	static struct mdio_dwmac_dev_data mdio_dwmac_dev_data##n;                       \
	DEVICE_DT_INST_DEFINE(n, &mdio_dwmac_initialize, NULL, &mdio_dwmac_dev_data##n, \
			      &mdio_dwmac_dev_config_##n, POST_KERNEL,                  \
			      CONFIG_MDIO_INIT_PRIORITY, &mdio_dwmac_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MDIO_DWMAC_DEVICE)
