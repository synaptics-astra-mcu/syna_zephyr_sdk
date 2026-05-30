/*
 * Copyright (c) 2026 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/dt-bindings/clock/syna_sl261x_a55_clock.h>

extern int syna_sl_clk_configure(const struct device *dev, uint32_t id, uint32_t pllsel, uint32_t div);

void soc_clk_init(const struct device *dev)
{
	mm_reg_t base_addr = 0xf7e104e0;
	device_map(&base_addr, base_addr, 32, K_MEM_CACHE_NONE);
	sys_write32(0x28, base_addr);
	sys_write32(0x14, base_addr + 4);
	sys_write32(0x28, base_addr + 8);
	sys_write32(0x14, base_addr + 0xc);
#ifdef CONFIG_CLOCK_CONTROL_SYNA_SL_CLK
	syna_sl_clk_configure(dev, CLK_CPUFASTREF, 5, 1);
	syna_sl_clk_configure(dev, CLK_MEMFASTREF, 5, 2);
	syna_sl_clk_configure(dev, CLK_CFG, 2, 6);
	syna_sl_clk_configure(dev, CLK_SYS, 2, 2);
	syna_sl_clk_configure(dev, CLK_PERIFSYS, 2, 3);
	syna_sl_clk_configure(dev, CLK_APBCORE, 2, 6);
	syna_sl_clk_configure(dev, CLK_APBSER, 2, 6);
	syna_sl_clk_configure(dev, CLK_ATB, 2, 6);
	syna_sl_clk_configure(dev, CLK_HPC, 5, 2);
	syna_sl_clk_configure(dev, CLK_EMMC, 2, 3);
	syna_sl_clk_configure(dev, CLK_SD0, 2, 3);
	syna_sl_clk_configure(dev, CLK_SD1, 2, 3);
	syna_sl_clk_configure(dev, CLK_GETHRGMII, 4, 4);
	syna_sl_clk_configure(dev, CLK_GETHRGMII1, 4, 4);
	syna_sl_clk_configure(dev, CLK_GE0_PTP_REF, 4, 4);
	syna_sl_clk_configure(dev, CLK_GE1_PTP_REF, 4, 4);
	syna_sl_clk_configure(dev, CLK_GPU, 0, 1);
	syna_sl_clk_configure(dev, CLK_NPU, 5, 1);
#endif
}

int __weak pm_cpu_on(unsigned long cpuid, uintptr_t entry_point)
{
	return 0;
}
