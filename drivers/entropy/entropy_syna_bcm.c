/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_bcm_trng

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>

LOG_MODULE_REGISTER(entropy_bcm, CONFIG_ENTROPY_LOG_LEVEL);

#define BCM_CMD				0x40
#define BCM_CMD_RET_STATUS		0x80
#define BCM_CMD_STATUS0			0x84
#define BCM_HST_INTERRUPT_RST		0xc8
#define SP_CMD_CMPLT			BIT(0)

#define BCM_ENTROPY_WORDS		8

#define MAX_ITERATIONS_OF_WAIT_TO_BCM	(500000)

#define BCM_PI_GET_RANDOM_NUMBER	0x10A

struct entropy_syna_bcm_config {
	uintptr_t reg_base;
};

static struct entropy_syna_bcm_config entropy_syna_bcm_cfg = {
	.reg_base = (uintptr_t)DT_INST_REG_ADDR(0),
};

static int bcm_wait_for_complete(const struct entropy_syna_bcm_config *cfg)
{
	uint32_t ret = -ETIMEDOUT;
	uint32_t cmd_cmplt = -1;
	uint32_t counter = 0;

	while (counter < MAX_ITERATIONS_OF_WAIT_TO_BCM) {
		cmd_cmplt = sys_read32(cfg->reg_base + BCM_HST_INTERRUPT_RST) & SP_CMD_CMPLT;

		if (cmd_cmplt) {
			ret = 0;
			break;
		}

		k_busy_wait(1); /* 256 cycles */
		counter++;
	}

	return ret;
}

static int read_bcm_status(const struct entropy_syna_bcm_config *cfg)
{
	int ret;

	ret = bcm_wait_for_complete(cfg);
	if (ret < 0) {
		return ret;
	}

	if (sys_read32(cfg->reg_base + BCM_CMD_RET_STATUS)) {
		/* Status code 0 means success */
		return -EINVAL;
	}

	return 0;
}

static void bcm_clear_cmd_cmplt_interrupt(const struct entropy_syna_bcm_config *cfg)
{
	uint32_t val;

	val = sys_read32(cfg->reg_base + BCM_HST_INTERRUPT_RST);
	val |= SP_CMD_CMPLT; /* write to clear */
	sys_write32(val, cfg->reg_base + BCM_HST_INTERRUPT_RST);
}

static int entropy_syna_bcm_get_entropy(const struct device *dev, uint8_t *buffer, uint16_t length)
{
	const struct entropy_syna_bcm_config *cfg = dev->config;
	int i;
	int ret;
	uint32_t val;
	uint32_t offset = 0;
	uint32_t remaining = length;

	while (remaining > 0) {
		sys_write32(BCM_PI_GET_RANDOM_NUMBER, cfg->reg_base + BCM_CMD);

		ret = read_bcm_status(cfg);
		if (ret < 0) {
			return ret;
		}

		i = 0;
		while ((remaining > 0) && (i < BCM_ENTROPY_WORDS)) {
			val = sys_read32(cfg->reg_base + BCM_CMD_STATUS0 + (i * 4));
			int chunk = MIN(remaining, 4);

			(void)memcpy(&buffer[offset], &val, chunk);
			offset += chunk;
			remaining -= chunk;
			i++;
		}

		bcm_clear_cmd_cmplt_interrupt(cfg);
	}

	return 0;
}

static int entropy_syna_bcm_get_entropy_isr(const struct device *dev, uint8_t *buffer,
					    uint16_t length, uint32_t flags)
{
	if (!(flags & ENTROPY_BUSYWAIT)) {
		return -ENOTSUP;
	}

	return entropy_syna_bcm_get_entropy(dev, buffer, length);
}

static DEVICE_API(entropy, entropy_syna_bcm_api) = {
	.get_entropy = entropy_syna_bcm_get_entropy,
	.get_entropy_isr = entropy_syna_bcm_get_entropy_isr,
};

static int entropy_syna_bcm_init(const struct device *dev)
{
	return 0;
}

DEVICE_DT_INST_DEFINE(0, entropy_syna_bcm_init, NULL, NULL, &entropy_syna_bcm_cfg, PRE_KERNEL_1,
		      CONFIG_ENTROPY_INIT_PRIORITY, &entropy_syna_bcm_api);
