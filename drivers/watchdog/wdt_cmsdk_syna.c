/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT arm_cmsdk_syna_watchdog

/**
 * @brief Driver for CMSDK APB Watchdog.
 */

#include <errno.h>
#include <soc.h>
#include <zephyr/irq.h>
#include <zephyr/irq_extend.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/fc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "wdt_cmsdk_syna.h"

#define WATCHDOG_CMSDK_FREQ(inst)	DT_INST_PROP_BY_PHANDLE(inst, clocks, clock_frequency)
#define FLIGHT_CONTROL_NODE(inst)	DT_PHANDLE(DT_DRV_INST(inst), flight_control)

struct cmsdk_wdt_cfg {
	struct wdog_cmsdk *base;
	const struct device *fc_dev;
	uint32_t irq_warning;
	uint32_t irq_expire;
	uint32_t fault_num;
	uint32_t clk_freq_khz;
};

struct cmsdk_wdt_data {
	wdt_callback_t callback_warning;
	wdt_callback_t callback_expire;
};

static int cmsdk_wdt_setup(const struct device *dev, uint8_t options)
{
	const struct cmsdk_wdt_cfg *cfg = dev->config;
	cfg->base->ctrl = (CMSDK_WDOG_CTRL_RESEN | CMSDK_WDOG_CTRL_INTEN);
	irq_clearpending(cfg->irq_warning);
	irq_enable(cfg->irq_warning);
	irq_clearpending(cfg->irq_expire);
	irq_enable(cfg->irq_expire);
	return 0;
}

static int cmsdk_wdt_disable(const struct device *dev)
{
	const struct cmsdk_wdt_cfg *cfg = dev->config;

	cfg->base->intclr = CMSDK_WDOG_INTCLR;
	cfg->base->ctrl = ~(CMSDK_WDOG_CTRL_RESEN | CMSDK_WDOG_CTRL_INTEN);
	cfg->base->load = CMSDK_WDOG_VALUE;

	irq_disable(cfg->irq_warning);
	irq_disable(cfg->irq_expire);
	return 0;
}

static int cmsdk_wdt_install_timeout(const struct device *dev,
					const struct wdt_timeout_cfg *config)
{
	const struct cmsdk_wdt_cfg *cfg = dev->config;
	struct cmsdk_wdt_data *data = dev->data;

#ifndef CONFIG_WDT_MULTISTAGE
	return -EINVAL;
#endif

	if (config->window.max == 0) {	
		return -EINVAL;
	}

	data->callback_warning = config->callback;

	if (!config->next)
		return -EINVAL;

	data->callback_expire = config->next->callback;

	switch (config->flags) {
		case WDT_FLAG_RESET_NONE:
			fc_disable_reset0_on_fault(cfg->fc_dev, cfg->fault_num);
			break;
		case WDT_FLAG_RESET_CPU_CORE:
		case WDT_FLAG_RESET_SOC:
			fc_configure_num_faults_for_reset(cfg->fc_dev, 1);
			fc_enable_reset0_on_fault(cfg->fc_dev, cfg->fault_num);
			break;
		default:
			return -EINVAL;
	}

	cfg->base->load = config->window.max * cfg->clk_freq_khz;

	return 0;
}

static int cmsdk_wdt_feed(const struct device *dev, int channel_id)
{
	const struct cmsdk_wdt_cfg *cfg = dev->config;

	cfg->base->intclr = CMSDK_WDOG_INTCLR;
	irq_disable(cfg->irq_warning);
	irq_clearpending(cfg->irq_warning);
	irq_enable(cfg->irq_warning);
	irq_disable(cfg->irq_expire);
	irq_clearpending(cfg->irq_expire);
	irq_enable(cfg->irq_expire);

	return 0;
}

static DEVICE_API(wdt, cmsdk_wdt_api) = {
	.setup = cmsdk_wdt_setup,
	.disable = cmsdk_wdt_disable,
	.install_timeout = cmsdk_wdt_install_timeout,
	.feed = cmsdk_wdt_feed,
};

static void cmsdk_wdt_irq_warning(const struct device *dev)
{
	const struct cmsdk_wdt_cfg *cfg = dev->config;
	struct cmsdk_wdt_data *data = dev->data;

	irq_disable(cfg->irq_warning);

	data->callback_warning(dev, 0);
}

static void cmsdk_wdt_irq_expire(const struct device *dev)
{
	const struct cmsdk_wdt_cfg *cfg = dev->config;
	struct cmsdk_wdt_data *data = dev->data;

	irq_disable(cfg->irq_expire);

	data->callback_expire(dev, 0);
}

#define CMSDK_WDT_INIT(inst)                                              \
	static struct cmsdk_wdt_data cmsdk_wdt_data_##inst;            \
	static const struct cmsdk_wdt_cfg cmsdk_wdt_cfg_##inst = { \
		.base = (struct wdog_cmsdk *)DT_INST_REG_ADDR(inst),    \
		.fc_dev = DEVICE_DT_GET(FLIGHT_CONTROL_NODE(inst)),							\
		.irq_warning = DT_INST_IRQN(inst),                             \
		.irq_expire = DT_INST_IRQN_BY_IDX(inst, 1),                    \
		.fault_num = DT_INST_PROP(inst, fault_num),					   \
		.clk_freq_khz = WATCHDOG_CMSDK_FREQ(inst) / 1000,	\
	};                                                                   \
	static int cmsdk_wdt_init_##inst(const struct device *dev) {      \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),   \
			    cmsdk_wdt_irq_warning, DEVICE_DT_INST_GET(inst), 0); \
		IRQ_CONNECT(DT_INST_IRQN_BY_IDX(inst, 1),                       \
			    DT_INST_IRQ_BY_IDX(inst, 1, priority),               \
			    cmsdk_wdt_irq_expire, DEVICE_DT_INST_GET(inst), 0); \
		return 0;                                                      \
	}                                                                   \
	DEVICE_DT_INST_DEFINE(inst, cmsdk_wdt_init_##inst, NULL,          \
			      &cmsdk_wdt_data_##inst,                       \
			      &cmsdk_wdt_cfg_##inst, POST_KERNEL,         \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,              \
			      &cmsdk_wdt_api);

DT_INST_FOREACH_STATUS_OKAY(CMSDK_WDT_INIT);