/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_tcpwm

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define TCPWM_SYNA_GROUP_COUNT       12u
#define TCPWM_SYNA_GROUP_STRIDE      0x64u

#define TCPWM_SYNA_GRP_CLK_CTRL      0x00u
#define TCPWM_SYNA_GRP_CTRL          0x04u
#define TCPWM_SYNA_GRP_STATUS        0x08u
#define TCPWM_SYNA_GRP_CC0           0x0Cu
#define TCPWM_SYNA_GRP_CC1           0x10u
#define TCPWM_SYNA_GRP_PERIOD        0x14u
#define TCPWM_SYNA_GRP_COUNTER       0x18u
#define TCPWM_SYNA_GRP_PWM_CTRL      0x1Cu
#define TCPWM_SYNA_GRP_LINE_SEL      0x20u
#define TCPWM_SYNA_GRP_CC0_BUFF      0x24u
#define TCPWM_SYNA_GRP_CC1_BUFF      0x28u
#define TCPWM_SYNA_GRP_PERIOD_BUFF   0x2Cu
#define TCPWM_SYNA_GRP_LINE_SEL_BUFF 0x30u
#define TCPWM_SYNA_GRP_TRIN_CTRL0    0x34u
#define TCPWM_SYNA_GRP_TRIN_CTRL1    0x38u
#define TCPWM_SYNA_GRP_TRIN_EDGE_SEL 0x3Cu
#define TCPWM_SYNA_GRP_TR_CMD        0x44u
#define TCPWM_SYNA_GRP_INTR_STA      0x48u
#define TCPWM_SYNA_GRP_INTR_MASK     0x50u

#define TCPWM_SYNA_CLKDIV0_DIVCTRL   0x4B0u
#define TCPWM_SYNA_CLKDIV0_DIVSTS    0x4B4u
#define TCPWM_SYNA_DIV_CMD           0x504u

#define TCPWM_SYNA_DIVSTS_EN      BIT(0)
#define TCPWM_SYNA_DIV_CMD_ENABLE BIT(31)

/* CTRL */
#define TCPWM_SYNA_CTRL_AUTO_RELOAD_CC0     BIT(0)
#define TCPWM_SYNA_CTRL_AUTO_RELOAD_CC1     BIT(1)
#define TCPWM_SYNA_CTRL_AUTO_RELOAD_PERIOD  BIT(2)
#define TCPWM_SYNA_CTRL_AUTO_RELOAD_LINESEL BIT(3)
#define TCPWM_SYNA_CTRL_CC0_MATCH_UP_EN     BIT(4)
#define TCPWM_SYNA_CTRL_DISABLE_MODE_LSB 12u
#define TCPWM_SYNA_CTRL_UP_DOWN_MODE_LSB 16u
#define TCPWM_SYNA_CTRL_MODE_LSB         24u
#define TCPWM_SYNA_CTRL_ENABLED          BIT(31)

/* PWM_CTRL */
#define TCPWM_SYNA_PWM_CTRL_CC0_MATCH_MODE_LSB  0u
#define TCPWM_SYNA_PWM_CTRL_OVERFLOW_MODE_LSB   2u
#define TCPWM_SYNA_PWM_CTRL_UNDERFLOW_MODE_LSB  4u

/* LINE_SEL */
#define TCPWM_SYNA_LINE_SEL_OUT_LSB       0u
#define TCPWM_SYNA_LINE_SEL_COMPL_OUT_LSB 4u

/* TRIN_CTRL0/1 */
#define TCPWM_SYNA_TRIN_CTRL0_CAPTURE0_SEL_LSB 0u
#define TCPWM_SYNA_TRIN_CTRL0_COUNT_SEL_LSB    8u
#define TCPWM_SYNA_TRIN_CTRL1_START_SEL_LSB    0u
#define TCPWM_SYNA_TRIN_CTRL1_CAPTURE1_SEL_LSB 8u

/* TRIN_EDGE_SEL */
#define TCPWM_SYNA_CAPTURE0_EDGE_LSB 0u
#define TCPWM_SYNA_COUNT_EDGE_LSB    2u
#define TCPWM_SYNA_RELOAD_EDGE_LSB   4u
#define TCPWM_SYNA_STOP_EDGE_LSB     6u
#define TCPWM_SYNA_START_EDGE_LSB    8u
#define TCPWM_SYNA_CAPTURE1_EDGE_LSB 10u

/* INTR_STA/INTR_MASK: zero unmasks an interrupt source. */
#define TCPWM_SYNA_INTR_TC  BIT(0)
#define TCPWM_SYNA_INTR_CC0 BIT(1)
#define TCPWM_SYNA_INTR_CC1 BIT(2)
#define TCPWM_SYNA_INTR_ALL (TCPWM_SYNA_INTR_TC | TCPWM_SYNA_INTR_CC0 | \
                             TCPWM_SYNA_INTR_CC1)


/* Field values */
#define TCPWM_SYNA_MODE_CAPTURE     2u
#define TCPWM_SYNA_MODE_PWM         4u
#define TCPWM_SYNA_COUNT_UP         0u
#define TCPWM_SYNA_ACTION_SET       0u
#define TCPWM_SYNA_ACTION_CLEAR     1u
#define TCPWM_SYNA_ACTION_NO_CHANGE 3u
#define TCPWM_SYNA_DISABLE_LOW      2u
#define TCPWM_SYNA_LINE_SRC_PWM     2u
#define TCPWM_SYNA_LINE_SRC_INV     3u
#define TCPWM_SYNA_TRIN_1           1u
#define TCPWM_SYNA_TRIN_SM_CAP0     2u
#define TCPWM_SYNA_TR_RISING_EDGE   0u
#define TCPWM_SYNA_TR_FALLING_EDGE  1u
#define TCPWM_SYNA_TR_NO_EDGE       3u
#define TCPWM_SYNA_NO_EDGE_ALL      0xFFFu

#define TCPWM_SYNA_GBL_CTRL_OFFSET      0x0850u
#define TCPWM_SYNA_GBL_SRAM_PWR_OFFSET  0x0A08u
#define TCPWM_SYNA_PINS_PER_REG         6u
#define TCPWM_SYNA_OUTSEL_BITS          4u
#define TCPWM_SYNA_OUTSEL_MASK          0xFu
#define TCPWM_SYNA_COMPL_OUT_LSB        24u

struct tcpwm_syna_output_state {
	uint32_t period;
	uint32_t pulse;
	pwm_flags_t flags;
	bool configured;
};

#ifdef CONFIG_PWM_CAPTURE
struct tcpwm_syna_capture_state {
	pwm_capture_callback_handler_t callback;
	void *user_data;
	pwm_flags_t flags;
	uint32_t first_edge;
	uint32_t pulse_edge;
	bool configured;
	bool active;
	bool first_valid;
	bool pulse_valid;
	bool settling;
};
#endif

struct tcpwm_syna_data {
	struct k_spinlock lock;
	struct tcpwm_syna_output_state output[TCPWM_SYNA_GROUP_COUNT];
#ifdef CONFIG_PWM_CAPTURE
	struct tcpwm_syna_capture_state capture;
#endif
};

struct tcpwm_syna_config {
	uintptr_t base;
	uintptr_t gbl_base;
	uint32_t clock_freq;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pcfg;
	
#ifdef CONFIG_PWM_CAPTURE
	uint8_t capture_channel;
	uint8_t capture_input;
	uint32_t capture_irq;
	void (*irq_config_func)(const struct device *dev);
#endif
};

static inline uintptr_t grp_reg(const struct tcpwm_syna_config *cfg, uint32_t grp, uint32_t off)
{
	return cfg->base + (grp * TCPWM_SYNA_GROUP_STRIDE) + off;
}

/* Routes TCPWM output pin `pin` to `grp`; pinmux alone does not connect them.
 */
static void tcpwm_syna_route(const struct tcpwm_syna_config *cfg, uint32_t pin, uint32_t grp)
{
	uintptr_t addr =
		cfg->gbl_base + TCPWM_SYNA_GBL_CTRL_OFFSET + ((pin / TCPWM_SYNA_PINS_PER_REG) * 4U);
	uint32_t shift = (pin % TCPWM_SYNA_PINS_PER_REG) * TCPWM_SYNA_OUTSEL_BITS;
	uint32_t compl = TCPWM_SYNA_COMPL_OUT_LSB + (pin % TCPWM_SYNA_PINS_PER_REG);
	uint32_t val = sys_read32(addr);

	val &= ~((TCPWM_SYNA_OUTSEL_MASK << shift) | BIT(compl));
	val |= (grp & TCPWM_SYNA_OUTSEL_MASK) << shift;
	sys_write32(val, addr);
}

static int tcpwm_syna_hw_init(const struct device *dev)
{
	const struct tcpwm_syna_config *cfg = dev->config;
	int ret;

	ret = clock_control_on(cfg->clock_dev, cfg->clock_id);
	if (ret < 0) {
		return ret;
	}

	sys_write32(0U, cfg->gbl_base + TCPWM_SYNA_GBL_SRAM_PWR_OFFSET);
	sys_write32(0U, cfg->base + TCPWM_SYNA_CLKDIV0_DIVCTRL);
	sys_write32(0U, cfg->base + TCPWM_SYNA_DIV_CMD);
	sys_write32(TCPWM_SYNA_DIV_CMD_ENABLE, cfg->base + TCPWM_SYNA_DIV_CMD);

	if (!WAIT_FOR((sys_read32(cfg->base + TCPWM_SYNA_CLKDIV0_DIVSTS) & TCPWM_SYNA_DIVSTS_EN) !=
			      0U,
		      1000, k_busy_wait(1))) {
		return -ETIMEDOUT;
	}

	return pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
}

static void tcpwm_syna_program_output(const struct tcpwm_syna_config *cfg, uint32_t channel,
				      uint32_t period_cycles, uint32_t pulse_cycles,
				      pwm_flags_t flags)
{
	uint32_t out_sel;
	uint32_t compl_sel;
	uint32_t ctrl;

	if ((flags & PWM_POLARITY_INVERTED) != 0U) {
		out_sel = TCPWM_SYNA_LINE_SRC_INV;
		compl_sel = TCPWM_SYNA_LINE_SRC_PWM;
	} else {
		out_sel = TCPWM_SYNA_LINE_SRC_PWM;
		compl_sel = TCPWM_SYNA_LINE_SRC_INV;
	}

	sys_write32(0U, grp_reg(cfg, channel, TCPWM_SYNA_GRP_CTRL));
	sys_write32(0U, grp_reg(cfg, channel, TCPWM_SYNA_GRP_CLK_CTRL));
	sys_write32(period_cycles, grp_reg(cfg, channel, TCPWM_SYNA_GRP_PERIOD));
	sys_write32(period_cycles, grp_reg(cfg, channel, TCPWM_SYNA_GRP_PERIOD_BUFF));
	sys_write32(pulse_cycles, grp_reg(cfg, channel, TCPWM_SYNA_GRP_CC0));
	sys_write32(pulse_cycles, grp_reg(cfg, channel, TCPWM_SYNA_GRP_CC0_BUFF));
	sys_write32(0U, grp_reg(cfg, channel, TCPWM_SYNA_GRP_COUNTER));

	sys_write32((TCPWM_SYNA_ACTION_SET << TCPWM_SYNA_PWM_CTRL_OVERFLOW_MODE_LSB) |
			    (TCPWM_SYNA_ACTION_CLEAR << TCPWM_SYNA_PWM_CTRL_CC0_MATCH_MODE_LSB) |
			    (TCPWM_SYNA_ACTION_NO_CHANGE << TCPWM_SYNA_PWM_CTRL_UNDERFLOW_MODE_LSB),
		    grp_reg(cfg, channel, TCPWM_SYNA_GRP_PWM_CTRL));
	sys_write32((out_sel << TCPWM_SYNA_LINE_SEL_OUT_LSB) |
			    (compl_sel << TCPWM_SYNA_LINE_SEL_COMPL_OUT_LSB),
		    grp_reg(cfg, channel, TCPWM_SYNA_GRP_LINE_SEL));
	sys_write32((out_sel << TCPWM_SYNA_LINE_SEL_OUT_LSB) |
			    (compl_sel << TCPWM_SYNA_LINE_SEL_COMPL_OUT_LSB),
		    grp_reg(cfg, channel, TCPWM_SYNA_GRP_LINE_SEL_BUFF));
	sys_write32(TCPWM_SYNA_TRIN_1 << TCPWM_SYNA_TRIN_CTRL0_COUNT_SEL_LSB,
		    grp_reg(cfg, channel, TCPWM_SYNA_GRP_TRIN_CTRL0));
	sys_write32(TCPWM_SYNA_TRIN_1 << TCPWM_SYNA_TRIN_CTRL1_START_SEL_LSB,
		    grp_reg(cfg, channel, TCPWM_SYNA_GRP_TRIN_CTRL1));
	sys_write32(TCPWM_SYNA_NO_EDGE_ALL, grp_reg(cfg, channel, TCPWM_SYNA_GRP_TRIN_EDGE_SEL));

	tcpwm_syna_route(cfg, channel, channel);

	ctrl = (TCPWM_SYNA_MODE_PWM << TCPWM_SYNA_CTRL_MODE_LSB) |
	       (TCPWM_SYNA_COUNT_UP << TCPWM_SYNA_CTRL_UP_DOWN_MODE_LSB) |
	       TCPWM_SYNA_CTRL_AUTO_RELOAD_CC0 | TCPWM_SYNA_CTRL_AUTO_RELOAD_PERIOD |
	       TCPWM_SYNA_CTRL_AUTO_RELOAD_LINESEL | TCPWM_SYNA_CTRL_CC0_MATCH_UP_EN |
	       (TCPWM_SYNA_DISABLE_LOW << TCPWM_SYNA_CTRL_DISABLE_MODE_LSB);

	if (pulse_cycles == 0U) {
		sys_write32(ctrl, grp_reg(cfg, channel, TCPWM_SYNA_GRP_CTRL));
		return;
	}

	sys_write32(ctrl | TCPWM_SYNA_CTRL_ENABLED, grp_reg(cfg, channel, TCPWM_SYNA_GRP_CTRL));
	sys_set_bit(grp_reg(cfg, channel, TCPWM_SYNA_GRP_TR_CMD), 0U);
	sys_clear_bit(grp_reg(cfg, channel, TCPWM_SYNA_GRP_TR_CMD), 0U);
}

#ifdef CONFIG_PWM_CAPTURE
static void tcpwm_syna_capture_stop(const struct tcpwm_syna_config *cfg)
{
	uint32_t channel = cfg->capture_channel;

	sys_write32(TCPWM_SYNA_INTR_ALL, grp_reg(cfg, channel, TCPWM_SYNA_GRP_INTR_MASK));
	sys_write32(0U, grp_reg(cfg, channel, TCPWM_SYNA_GRP_CTRL));
}

static void tcpwm_syna_program_capture(const struct tcpwm_syna_config *cfg,
				       struct tcpwm_syna_capture_state *capture)
{
	uint32_t channel = cfg->capture_channel;
	uint32_t source = TCPWM_SYNA_TRIN_SM_CAP0 + cfg->capture_input;
	uint32_t active_edge = TCPWM_SYNA_TR_RISING_EDGE;
	uint32_t inactive_edge = TCPWM_SYNA_TR_FALLING_EDGE;
	uint32_t ctrl;

	if ((capture->flags & PWM_POLARITY_INVERTED) != 0U) {
		active_edge = TCPWM_SYNA_TR_FALLING_EDGE;
		inactive_edge = TCPWM_SYNA_TR_RISING_EDGE;
	}

	tcpwm_syna_capture_stop(cfg);
	sys_write32(0U, grp_reg(cfg, channel, TCPWM_SYNA_GRP_CLK_CTRL));
	sys_write32(UINT32_MAX, grp_reg(cfg, channel, TCPWM_SYNA_GRP_PERIOD));
	sys_write32(0U, grp_reg(cfg, channel, TCPWM_SYNA_GRP_COUNTER));
	sys_write32(source << TCPWM_SYNA_TRIN_CTRL0_CAPTURE0_SEL_LSB |
			    TCPWM_SYNA_TRIN_1 << TCPWM_SYNA_TRIN_CTRL0_COUNT_SEL_LSB,
		    grp_reg(cfg, channel, TCPWM_SYNA_GRP_TRIN_CTRL0));
	sys_write32(TCPWM_SYNA_TRIN_1 << TCPWM_SYNA_TRIN_CTRL1_START_SEL_LSB |
			    source << TCPWM_SYNA_TRIN_CTRL1_CAPTURE1_SEL_LSB,
		    grp_reg(cfg, channel, TCPWM_SYNA_GRP_TRIN_CTRL1));
	sys_write32(active_edge << TCPWM_SYNA_CAPTURE0_EDGE_LSB |
			    TCPWM_SYNA_TR_NO_EDGE << TCPWM_SYNA_COUNT_EDGE_LSB |
			    TCPWM_SYNA_TR_NO_EDGE << TCPWM_SYNA_RELOAD_EDGE_LSB |
			    TCPWM_SYNA_TR_NO_EDGE << TCPWM_SYNA_STOP_EDGE_LSB |
			    TCPWM_SYNA_TR_RISING_EDGE << TCPWM_SYNA_START_EDGE_LSB |
			    inactive_edge << TCPWM_SYNA_CAPTURE1_EDGE_LSB,
		    grp_reg(cfg, channel, TCPWM_SYNA_GRP_TRIN_EDGE_SEL));
	sys_write32(TCPWM_SYNA_INTR_ALL, grp_reg(cfg, channel, TCPWM_SYNA_GRP_INTR_STA));
	/* Keep terminal-count masked and enable both capture/compare sources. */
	sys_write32(TCPWM_SYNA_INTR_TC, grp_reg(cfg, channel, TCPWM_SYNA_GRP_INTR_MASK));

	ctrl = (TCPWM_SYNA_MODE_CAPTURE << TCPWM_SYNA_CTRL_MODE_LSB) |
	       (TCPWM_SYNA_COUNT_UP << TCPWM_SYNA_CTRL_UP_DOWN_MODE_LSB) | TCPWM_SYNA_CTRL_ENABLED;
	sys_write32(ctrl, grp_reg(cfg, channel, TCPWM_SYNA_GRP_CTRL));
	sys_set_bit(grp_reg(cfg, channel, TCPWM_SYNA_GRP_TR_CMD), 4U);
	sys_clear_bit(grp_reg(cfg, channel, TCPWM_SYNA_GRP_TR_CMD), 4U);
}

static void tcpwm_syna_isr(const struct device *dev)
{
	const struct tcpwm_syna_config *cfg = dev->config;
	struct tcpwm_syna_data *data = dev->data;
	struct tcpwm_syna_capture_state *capture = &data->capture;
	pwm_capture_callback_handler_t callback = NULL;
	void *user_data = NULL;
	uint32_t channel = cfg->capture_channel;
	uint32_t status;
	uint32_t period = 0U;
	uint32_t pulse = 0U;
	bool complete = false;
	k_spinlock_key_t key;

	status = sys_read32(grp_reg(cfg, channel, TCPWM_SYNA_GRP_INTR_STA)) & TCPWM_SYNA_INTR_ALL;
	if (status == 0U) {
		return;
	}
	sys_write32(status, grp_reg(cfg, channel, TCPWM_SYNA_GRP_INTR_STA));

	key = k_spin_lock(&data->lock);
	if (!capture->active) {
		k_spin_unlock(&data->lock, key);
		return;
	}

	if ((status & TCPWM_SYNA_INTR_CC0) != 0U) {
		uint32_t edge = sys_read32(grp_reg(cfg, channel, TCPWM_SYNA_GRP_CC0));

		if (!capture->first_valid) {
			capture->first_edge = edge;
			capture->first_valid = true;
		} else if ((capture->flags & PWM_CAPTURE_TYPE_PERIOD) != 0U) {
			period = edge - capture->first_edge;
			if ((capture->flags & PWM_CAPTURE_TYPE_PULSE) == 0U ||
			    capture->pulse_valid) {
				pulse = capture->pulse_edge - capture->first_edge;
				complete = true;
			}
			capture->first_edge = edge;
			capture->pulse_valid = false;
		}
	}

	if ((status & TCPWM_SYNA_INTR_CC1) != 0U && capture->first_valid) {
		capture->pulse_edge = sys_read32(grp_reg(cfg, channel, TCPWM_SYNA_GRP_CC1));
		capture->pulse_valid = true;
		if ((capture->flags & PWM_CAPTURE_TYPE_PERIOD) == 0U) {
			pulse = capture->pulse_edge - capture->first_edge;
			complete = true;
		}
	}

	if (complete && capture->settling) {
		/* Ignore the first result while the input and capture state synchronize. */
		capture->settling = false;
		capture->first_valid = false;
		capture->pulse_valid = false;
		complete = false;
	}

	if (complete) {
		callback = capture->callback;
		user_data = capture->user_data;
		if ((capture->flags & PWM_CAPTURE_MODE_MASK) == PWM_CAPTURE_MODE_SINGLE) {
			capture->active = false;
			tcpwm_syna_capture_stop(cfg);
		} else if ((capture->flags & PWM_CAPTURE_TYPE_PERIOD) == 0U) {
			capture->first_valid = false;
			capture->pulse_valid = false;
		}
	}
	k_spin_unlock(&data->lock, key);

	if (callback != NULL && complete) {
		callback(dev, channel, period, pulse, 0, user_data);
	}
}

static int tcpwm_syna_configure_capture(const struct device *dev, uint32_t channel,
					pwm_flags_t flags, pwm_capture_callback_handler_t callback,
					void *user_data)
{
	const struct tcpwm_syna_config *cfg = dev->config;
	struct tcpwm_syna_data *data = dev->data;
	struct tcpwm_syna_capture_state *capture = &data->capture;
	pwm_flags_t valid_flags =
		PWM_POLARITY_INVERTED | PWM_CAPTURE_TYPE_MASK | PWM_CAPTURE_MODE_MASK;
	k_spinlock_key_t key;

	if (channel != cfg->capture_channel) {
		return -EINVAL;
	}
	if ((flags & ~valid_flags) != 0U || (flags & PWM_CAPTURE_TYPE_MASK) == 0U) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	if (capture->active) {
		k_spin_unlock(&data->lock, key);
		return -EBUSY;
	}

	capture->callback = callback;
	capture->user_data = user_data;
	capture->flags = flags;
	capture->configured = callback != NULL;
	capture->first_valid = false;
	capture->pulse_valid = false;
	k_spin_unlock(&data->lock, key);
	return 0;
}

static int tcpwm_syna_enable_capture(const struct device *dev, uint32_t channel)
{
	const struct tcpwm_syna_config *cfg = dev->config;
	struct tcpwm_syna_data *data = dev->data;
	struct tcpwm_syna_capture_state *capture = &data->capture;
	k_spinlock_key_t key;

	if (channel != cfg->capture_channel) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	if (!capture->configured || capture->callback == NULL) {
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}
	if (capture->active) {
		k_spin_unlock(&data->lock, key);
		return -EBUSY;
	}
	if (data->output[channel].configured && data->output[channel].pulse != 0U) {
		k_spin_unlock(&data->lock, key);
		return -EBUSY;
	}

	capture->first_valid = false;
	capture->pulse_valid = false;
	capture->settling = true;
	capture->active = true;
	tcpwm_syna_program_capture(cfg, capture);
	irq_enable(cfg->capture_irq);
	k_spin_unlock(&data->lock, key);
	return 0;
}

static int tcpwm_syna_disable_capture(const struct device *dev, uint32_t channel)
{
	const struct tcpwm_syna_config *cfg = dev->config;
	struct tcpwm_syna_data *data = dev->data;
	k_spinlock_key_t key;

	if (channel != cfg->capture_channel) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	data->capture.active = false;
	data->capture.first_valid = false;
	data->capture.pulse_valid = false;
	tcpwm_syna_capture_stop(cfg);
	k_spin_unlock(&data->lock, key);
	return 0;
}
#endif /* CONFIG_PWM_CAPTURE */

static int tcpwm_syna_set_cycles(const struct device *dev, uint32_t channel, uint32_t period_cycles,
				 uint32_t pulse_cycles, pwm_flags_t flags)
{
	const struct tcpwm_syna_config *cfg = dev->config;
	struct tcpwm_syna_data *data = dev->data;
	k_spinlock_key_t key;

	if (channel >= TCPWM_SYNA_GROUP_COUNT || period_cycles == 0U ||
	    pulse_cycles > period_cycles) {
		return -EINVAL;
	}
	if ((flags & ~PWM_POLARITY_INVERTED) != 0U) {
		return -ENOTSUP;
	}

	key = k_spin_lock(&data->lock);
#ifdef CONFIG_PWM_CAPTURE
	if (channel == cfg->capture_channel && data->capture.active) {
		k_spin_unlock(&data->lock, key);
		return -EBUSY;
	}
#endif
	tcpwm_syna_program_output(cfg, channel, period_cycles, pulse_cycles, flags);
	data->output[channel].period = period_cycles;
	data->output[channel].pulse = pulse_cycles;
	data->output[channel].flags = flags;
	data->output[channel].configured = true;
	k_spin_unlock(&data->lock, key);
	return 0;
}

static int tcpwm_syna_get_cycles_per_sec(const struct device *dev, uint32_t channel,
					 uint64_t *cycles)
{
	const struct tcpwm_syna_config *cfg = dev->config;

	if (channel >= TCPWM_SYNA_GROUP_COUNT || cycles == NULL) {
		return -EINVAL;
	}

	*cycles = (uint64_t)cfg->clock_freq;
	return 0;
}


static int tcpwm_syna_init(const struct device *dev)
{
	const struct tcpwm_syna_config *cfg = dev->config;
	int ret;

	if (!device_is_ready(cfg->clock_dev) || !device_is_ready(cfg->reset.dev)) {
		return -ENODEV;
	}

	ret = reset_line_toggle_dt(&cfg->reset);
	if (ret != 0) {
		return ret;
	}

	ret = tcpwm_syna_hw_init(dev);
	if (ret < 0) {
		return ret;
	}

#ifdef CONFIG_PWM_CAPTURE
	cfg->irq_config_func(dev);
	tcpwm_syna_capture_stop(cfg);
#endif
	return 0;
}

static DEVICE_API(pwm, tcpwm_syna_api) = {
	.set_cycles = tcpwm_syna_set_cycles,
	.get_cycles_per_sec = tcpwm_syna_get_cycles_per_sec,
#ifdef CONFIG_PWM_CAPTURE
	.configure_capture = tcpwm_syna_configure_capture,
	.enable_capture = tcpwm_syna_enable_capture,
	.disable_capture = tcpwm_syna_disable_capture,
#endif
};

#ifdef CONFIG_PWM_CAPTURE
#define TCPWM_SYNA_IRQ_CONFIG(inst)                                                                \
	static void tcpwm_syna_irq_config_##inst(const struct device *dev)                         \
	{                                                                                          \
		IRQ_CONNECT(                                                                       \
			DT_INST_IRQ_BY_IDX(inst, DT_INST_PROP(inst, capture_channel), irq),        \
			DT_INST_IRQ_BY_IDX(inst, DT_INST_PROP(inst, capture_channel), priority),   \
			tcpwm_syna_isr, DEVICE_DT_INST_GET(inst), 0);                              \
	}
#define TCPWM_SYNA_CAPTURE_CONFIG(inst)                                                            \
	.capture_channel = DT_INST_PROP(inst, capture_channel),                                    \
	.capture_input = DT_INST_PROP(inst, capture_input),                                        \
	.capture_irq = DT_INST_IRQ_BY_IDX(inst, DT_INST_PROP(inst, capture_channel), irq),         \
	.irq_config_func = tcpwm_syna_irq_config_##inst,
#else
#define TCPWM_SYNA_IRQ_CONFIG(inst)
#define TCPWM_SYNA_CAPTURE_CONFIG(inst)
#endif

#define TCPWM_SYNA_INIT(inst)                                                                      \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	TCPWM_SYNA_IRQ_CONFIG(inst)                                                                \
	static struct tcpwm_syna_data tcpwm_syna_data_##inst;                                      \
	static const struct tcpwm_syna_config tcpwm_syna_cfg_##inst = {                            \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.gbl_base = DT_REG_ADDR(DT_INST_PHANDLE(inst, gbl)),                               \
		.clock_freq = DT_INST_PROP(inst, clock_frequency),                                 \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),              \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                             \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		TCPWM_SYNA_CAPTURE_CONFIG(inst)};                                                  \
	DEVICE_DT_INST_DEFINE(inst, tcpwm_syna_init, NULL,                  \
			      &tcpwm_syna_data_##inst, &tcpwm_syna_cfg_##inst, POST_KERNEL,        \
			      CONFIG_PWM_INIT_PRIORITY, &tcpwm_syna_api);

DT_INST_FOREACH_STATUS_OKAY(TCPWM_SYNA_INIT)
