/*
 * Copyright (C) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ARM-SSE-IP Timer driver
 */

#define DT_DRV_COMPAT arm_sse_timer

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/sys_clock.h>
#include <zephyr/sys/util.h>
#include <zephyr/irq.h>

static mem_addr_t timer_base;
static uint32_t clk_freq;
static struct k_spinlock lock;
static uint64_t last_cycle;
static uint64_t last_tick;
static uint32_t last_elapsed;

#define TIMER_CNTPCT_LOW	0x00
#define TIMER_CNTP_CVAL_LOW	0x20
#define TIMER_CNTP_CTL		0x2c
#define CNTP_CTL_ENABLE		BIT(0)
#define CNTP_CTL_IMASK		BIT(1)
#define TIMER_CNTP_AIVAL_CTL	0x4c

#define CYC_PER_TICK		(uint32_t)(clk_freq / CONFIG_SYS_CLOCK_TICKS_PER_SEC)

static uint64_t sse_get_cntpct(void)
{
	return sys_read64(timer_base + TIMER_CNTPCT_LOW);
}

static int arm_sse_timer_set_compare(uint64_t evt)
{
	uint32_t val;

	val = sys_read32(timer_base + TIMER_CNTP_CTL);
	val &= ~CNTP_CTL_ENABLE;
	sys_write32(val, timer_base + TIMER_CNTP_CTL);

	sys_write64(evt, timer_base + TIMER_CNTP_CVAL_LOW);
	sys_write32(val | CNTP_CTL_ENABLE, timer_base + TIMER_CNTP_CTL);

	return 0;
}

static void arm_sse_timer_set_irq_mask(bool mask)
{
	uint32_t val;

	val = sys_read32(timer_base + TIMER_CNTP_CTL);
	val &= ~CNTP_CTL_IMASK;
	if (mask)
		val |= CNTP_CTL_IMASK;
	sys_write32(val, timer_base + TIMER_CNTP_CTL);
}

static void timer_arm_sse_isr(const void *arg)
{
	uint64_t delta_cycles;
	uint32_t delta_ticks;

	ARG_UNUSED(arg);

	k_spinlock_key_t key = k_spin_lock(&lock);

	delta_cycles = sse_get_cntpct() - last_cycle;
	delta_ticks = (uint32_t)delta_cycles / CYC_PER_TICK;

	last_cycle += delta_ticks * CYC_PER_TICK;
	last_tick += delta_ticks;
	last_elapsed = 0;

	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		uint64_t next_cycle = last_cycle + CYC_PER_TICK;

		arm_sse_timer_set_compare(next_cycle);
		arm_sse_timer_set_irq_mask(false);
	} else {
		arm_sse_timer_set_irq_mask(true);
	}

	k_spin_unlock(&lock, key);

	sys_clock_announce(delta_ticks);
}

void sys_clock_set_timeout(int32_t ticks, bool idle)
{
	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return;
	}

	if (idle && ticks == K_TICKS_FOREVER) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);

	if (ticks == K_TICKS_FOREVER) {
		arm_sse_timer_set_irq_mask(true);
	} else {
		uint64_t next_cycle;

		ticks = MAX(ticks, 1);
		next_cycle = last_cycle + ((uint64_t)(last_elapsed + ticks) * CYC_PER_TICK);

		arm_sse_timer_set_compare(next_cycle);
		arm_sse_timer_set_irq_mask(false);
	}

	k_spin_unlock(&lock, key);
}

uint32_t sys_clock_elapsed(void)
{
	uint64_t delta_cycles;
	uint32_t delta_ticks;

	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return 0;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);

	delta_cycles = sse_get_cntpct() - last_cycle;
	delta_ticks = (uint32_t)delta_cycles / CYC_PER_TICK;
	last_elapsed = delta_ticks;

	k_spin_unlock(&lock, key);

	return delta_ticks;
}

uint32_t sys_clock_cycle_get_32(void)
{
	return (uint32_t)sse_get_cntpct();
}

uint64_t sys_clock_cycle_get_64(void)
{
	return sse_get_cntpct();
}

void sys_clock_disable(void)
{
	sys_write32(CNTP_CTL_IMASK, timer_base + TIMER_CNTP_CTL);
}

static int timer_arm_sse_init(void)
{
	uint64_t now = sse_get_cntpct();

	last_tick = now / CYC_PER_TICK;
	last_cycle = (uint64_t)last_tick * CYC_PER_TICK;
	last_elapsed = 0;

	timer_base = DT_INST_REG_ADDR(0);
	clk_freq = DT_INST_PROP(0, clock_frequency);

	sys_write32(0, timer_base + TIMER_CNTP_AIVAL_CTL);
	sys_write32(CNTP_CTL_ENABLE | CNTP_CTL_IMASK, timer_base + TIMER_CNTP_CTL);

	/* Setup IRQ */
	IRQ_CONNECT(DT_INST_IRQN(0), 0, timer_arm_sse_isr, NULL, 0);
	irq_enable(DT_INST_IRQN(0));

	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		uint64_t next_cycle = last_cycle + CYC_PER_TICK;

		arm_sse_timer_set_compare(next_cycle);
		arm_sse_timer_set_irq_mask(false);
	}

	return 0;
}

SYS_INIT(timer_arm_sse_init, PRE_KERNEL_2, CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
