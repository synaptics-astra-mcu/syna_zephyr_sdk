/*
 * Copyright (C) 2026 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sl_pll

#include <string.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>

#define FRAC_BITS		24
#define FRAC_MASK		((1 << FRAC_BITS) - 1)

#define CTRLA			0x0
#define  A_RESET		BIT(0)
#define  A_BYPASS		BIT(1)
#define  A_NEWDIV		BIT(2)
#define  A_RANGE_SHIFT		3
#define  A_RANGE_MASK		(0x7 << A_RANGE_SHIFT)

#define CTRLB			0x4
#define  B_SSE			BIT(9)

#define CTRLC			0x8
#define  C_DIVR_SHIFT		0
#define  C_DIVR_MASK		(0x1ff << C_DIVR_SHIFT)

#define CTRLD			0xc
#define  DIVF_SHIFT		0
#define  DIVF_MASK		(0x1ff << DIVF_SHIFT)

#define CTRLE			0x10
#define  DIVFF_SHIFT		0
#define  DIVFF_MASK		(((1 << FRAC_BITS) - 1) << DIVFF_SHIFT)

#define CTRLF			0x14
#define  DIVQ_SHIFT		0
#define  DIVQ_MASK		(0x1f << DIVQ_SHIFT)

#define CTRLG			0x18
#define  DIVQF_SHIFT		0
#define  DIVQF_MASK		(0x7 << DIVQF_SHIFT)

#define STATUS			0x1c
#define  PLL_LOCK		BIT(0)
#define  DIV_ACK		BIT(1)

#define FREQ_FACTOR		(1000)
#define DIVF_DEF_MULT		4

struct syna_sl_pll_cfg {
	uintptr_t base_phy;
	size_t base_size;
	uintptr_t bypass_phy;
	size_t bypass_size;
	uint8_t bypass_shift;
	uint32_t dm: 9;
	uint32_t dn: 9;
	uint32_t dp0: 5;
	uint32_t dp1: 3;
	uint32_t frac: 24;
	bool pd_bypass;
	const struct device *clock_dev;
};

struct syna_sl_pll_data {
	mm_reg_t base;
	mm_reg_t bypass;
};

static inline uint32_t rdl(struct syna_sl_pll_data *pll, uint32_t offset)
{
	return sys_read32(pll->base + offset);
}

static inline void wrl(struct syna_sl_pll_data *pll, uint32_t offset, uint32_t val)
{
	sys_write32(val, pll->base + offset);
}

static enum clock_control_status syna_sl_pll_get_status(const struct device *dev,
							clock_control_subsys_t sys)
{
	return CLOCK_CONTROL_STATUS_ON;
}

static int syna_sl_pll_get_rate(const struct device *dev,
				clock_control_subsys_t sys, uint32_t *rate)
{
	const struct syna_sl_pll_cfg *cfg = dev->config;
	struct syna_sl_pll_data *pll = dev->data;
	uint32_t val, dm, dn, frac, dp, parent_rate, id = (uintptr_t)sys;
	uint64_t result;
	int ret;

	ret = clock_control_get_rate(cfg->clock_dev, NULL, &parent_rate);
	if (ret) {
		return ret;
	}

	val = rdl(pll, CTRLC);
	dm = ((val & C_DIVR_MASK) >> C_DIVR_SHIFT) + 1;

	val = rdl(pll, CTRLD);
	dn = ((val & DIVF_MASK) >> DIVF_SHIFT) + 1;

	val = rdl(pll, CTRLE);
	frac = (val & DIVFF_MASK) >> DIVFF_SHIFT;

	if (id == 0) {
		val = rdl(pll, CTRLF);
		dp = (val & DIVQ_MASK) >> DIVQ_SHIFT;
		dp = (dp + 1) * 2;
	} else {
		val = rdl(pll, CTRLG);
		dp = ((val & DIVQF_MASK) >> DIVQF_SHIFT) + 1;
	}

	result = parent_rate * dn;
	result += (parent_rate * frac + (1 << (FRAC_BITS - 1))) / (1 << FRAC_BITS);
	result = (result * DIVF_DEF_MULT + dm * dp / 2) / dm / dp;

	*rate = result;

	return 0;
}

static uint32_t syna_sl_pll_get_pfdrange(uint32_t freq)
{
	uint32_t r;

	if (freq >= 50 && freq < 75) {
		r = 0;
	} else if (freq >= 75 && freq < 110) {
		r = 1;
	} else if (freq >= 110 && freq < 180) {
		r = 2;
	} else if (freq >= 180 && freq < 300) {
		r = 3;
	} else if (freq >= 300 && freq < 500) {
		r = 4;
	} else if (freq >= 500 && freq < 800) {
		r = 5;
	} else if (freq >= 800 && freq < 1300) {
		r = 6;
	} else if (freq >= 1300 && freq <= 2000) {
		r = 7;
	} else {
		r = 0;
	}

	return r;
}

static void syna_sl_pll_update_setting(const struct device *dev, uint32_t dm, uint32_t dn,
				       uint32_t frac, uint32_t dp0, uint32_t dp1)
{
	const struct syna_sl_pll_cfg *cfg = dev->config;
	struct syna_sl_pll_data *pll = dev->data;
	uint32_t freq, range, val, timeout, parent_rate;
	unsigned int key;
	int ret;

	ret = clock_control_get_rate(cfg->clock_dev, NULL, &parent_rate);
	if (ret) {
		return;
	}

	freq = parent_rate / (1000000 / FREQ_FACTOR) * 10 / (dm + 1);
	range = syna_sl_pll_get_pfdrange(freq);

	key = arch_irq_lock();

	val = sys_read32(pll->bypass);
	val |= 1 << cfg->bypass_shift;
	sys_write32(val, pll->bypass);

	/* set internal bypass and hold reset */
	val = rdl(pll, CTRLA);
	wrl(pll, CTRLA, val | A_BYPASS | A_RESET);

	/* disable ssc */
	val = rdl(pll, CTRLB);
	wrl(pll, CTRLB, val & (~B_SSE));

	/* set the vco */
	val = rdl(pll, CTRLC);
	val &= ~C_DIVR_MASK;
	val |= dm << C_DIVR_SHIFT;
	wrl(pll, CTRLC, val);

	val = rdl(pll, CTRLD);
	val &= ~DIVF_MASK;
	wrl(pll, CTRLD, val | (dn << DIVF_SHIFT));

	val = rdl(pll, CTRLE);
	val &= ~DIVFF_MASK;
	wrl(pll, CTRLE, val | (frac << DIVFF_SHIFT));

	/* set the range */
	val = rdl(pll, CTRLA);
	val &= ~A_RANGE_MASK;
	wrl(pll, CTRLA, val | (range << A_RANGE_SHIFT));

	/* set the divq */
	if (dp0 >= 0) {
		val = rdl(pll, CTRLF);
		val &= ~DIVQ_MASK;
		wrl(pll, CTRLF, val | (dp0 << DIVQ_SHIFT));
	}

	/* set the divq */
	if (dp1 >= 0) {
		val = rdl(pll, CTRLG);
		val &= ~DIVQF_MASK;
		wrl(pll, CTRLG, val | (dp1 << DIVQF_SHIFT));
	}

	/* release reset */
	val = rdl(pll, CTRLA);
	val &= ~A_RESET;
	wrl(pll, CTRLA, val);

	/* release bypass */
	val &= ~A_BYPASS;
	wrl(pll, CTRLA, val);

	if (cfg->pd_bypass) {
		val = sys_read32(pll->bypass);
		val &= ~(1 << cfg->bypass_shift);
		sys_write32(val, pll->bypass);
	}

	k_busy_wait(120);
	/*
	 * according to SPEC and diag team's feedback, the lock bit is not
	 * a must have, below is the part from SPEC.
	 * It is recommended that LOCK is only used for test and system status
	 * information, and is not used for critical system functions without
	 * thorough characterization in the host system.
	 * So, here we use timeout to print a warning when lock bit is not set.
	 */
	timeout = 100;
	val = rdl(pll, STATUS);
	while (!(val & PLL_LOCK)) {
		k_busy_wait(1);
		timeout--;
		if (!timeout) {
			/* pll lock timeout and continue */
			break;
		}
		val = rdl(pll, STATUS);
	}

	if (!cfg->pd_bypass) {
		val = sys_read32(pll->bypass);
		val &= ~(1 << cfg->bypass_shift);
		sys_write32(val, pll->bypass);
	}
	arch_irq_unlock(key);
}

static DEVICE_API(clock_control, clock_control_syna_sl_pll_api) = {
	.get_status = syna_sl_pll_get_status,
	.get_rate = syna_sl_pll_get_rate,
};

static int syna_sl_pll_init(const struct device *dev)
{
	const struct syna_sl_pll_cfg *cfg = dev->config;
	struct syna_sl_pll_data *data = dev->data;

	device_map(&data->base, cfg->base_phy, cfg->base_size, K_MEM_CACHE_NONE);
	device_map(&data->bypass, cfg->bypass_phy, cfg->bypass_size, K_MEM_CACHE_NONE);

	syna_sl_pll_update_setting(dev, cfg->dm - 1, cfg->dn - 1, cfg->frac, cfg->dp0 - 1, cfg->dp1 - 1);

	return 0;
}

#define SYNA_SL_PLL_CLK_INIT(idx)							\
	static struct syna_sl_pll_cfg pll_cfg##idx = {					\
		.clock_dev = DEVICE_DT_GET(DT_CLOCKS_CTLR(DT_DRV_INST(idx))),		\
		.base_phy = DT_REG_ADDR(DT_DRV_INST(idx)),				\
		.base_size = DT_REG_SIZE(DT_DRV_INST(idx)),				\
		.bypass_phy = DT_REG_ADDR_BY_IDX(DT_DRV_INST(idx), 1),			\
		.bypass_size = DT_REG_SIZE_BY_IDX(DT_DRV_INST(idx), 1),			\
		.bypass_shift = DT_PROP(DT_DRV_INST(idx), bypass_shift),		\
		.pd_bypass = DT_INST_PROP_OR(n, pd_bypass, 0),				\
		.dm = DT_INST_PROP_OR(idx, dm, 0),					\
		.dn = DT_INST_PROP_OR(idx, dn, 0),					\
		.dp0 = DT_INST_PROP_OR(idx, dp0, 0),					\
		.dp1 = DT_INST_PROP_OR(idx, dp1, 0),					\
		.frac = DT_INST_PROP_OR(idx, frac, 0),					\
	};										\
	static struct syna_sl_pll_data pll_data##idx;					\
	DEVICE_DT_INST_DEFINE(idx, syna_sl_pll_init, NULL, &pll_data##idx, &pll_cfg##idx, \
			      PRE_KERNEL_1, CONFIG_CLOCK_CONTROL_INIT_PRIORITY,		\
			      &clock_control_syna_sl_pll_api);

DT_INST_FOREACH_STATUS_OKAY(SYNA_SL_PLL_CLK_INIT);
