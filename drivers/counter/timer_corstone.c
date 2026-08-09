/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT arm_corstone_timer

#include <limits.h>

#include <zephyr/drivers/counter.h>
#include <zephyr/device.h>
#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/irq_extend.h>
#include <soc.h>
#include <zephyr/drivers/clock_control/arm_clock_control.h>

#include "timer_corstone.h"


#define CORSTONE_SYSCNT_NODE(inst)  DT_PHANDLE(DT_DRV_INST(inst), system_counter)
#define CORSTONE_CLOCK_NODE(inst)   DT_PHANDLE(CORSTONE_SYSCNT_NODE(inst), clocks)
#define TIMER_CORSTONE_FREQ(inst)   DT_PROP(CORSTONE_CLOCK_NODE(inst), clock_frequency)

typedef void (*timer_config_func_t)(const struct device *dev);

struct tmr_corstone_cfg {
	struct counter_config_info info;
	volatile struct timer_corstone *timer;
	timer_config_func_t timer_config_func;
	uint32_t irq_num;
};

struct tmr_corstone_dev_data {
	counter_top_callback_t top_callback;
	void *top_user_data;

	uint32_t load;
};

static int tmr_corstone_start(const struct device *dev)
{
	const struct tmr_corstone_cfg * const cfg =
						dev->config;
	struct tmr_corstone_dev_data *data = dev->data;

	/* Set timeout value in clock ticks by setting CNTP_TVAL */
	cfg->timer->tval = data->load;
	cfg->timer->ctl = TIMER_SYSTEM_TIMER_DISABLE_IMASK;

	return 0;
}

static int tmr_corstone_stop(const struct device *dev)
{
	const struct tmr_corstone_cfg * const cfg =
						dev->config;
	
	irq_disable(cfg->irq_num);
	cfg->timer->ctl = TIMER_SYSTEM_TIMER_DISABLE;
	cfg->timer->ctl = TIMER_SYSTEM_TIMER_ENABLE_IMASK;
	cfg->timer->tval = 0;

	return 0;
}

static int tmr_corstone_get_value(const struct device *dev, uint32_t *ticks)
{
	const struct tmr_corstone_cfg * const cfg =
						dev->config;
	struct tmr_corstone_dev_data *data = dev->data;

	/* Get Counter Value */
	*ticks = data->load - cfg->timer->tval;
	return 0;
}

static int tmr_corstone_set_top_value(const struct device *dev,
				       const struct counter_top_cfg *top_cfg)
{
	const struct tmr_corstone_cfg * const cfg =
						dev->config;
	struct tmr_corstone_dev_data *data = dev->data;

	/* Counter is always reset when top value is updated. */
	if (top_cfg->flags & COUNTER_TOP_CFG_DONT_RESET) {
		return -ENOTSUP;
	}

	data->top_callback = top_cfg->callback;
	data->top_user_data = top_cfg->user_data;

	/* Store the reload value */
	data->load = top_cfg->ticks;

	/* Set timeout value in clock ticks by setting CNTP_TVAL */
	cfg->timer->ctl = TIMER_SYSTEM_TIMER_DISABLE;
	cfg->timer->tval = data->load;
	cfg->timer->ctl = TIMER_SYSTEM_TIMER_DISABLE_IMASK;

	irq_clearpending(cfg->irq_num);
	irq_enable(cfg->irq_num);

	return 0;
}

static uint32_t tmr_corstone_get_top_value(const struct device *dev)
{
	struct tmr_corstone_dev_data *data = dev->data;

	uint32_t ticks = data->load;

	return ticks;
}

static uint32_t tmr_corstone_get_pending_int(const struct device *dev)
{
	const struct tmr_corstone_cfg * const cfg =
						dev->config;

	uint32_t intstatus = (cfg->timer->ctl & TIMER_CTL_ISTATUS) ? 1 : 0;
	return intstatus;
}

static DEVICE_API(counter, tmr_corstone_api) = {
	.start = tmr_corstone_start,
	.stop = tmr_corstone_stop,
	.get_value = tmr_corstone_get_value,
	.set_top_value = tmr_corstone_set_top_value,
	.get_pending_int = tmr_corstone_get_pending_int,
	.get_top_value = tmr_corstone_get_top_value,
};

static void tmr_corstone_isr(void *arg)
{
	const struct device *dev = (const struct device *)arg;
	struct tmr_corstone_dev_data *data = dev->data;
	const struct tmr_corstone_cfg * const cfg =
						dev->config;

	irq_disable(cfg->irq_num);
	// Write tval with same load value to clear the ISTATUS bit in control register
	cfg->timer->ctl = TIMER_SYSTEM_TIMER_DISABLE;
	cfg->timer->tval = data->load;
	cfg->timer->ctl = TIMER_SYSTEM_TIMER_DISABLE_IMASK;
	if (data->top_callback) {
		data->top_callback(dev, data->top_user_data);
	}
	irq_clearpending(cfg->irq_num);
	irq_enable(cfg->irq_num);
}

#define TIMER_CORSTONE_INIT(inst)						\
	static int tmr_corstone_init_##inst(const struct device *dev)	\
	{														\
		const struct device *syscounter =                   \
        	DEVICE_DT_GET(CORSTONE_SYSCNT_NODE(inst));		\
															\
		if (!device_is_ready(syscounter)) {                 \
        	return -ENODEV;                                 \
    	}  													\
															\
		const struct tmr_corstone_cfg * const cfg =			\
							dev->config;					\
															\
		/* Disable interrupt by setting CNTP_CTL.IMASK = 1 (IMPORTANT - otherwise the interrupt will constantly trigger) */	\
		cfg->timer->ctl |= TIMER_CTL_IMASK;					\
		/* Disable autoinc by clearing CNTP_AIVAL_CTL.EN */	\
		cfg->timer->aival_ctl &= ~(TIMER_AIVAL_CTL_EN);		\
		/* Set counter frequency by setting CNTFRQ with a Hz value (does not actually affect the timer) */	\
		cfg->timer->cntfrq = cfg->info.freq;				\
		cfg->timer->ctl |= TIMER_CTL_ENABLE;				\
															\
		cfg->timer_config_func(dev);						\
															\
		return 0;											\
	}														\
															\
	static void timer_corstone_config_##inst(const struct device *dev); \
									\
	static const struct tmr_corstone_cfg tmr_corstone_cfg_##inst = { \
		.info = {						\
			.max_top_value = UINT32_MAX,			\
			.freq = TIMER_CORSTONE_FREQ(inst),			\
			.flags = COUNTER_CONFIG_INFO_COUNT_UP,		\
			.channels = 0U,					\
		},							\
		.timer = ((volatile struct timer_corstone *)DT_INST_REG_ADDR(inst)), \
		.timer_config_func = timer_corstone_config_##inst,	\
		.irq_num = DT_INST_IRQN(inst) \
	};								\
									\
	static struct tmr_corstone_dev_data tmr_corstone_dev_data_##inst = { \
		.load = UINT32_MAX,					\
	};								\
									\
	DEVICE_DT_INST_DEFINE(inst,					\
			    tmr_corstone_init_##inst,				\
			    NULL,			\
			    &tmr_corstone_dev_data_##inst,		\
			    &tmr_corstone_cfg_##inst, POST_KERNEL,	\
			    CONFIG_COUNTER_INIT_PRIORITY,		\
			    &tmr_corstone_api);			\
									\
	static void timer_corstone_config_##inst(const struct device *dev) \
	{								\
		IRQ_CONNECT(DT_INST_IRQN(inst),				\
			    DT_INST_IRQ(inst, priority),		\
			    tmr_corstone_isr,				\
			    DEVICE_DT_INST_GET(inst),			\
			    0);						\
		irq_disable(DT_INST_IRQN(inst));				\
	}

DT_INST_FOREACH_STATUS_OKAY(TIMER_CORSTONE_INIT)
