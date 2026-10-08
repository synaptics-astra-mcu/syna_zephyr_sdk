/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cache_benchmark.h"

#include <errno.h>
#include <cmsis_core.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/cache/arm_ahb_cache.h>
#include <zephyr/sys/util.h>

#define XSPI_NODE DT_NODELABEL(xspi)

#define CACHE_BENCHMARK_OFFSET	0x42000U
#define CACHE_BENCHMARK_SIZE	(4U * 1024U)
#define CACHE_BENCHMARK_RUNS	3U
#define CACHE_BENCHMARK_MAX_STRIDE 128U

#define XSPI_XIP_BASE DT_REG_ADDR_BY_NAME(XSPI_NODE, xspi_xip)
#define XSPI_XIP_SIZE DT_REG_SIZE_BY_NAME(XSPI_NODE, xspi_xip)

BUILD_ASSERT(CACHE_BENCHMARK_OFFSET + CACHE_BENCHMARK_SIZE <= XSPI_XIP_SIZE,
	     "Cache benchmark range exceeds the XSPI XIP window");

static int dwt_init(void)
{
	DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0U;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U ? 0 : -ENOTSUP;
}

static uint32_t test_seq_read(const volatile uint32_t *addr, size_t size, uint32_t *sum)
{
	volatile uint32_t result = 0U;
	unsigned int key;
	uint32_t start;
	uint32_t cycles;

	key = irq_lock();
	start = DWT->CYCCNT;
	for (size_t i = 0U; i < size / sizeof(*addr); i++) {
		result += addr[i];
	}
	cycles = DWT->CYCCNT - start;
	irq_unlock(key);

	*sum = result;
	return cycles;
}

static uint32_t test_random_read(const volatile uint32_t *addr, size_t size, uint32_t *sum)
{
	const uint32_t mask = (size / sizeof(*addr)) - 1U;
	volatile uint32_t result = 0U;
	uint32_t index = 0U;
	unsigned int key;
	uint32_t start;
	uint32_t cycles;

	key = irq_lock();
	start = DWT->CYCCNT;
	for (size_t i = 0U; i < size / sizeof(*addr); i++) {
		index = (index * 131U + 1U) & mask;
		result += addr[index];
	}
	cycles = DWT->CYCCNT - start;
	irq_unlock(key);

	*sum = result;
	return cycles;
}

static uint32_t test_stride_read(const volatile uint32_t *addr, size_t size,
				 uint32_t stride, uint32_t *sum)
{
	const uint32_t mask = (size / sizeof(*addr)) - 1U;
	volatile uint32_t result = 0U;
	unsigned int key;
	uint32_t start;
	uint32_t cycles;

	key = irq_lock();
	start = DWT->CYCCNT;
	for (uint32_t i = 0U; i < CACHE_BENCHMARK_MAX_STRIDE; i++) {
		result += addr[(i * stride) & mask];
	}
	cycles = DWT->CYCCNT - start;
	irq_unlock(key);

	*sum = result;
	return cycles;
}

static void report_run(const struct shell *shell, uint32_t run, uint32_t cycles,
		       uint32_t sum, uint32_t hit_before, uint32_t miss_before)
{
	uint32_t hits = arm_ahb_cache_hit_count_get() - hit_before;
	uint32_t misses = arm_ahb_cache_miss_count_get() - miss_before;

	shell_print(shell, "  Run %u: cycles=%u sum=0x%08x hits=%u misses=%u",
		    run, cycles, sum, hits, misses);
}

static int prepare_pattern(const volatile uint32_t *addr, size_t size)
{
	int ret;

	ret = sys_cache_data_flush_and_invd_range((void *)addr, size);
	if (ret != 0) {
		return ret;
	}

	ret = arm_ahb_cache_flush_and_invd_all();
	if (ret == 0) {
		arm_ahb_cache_stats_reset();
	}

	return ret;
}

static int run_cache_benchmark(const struct shell *shell, const char *name,
			       const volatile uint32_t *addr, size_t size)
{
	uint32_t hit_before;
	uint32_t miss_before;
	uint32_t cycles;
	uint32_t sum;
	int ret;

	if ((size < sizeof(*addr)) || !is_power_of_two(size / sizeof(*addr))) {
		return -EINVAL;
	}

	ret = dwt_init();
	if (ret != 0) {
		shell_error(shell, "DWT cycle counter is unavailable");
		return ret;
	}

	shell_print(shell, "");
	shell_print(shell, "==============================");
	shell_print(shell, "Benchmark: %s", name);
	shell_print(shell, "Address: 0x%08lx, size: %u bytes, cache: %u bytes",
		    (unsigned long)addr, (uint32_t)size, arm_ahb_cache_size_get());
	shell_print(shell, "==============================");

	ret = prepare_pattern(addr, size);
	if (ret != 0) {
		return ret;
	}
	shell_print(shell, "[Sequential Read]");
	for (uint32_t run = 1U; run <= CACHE_BENCHMARK_RUNS; run++) {
		hit_before = arm_ahb_cache_hit_count_get();
		miss_before = arm_ahb_cache_miss_count_get();
		cycles = test_seq_read(addr, size, &sum);
		report_run(shell, run, cycles, sum, hit_before, miss_before);
	}

	ret = prepare_pattern(addr, size);
	if (ret != 0) {
		return ret;
	}
	shell_print(shell, "[Random Read]");
	for (uint32_t run = 1U; run <= CACHE_BENCHMARK_RUNS; run++) {
		hit_before = arm_ahb_cache_hit_count_get();
		miss_before = arm_ahb_cache_miss_count_get();
		cycles = test_random_read(addr, size, &sum);
		report_run(shell, run, cycles, sum, hit_before, miss_before);
	}

	shell_print(shell, "[Stride Sweep]");
	for (uint32_t stride = 1U; stride <= CACHE_BENCHMARK_MAX_STRIDE; stride <<= 1) {
		ret = prepare_pattern(addr, size);
		if (ret != 0) {
			return ret;
		}

		shell_print(shell, "Stride %u", stride);
		for (uint32_t run = 1U; run <= CACHE_BENCHMARK_RUNS; run++) {
			hit_before = arm_ahb_cache_hit_count_get();
			miss_before = arm_ahb_cache_miss_count_get();
			cycles = test_stride_read(addr, size, stride, &sum);
			report_run(shell, run, cycles, sum, hit_before, miss_before);
		}
	}

	return 0;
}

int cache_benchmark_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *flash_dev = DEVICE_DT_GET(XSPI_NODE);
	const volatile uint32_t *addr =
		(const volatile uint32_t *)(XSPI_XIP_BASE + CACHE_BENCHMARK_OFFSET);
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(flash_dev)) {
		shell_error(shell, "XSPI flash device is not ready");
		return -ENODEV;
	}

	arm_ahb_cache_stats_enable();
	ret = run_cache_benchmark(shell, "XSPI", addr, CACHE_BENCHMARK_SIZE);
	arm_ahb_cache_stats_disable();

	return ret;
}
