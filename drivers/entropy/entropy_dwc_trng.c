/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT snps_dwc_trng

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <errno.h>

#include "dwc_trng_reg.h"

LOG_MODULE_REGISTER(dwc_trng, CONFIG_ENTROPY_LOG_LEVEL);

#define DWC_TRNG_SEED_WORDS      12
#define DWC_TRNG_TIMEOUT         K_MSEC(1)

struct dwc_trng_config {
	mm_reg_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};

struct dwc_trng_data {
	struct k_spinlock lock;
};

static inline uint32_t trng_read(const struct dwc_trng_config *cfg,
				 uint32_t reg)
{
	return sys_read32(cfg->base + reg);
}

static inline void trng_write(const struct dwc_trng_config *cfg,
			      uint32_t reg,
			      uint32_t value)
{
	sys_write32(value, cfg->base + reg);
}

static int dwc_trng_wait_idle(const struct dwc_trng_config *cfg)
{
	uint32_t stat;
	k_timepoint_t end = sys_timepoint_calc(DWC_TRNG_TIMEOUT);

	do {
		stat = trng_read(cfg, REG_ADDR__TRNG_REGS_STAT);

		if (!(stat & BIT(REG_FIELD_POS__TRNG_REGS_STAT_BUSY))) {
			return 0;
		}
	} while (!sys_timepoint_expired(end));

	LOG_ERR("Timed out waiting for idle");
	return -ETIMEDOUT;
}

static int dwc_trng_wait_done(const struct dwc_trng_config *cfg)
{
	uint32_t istat;
	k_timepoint_t end = sys_timepoint_calc(DWC_TRNG_TIMEOUT);

	do {
		istat = trng_read(cfg, REG_ADDR__TRNG_REGS_ISTAT);

		if (istat & BIT(REG_FIELD_POS__TRNG_REGS_ISTAT_DONE)) {
			trng_write(cfg,
				   REG_ADDR__TRNG_REGS_ISTAT,
				   BIT(REG_FIELD_POS__TRNG_REGS_ISTAT_DONE));
			return 0;
		}
	} while (!sys_timepoint_expired(end));

	LOG_ERR("Timed out waiting for DONE interrupt");
	return -ETIMEDOUT;
}

static void dwc_trng_set_test_mode(const struct dwc_trng_config *cfg)
{
	uint32_t smode;

	smode = trng_read(cfg, REG_ADDR__TRNG_REGS_SMODE);

	smode &= ~BIT(REG_FIELD_POS__TRNG_REGS_SMODE_MISSION_MODE);

	trng_write(cfg,
		   REG_ADDR__TRNG_REGS_SMODE,
		   smode);
}

static void dwc_trng_start_noise_generation(
			const struct dwc_trng_config *cfg)
{
	uint32_t ctrl;

	ctrl = ENUM__TRNG_REGS_CTRL_CMD__GEN_NOISE
		<< REG_FIELD_POS__TRNG_REGS_CTRL_CMD;

	trng_write(cfg,
		   REG_ADDR__TRNG_REGS_CTRL,
		   ctrl);
}

static int dwc_trng_generate_seed(const struct dwc_trng_config *cfg,
				  uint32_t *seed_words)
{
	int ret;

	ret = dwc_trng_wait_idle(cfg);
	if (ret != 0) {
		return ret;
	}

	dwc_trng_set_test_mode(cfg);

	ret = dwc_trng_wait_idle(cfg);
	if (ret != 0) {
		return ret;
	}

	dwc_trng_start_noise_generation(cfg);

	ret = dwc_trng_wait_done(cfg);
	if (ret != 0) {
		return ret;
	}

	seed_words[0]  = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED0);
	seed_words[1]  = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED1);
	seed_words[2]  = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED2);
	seed_words[3]  = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED3);
	seed_words[4]  = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED4);
	seed_words[5]  = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED5);
	seed_words[6]  = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED6);
	seed_words[7]  = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED7);
	seed_words[8]  = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED8);
	seed_words[9]  = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED9);
	seed_words[10] = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED10);
	seed_words[11] = trng_read(cfg, REG_ADDR__TRNG_REGS_SEED11);

	return 0;
}

static int dwc_trng_get_entropy(const struct device *dev,
				uint8_t *buffer,
				uint16_t length)
{
	const struct dwc_trng_config *cfg = dev->config;
	struct dwc_trng_data *data = dev->data;

	uint32_t seed[DWC_TRNG_SEED_WORDS];
	size_t copied = 0;
	int ret;

	k_spinlock_key_t key = k_spin_lock(&data->lock);

	while (copied < length) {

		size_t remaining;
		size_t copy_len;

		ret = dwc_trng_generate_seed(cfg, seed);
		if (ret != 0) {
			k_spin_unlock(&data->lock, key);
			return ret;
		}

		remaining = length - copied;

		copy_len = MIN(remaining, sizeof(seed));

		memcpy(buffer + copied, seed, copy_len);

		copied += copy_len;
	}

	k_spin_unlock(&data->lock, key);

	return 0;
}

static const struct entropy_driver_api dwc_trng_api = {
	.get_entropy = dwc_trng_get_entropy,
};

static int dwc_trng_init(const struct device *dev)
{
	const struct dwc_trng_config *cfg = dev->config;
	int ret;

	if (cfg->reset.dev != NULL) {
		if (!device_is_ready(cfg->reset.dev)) {
			LOG_ERR("Reset device not ready");
			return -ENODEV;
		}

		ret = reset_line_toggle(cfg->reset.dev, cfg->reset.id);
		if (ret != 0) {
			LOG_ERR("TRNG reset failed (%d)", ret);
			return ret;
		}
	}

	if (cfg->clock_dev != NULL) {
		if (!device_is_ready(cfg->clock_dev)) {
			LOG_ERR("Clock device not ready");
			return -ENODEV;
		}

		ret = clock_control_on(cfg->clock_dev, cfg->clock_subsys);
		if (ret != 0 && ret != -EALREADY && ret != -ENOSYS) {
			LOG_ERR("TRNG clock enable failed (%d)", ret);
			return ret;
		}
	}

	return dwc_trng_wait_idle(cfg);
}

#define DWC_TRNG_CLOCK_INIT(inst) \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, clocks), \
		(.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)), \
		 .clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),), \
		(.clock_dev = NULL, .clock_subsys = NULL,))

#define DWC_TRNG_RESET_INIT(inst) \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, resets), \
		(.reset = RESET_DT_SPEC_INST_GET(inst),), \
		(.reset = { .dev = NULL, .id = 0 },))

#define DWC_TRNG_INIT(inst)						\
	static struct dwc_trng_data dwc_trng_data_##inst;		\
									\
	static const struct dwc_trng_config				\
	dwc_trng_config_##inst = {					\
		.base = DT_INST_REG_ADDR(inst),			\
		DWC_TRNG_CLOCK_INIT(inst)				\
		DWC_TRNG_RESET_INIT(inst)				\
	};								\
									\
	DEVICE_DT_INST_DEFINE(inst,					\
			      dwc_trng_init,				\
			      NULL,					\
			      &dwc_trng_data_##inst,			\
			      &dwc_trng_config_##inst,			\
			      PRE_KERNEL_1,				\
			      CONFIG_ENTROPY_INIT_PRIORITY,		\
			      &dwc_trng_api);

DT_INST_FOREACH_STATUS_OKAY(DWC_TRNG_INIT)