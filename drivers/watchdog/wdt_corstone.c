/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT arm_corstone_watchdog

/**
 * @brief Driver for Corstone APB Watchdog.
 */

#include <errno.h>
#include <soc.h>
#include <zephyr/irq.h>
#include <zephyr/irq_extend.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/fc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>


#include "wdt_corstone.h"

#define CORSTONE_SYSCNT_NODE(inst)  DT_PHANDLE(DT_DRV_INST(inst), system_counter)
#define CORSTONE_CLOCK_NODE(inst)   DT_PHANDLE(CORSTONE_SYSCNT_NODE(inst), clocks)
#define WATCHDOG_CORSTONE_FREQ(inst)   DT_PROP(CORSTONE_CLOCK_NODE(inst), clock_frequency)
#define FLIGHT_CONTROL_NODE(inst)	DT_PHANDLE(DT_DRV_INST(inst), flight_control)

struct corstone_wdt_cfg {
	struct wdog_corstone_ctrl *ctrl_base;
	struct wdog_corstone_ref *ref_base;
	const struct device *fc_dev;
	uint32_t irq_warning;
	uint32_t irq_expire;
	uint32_t fault_num;
	uint32_t clk_freq_khz;
};

struct corstone_wdt_data {
	wdt_callback_t callback_warning;
	wdt_callback_t callback_expire;
};

static int corstone_wdt_setup(const struct device *dev, uint8_t options)
{
	const struct corstone_wdt_cfg *cfg = dev->config;
	cfg->ctrl_base->wcs |= WDT_WCS_ENABLE;
	irq_clearpending(cfg->irq_warning);
	irq_enable(cfg->irq_warning);
	irq_clearpending(cfg->irq_expire);
	irq_enable(cfg->irq_expire);
	return 0;
}

static int corstone_wdt_disable(const struct device *dev)
{
	const struct corstone_wdt_cfg *cfg = dev->config;

	cfg->ctrl_base->wcs &= ~WDT_WCS_ENABLE;

	irq_disable(cfg->irq_warning);
	irq_disable(cfg->irq_expire);
	return 0;
}

static int corstone_wdt_install_timeout(const struct device *dev,
					const struct wdt_timeout_cfg *config)
{
	const struct corstone_wdt_cfg *cfg = dev->config;
	struct corstone_wdt_data *data = dev->data;

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

	cfg->ctrl_base->wor = config->window.max * cfg->clk_freq_khz;

	return 0;
}

static int corstone_wdt_feed(const struct device *dev, int channel_id)
{
	const struct corstone_wdt_cfg *cfg = dev->config;

	cfg->ref_base->wrr = 1;
	irq_disable(cfg->irq_warning);
	irq_clearpending(cfg->irq_warning);
	irq_enable(cfg->irq_warning);
	irq_disable(cfg->irq_expire);
	irq_clearpending(cfg->irq_expire);
	irq_enable(cfg->irq_expire);

	return 0;
}

static DEVICE_API(wdt, corstone_wdt_api) = {
	.setup = corstone_wdt_setup,
	.disable = corstone_wdt_disable,
	.install_timeout = corstone_wdt_install_timeout,
	.feed = corstone_wdt_feed,
};

static void corstone_wdt_irq_warning(const struct device *dev)
{
	const struct corstone_wdt_cfg *cfg = dev->config;
	struct corstone_wdt_data *data = dev->data;

	irq_disable(cfg->irq_warning);

	data->callback_warning(dev, 0);
}

static void corstone_wdt_irq_expire(const struct device *dev)
{
	const struct corstone_wdt_cfg *cfg = dev->config;
	struct corstone_wdt_data *data = dev->data;

	irq_disable(cfg->irq_expire);

	data->callback_expire(dev, 0);
}

#define CORSTONE_WDT_INIT(inst)                                              \
	static struct corstone_wdt_data corstone_wdt_data_##inst;            \
	static const struct corstone_wdt_cfg corstone_wdt_cfg_##inst = { \
		.ctrl_base = (struct wdog_corstone_ctrl *)DT_INST_REG_ADDR(inst),    \
		.ref_base = (struct wdog_corstone_ref *)(DT_INST_REG_ADDR(inst) + 0x1000),    \
		.fc_dev = DEVICE_DT_GET(FLIGHT_CONTROL_NODE(inst)),							\
		.irq_warning = DT_INST_IRQN(inst),                             \
		.irq_expire = DT_INST_IRQN_BY_IDX(inst, 1),                    \
		.fault_num = DT_INST_PROP(inst, fault_num),					   \
		.clk_freq_khz = WATCHDOG_CORSTONE_FREQ(inst) / 1000,	\
	};                                                                   \
	static int corstone_wdt_init_##inst(const struct device *dev) {      \
		const struct device *syscounter =                   \
        	DEVICE_DT_GET(CORSTONE_SYSCNT_NODE(inst));		\
															\
		if (!device_is_ready(syscounter)) {                 \
        	return -ENODEV;                                 \
    	}  													\
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),   \
			    corstone_wdt_irq_warning, DEVICE_DT_INST_GET(inst), 0); \
		IRQ_CONNECT(DT_INST_IRQN_BY_IDX(inst, 1),                       \
			    DT_INST_IRQ_BY_IDX(inst, 1, priority),               \
			    corstone_wdt_irq_expire, DEVICE_DT_INST_GET(inst), 0); \
		return 0;                                                      \
	}                                                                   \
	DEVICE_DT_INST_DEFINE(inst, corstone_wdt_init_##inst, NULL,          \
			      &corstone_wdt_data_##inst,                       \
			      &corstone_wdt_cfg_##inst, POST_KERNEL,         \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,              \
			      &corstone_wdt_api);

DT_INST_FOREACH_STATUS_OKAY(CORSTONE_WDT_INIT);