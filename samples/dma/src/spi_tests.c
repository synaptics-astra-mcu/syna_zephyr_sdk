/*
 * SPI Test Suite: Comprehensive tests for various SPI transfer combinations
 * Covers: Master/Slave, TX-only, RX-only, TX+RX transfers
 * Modes: Synchronous and asynchronous operations
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/cache.h>

/* ============================================================================
 * Configuration & Global State
 * ============================================================================ */

/* Test buffer sizes */
#define TEST_BUFFER_SIZE 256
#define TEST_TRANSFER_SIZE 8

/* Test result tracking */
struct spi_test_result {
	const char *test_name;
	int result;
	uint8_t expected[TEST_BUFFER_SIZE];
	uint8_t actual[TEST_BUFFER_SIZE];
	int data_size;
	int passed;
};

struct async_ctx {
	volatile int done;
	int result;
};

static const struct device *g_spi_master_dev;
static const struct device *g_spi_slave_dev;
static struct spi_config *g_config_master;
static struct spi_config *g_config_slave;

static uint8_t __aligned(32) tx_buf[32];
static uint8_t __aligned(32) rx_buf[32];
static uint8_t __aligned(32) slave_tx_buf[32];
static uint8_t __aligned(32) slave_rx_buf[32];
static uint8_t __aligned(32) master_tx_buf[32];
static uint8_t __aligned(32) master_rx_buf[32];

/* ============================================================================
 * Callback Handlers
 * ============================================================================ */

/**
 * Generic asynchronous SPI callback for test synchronization
 */
static void spi_async_callback(const struct device *dev, int result, void *data)
{
	struct async_ctx *ctx = (struct async_ctx *)data;

	ARG_UNUSED(dev);

	ctx->result = result;
	ctx->done = 1;

	if (result < 0) {
		printf("[ASYNC] SPI transfer failed with code: %d\n", result);
	}
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Initialize buffers with test pattern
 */
static void init_tx_buffer(uint8_t *buffer, int size, uint8_t start_value)
{
	for (int i = 0; i < size; i++) {
		buffer[i] = (start_value + i) & 0xFF;
	}
}

/**
 * Clear RX buffer
 */
static void clear_rx_buffer(uint8_t *buffer, int size)
{
	memset(buffer, 0, size);
}

/**
 * Compare two buffers and report differences
 */
static int compare_buffers(const uint8_t *expected, const uint8_t *actual,
			   int size, const char *test_name)
{
	int differences = 0;

	for (int i = 0; i < size; i++) {
		if (expected[i] != actual[i]) {
			if (differences < 3) { /* Print first 3 differences */
				printf("  [%s] Mismatch at byte %d: expected 0x%02x, got 0x%02x\n",
				       test_name, i, expected[i], actual[i]);
			}
			differences++;
		}
	}

	if (differences > 3) {
		printf("  [%s] ... and %d more mismatches\n", test_name, differences - 3);
	}

	return differences == 0;
}

/**
 * Print test result summary
 */
static void print_test_result(struct spi_test_result *result)
{
	if (result->result >= 0 && result->passed) {
		printf("✓ PASS: %s\n", result->test_name);
	} else {
		printf("✗ FAIL: %s (SPI result: %d, Data match: %s)\n",
		       result->test_name, result->result, result->passed ? "yes" : "no");
	}
}

/**
 * Wait for asynchronous operation with timeout
 */
static int wait_async_complete(struct async_ctx *ctx, int timeout_ms)
{
	int elapsed = 0;
	int poll_interval = 10; /* ms */

	while (!ctx->done && elapsed < timeout_ms) {
		k_sleep(K_MSEC(poll_interval));
		elapsed += poll_interval;
	}

	if (!ctx->done) {
		printf("[ASYNC] Timeout waiting for callback\n");
		return -1;
	}

	return ctx->result;
}

static void async_ctx_reset(struct async_ctx *ctx)
{
	ctx->done = 0;
	ctx->result = -1;
}

/* ============================================================================
 * Master Synchronous Tests
 * ============================================================================ */

/**
 * Test: Master TX only (synchronous)
 * Master sends data, no receive
 */
static int test_master_tx_only(const struct device *spi_master,
			       struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Master TX only (sync)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx slave_ctx;

	struct spi_buf tx_buf_struct[1] = {
		{.buf = tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf slave_rx_buf_struct[1] = {
		{.buf = slave_rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set tx_set = {.buffers = tx_buf_struct, .count = 1};
	struct spi_buf_set slave_rx_set = {.buffers = slave_rx_buf_struct, .count = 1};

	init_tx_buffer(tx_buf, TEST_TRANSFER_SIZE, 0xA0);
	clear_rx_buffer(slave_rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, tx_buf, TEST_TRANSFER_SIZE);
	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(g_spi_slave_dev, g_config_slave, NULL,
					  &slave_rx_set, spi_async_callback,
					  &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = spi_transceive(spi_master, config, &tx_set, NULL);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);
	else printf("failed\n");

	memcpy(result.actual, slave_rx_buf, TEST_TRANSFER_SIZE);
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name);

	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/**
 * Test: Master RX only (synchronous)
 * Master receives data, no transmit
 */
static int test_master_rx_only(const struct device *spi_master,
			       struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Master RX only (sync)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx slave_ctx;

	struct spi_buf slave_tx_buf_struct[1] = {
		{.buf = slave_tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf rx_buf_struct[1] = {
		{.buf = rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set slave_tx_set = {.buffers = slave_tx_buf_struct, .count = 1};
	struct spi_buf_set rx_set = {.buffers = rx_buf_struct, .count = 1};

	init_tx_buffer(slave_tx_buf, TEST_TRANSFER_SIZE, 0xB8);
	clear_rx_buffer(rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, slave_tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(g_spi_slave_dev, g_config_slave,
					  &slave_tx_set, NULL,
					  spi_async_callback, &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = spi_transceive(spi_master, config, NULL, &rx_set);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, rx_buf, TEST_TRANSFER_SIZE);
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name);

	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/**
 * Test: Master TX + RX (synchronous)
 * Full duplex transfer
 */
static int test_master_tx_rx(const struct device *spi_master,
			     struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Master TX + RX (sync)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx slave_ctx;

	struct spi_buf tx_buf_struct[1] = {
		{.buf = tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf rx_buf_struct[1] = {
		{.buf = rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf slave_tx_buf_struct[1] = {
		{.buf = slave_tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf slave_rx_buf_struct[1] = {
		{.buf = slave_rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set tx_set = {.buffers = tx_buf_struct, .count = 1};
	struct spi_buf_set rx_set = {.buffers = rx_buf_struct, .count = 1};
	struct spi_buf_set slave_tx_set = {.buffers = slave_tx_buf_struct, .count = 1};
	struct spi_buf_set slave_rx_set = {.buffers = slave_rx_buf_struct, .count = 1};
	int slave_data_ok;

	init_tx_buffer(tx_buf, TEST_TRANSFER_SIZE, 0xB0);
	init_tx_buffer(slave_tx_buf, TEST_TRANSFER_SIZE, 0xC8);
	clear_rx_buffer(rx_buf, TEST_TRANSFER_SIZE);
	clear_rx_buffer(slave_rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, slave_tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(g_spi_slave_dev, g_config_slave,
					  &slave_tx_set, &slave_rx_set,
					  spi_async_callback, &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = spi_transceive(spi_master, config, &tx_set, &rx_set);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, rx_buf, TEST_TRANSFER_SIZE);
	slave_data_ok = compare_buffers(tx_buf, slave_rx_buf,
					TEST_TRANSFER_SIZE,
					"Master TX + RX (sync) slave-rx");
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name) &&
			slave_data_ok;

	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/* ============================================================================
 * Slave Synchronous Tests
 * ============================================================================ */

/**
 * Test: Slave TX only (synchronous)
 * Slave sends data, no receive
 */
static int test_slave_tx_only(const struct device *spi_slave,
			      struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Slave TX only (sync)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx slave_ctx;

	struct spi_buf tx_buf_struct[1] = {
		{.buf = tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf master_rx_buf_struct[1] = {
		{.buf = master_rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set tx_set = {.buffers = tx_buf_struct, .count = 1};
	struct spi_buf_set master_rx_set = {.buffers = master_rx_buf_struct, .count = 1};

	init_tx_buffer(tx_buf, TEST_TRANSFER_SIZE, 0xC0);
	clear_rx_buffer(master_rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(spi_slave, config, &tx_set, NULL,
					  spi_async_callback, &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = spi_transceive(g_spi_master_dev, g_config_master,
				       NULL, &master_rx_set);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, master_rx_buf, TEST_TRANSFER_SIZE);
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name);

	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/**
 * Test: Slave RX only (synchronous)
 * Slave receives data, no transmit
 */
static int test_slave_rx_only(const struct device *spi_slave,
			      struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Slave RX only (sync)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx slave_ctx;

	struct spi_buf master_tx_buf_struct[1] = {
		{.buf = master_tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf rx_buf_struct[1] = {
		{.buf = rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set master_tx_set = {.buffers = master_tx_buf_struct, .count = 1};
	struct spi_buf_set rx_set = {.buffers = rx_buf_struct, .count = 1};

	init_tx_buffer(master_tx_buf, TEST_TRANSFER_SIZE, 0xD8);
	clear_rx_buffer(rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, master_tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(spi_slave, config, NULL, &rx_set,
					  spi_async_callback, &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = spi_transceive(g_spi_master_dev, g_config_master,
				       &master_tx_set, NULL);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, rx_buf, TEST_TRANSFER_SIZE);
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name);

	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/**
 * Test: Slave TX + RX (synchronous)
 * Full duplex slave transfer
 */
static int test_slave_tx_rx(const struct device *spi_slave,
			    struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Slave TX + RX (sync)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx slave_ctx;

	struct spi_buf tx_buf_struct[1] = {
		{.buf = tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf rx_buf_struct[1] = {
		{.buf = rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf master_tx_buf_struct[1] = {
		{.buf = master_tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf master_rx_buf_struct[1] = {
		{.buf = master_rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set tx_set = {.buffers = tx_buf_struct, .count = 1};
	struct spi_buf_set rx_set = {.buffers = rx_buf_struct, .count = 1};
	struct spi_buf_set master_tx_set = {.buffers = master_tx_buf_struct, .count = 1};
	struct spi_buf_set master_rx_set = {.buffers = master_rx_buf_struct, .count = 1};
	int master_data_ok;

	init_tx_buffer(tx_buf, TEST_TRANSFER_SIZE, 0xD0);
	init_tx_buffer(master_tx_buf, TEST_TRANSFER_SIZE, 0xE8);
	clear_rx_buffer(rx_buf, TEST_TRANSFER_SIZE);
	clear_rx_buffer(master_rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, master_tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(spi_slave, config, &tx_set, &rx_set,
					  spi_async_callback, &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = spi_transceive(g_spi_master_dev, g_config_master,
				       &master_tx_set, &master_rx_set);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, rx_buf, TEST_TRANSFER_SIZE);
	master_data_ok = compare_buffers(tx_buf, master_rx_buf,
					 TEST_TRANSFER_SIZE,
					 "Slave TX + RX (sync) master-rx");
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name) &&
			master_data_ok;

	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/* ============================================================================
 * Master Asynchronous Tests
 * ============================================================================ */

/**
 * Test: Master TX (asynchronous with callback)
 */
static int test_master_tx_async(const struct device *spi_master,
				struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Master TX (async)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx master_ctx;
	struct async_ctx slave_ctx;

	struct spi_buf tx_buf_struct[1] = {
		{.buf = tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf slave_rx_buf_struct[1] = {
		{.buf = slave_rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set tx_set = {.buffers = tx_buf_struct, .count = 1};
	struct spi_buf_set slave_rx_set = {.buffers = slave_rx_buf_struct, .count = 1};

	init_tx_buffer(tx_buf, TEST_TRANSFER_SIZE, 0xE0);
	clear_rx_buffer(slave_rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(g_spi_slave_dev, g_config_slave, NULL,
					  &slave_rx_set, spi_async_callback,
					  &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	async_ctx_reset(&master_ctx);
	result.result = spi_transceive_cb(spi_master, config, &tx_set, NULL,
					  spi_async_callback, &master_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = wait_async_complete(&master_ctx, 1000);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, slave_rx_buf, TEST_TRANSFER_SIZE);
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name);
	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/**
 * Test: Master RX (asynchronous with callback)
 */
static int test_master_rx_async(const struct device *spi_master,
				struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Master RX (async)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx master_ctx;
	struct async_ctx slave_ctx;

	struct spi_buf slave_tx_buf_struct[1] = {
		{.buf = slave_tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf rx_buf_struct[1] = {
		{.buf = rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set slave_tx_set = {.buffers = slave_tx_buf_struct, .count = 1};
	struct spi_buf_set rx_set = {.buffers = rx_buf_struct, .count = 1};

	init_tx_buffer(slave_tx_buf, TEST_TRANSFER_SIZE, 0xF8);
	clear_rx_buffer(rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, slave_tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(g_spi_slave_dev, g_config_slave,
					  &slave_tx_set, NULL,
					  spi_async_callback, &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	async_ctx_reset(&master_ctx);
	result.result = spi_transceive_cb(spi_master, config, NULL, &rx_set,
					  spi_async_callback, &master_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = wait_async_complete(&master_ctx, 1000);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, rx_buf, TEST_TRANSFER_SIZE);
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name);
	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/**
 * Test: Master TX + RX (asynchronous with callback)
 */
static int test_master_tx_rx_async(const struct device *spi_master,
				   struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Master TX + RX (async)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx master_ctx;
	struct async_ctx slave_ctx;

	struct spi_buf tx_buf_struct[1] = {
		{.buf = tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf rx_buf_struct[1] = {
		{.buf = rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf slave_tx_buf_struct[1] = {
		{.buf = slave_tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf slave_rx_buf_struct[1] = {
		{.buf = slave_rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set tx_set = {.buffers = tx_buf_struct, .count = 1};
	struct spi_buf_set rx_set = {.buffers = rx_buf_struct, .count = 1};
	struct spi_buf_set slave_tx_set = {.buffers = slave_tx_buf_struct, .count = 1};
	struct spi_buf_set slave_rx_set = {.buffers = slave_rx_buf_struct, .count = 1};
	int slave_data_ok;

	init_tx_buffer(tx_buf, TEST_TRANSFER_SIZE, 0xF0);
	init_tx_buffer(slave_tx_buf, TEST_TRANSFER_SIZE, 0x28);
	clear_rx_buffer(rx_buf, TEST_TRANSFER_SIZE);
	clear_rx_buffer(slave_rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, slave_tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(g_spi_slave_dev, g_config_slave,
					  &slave_tx_set, &slave_rx_set,
					  spi_async_callback, &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	async_ctx_reset(&master_ctx);
	result.result = spi_transceive_cb(spi_master, config, &tx_set, &rx_set,
					  spi_async_callback, &master_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = wait_async_complete(&master_ctx, 1000);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, rx_buf, TEST_TRANSFER_SIZE);
	slave_data_ok = compare_buffers(tx_buf, slave_rx_buf,
					TEST_TRANSFER_SIZE,
					"Master TX + RX (async) slave-rx");
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name) &&
			slave_data_ok;
	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/* ============================================================================
 * Slave Asynchronous Tests
 * ============================================================================ */

/**
 * Test: Slave TX (asynchronous with callback)
 */
static int test_slave_tx_async(const struct device *spi_slave,
			       struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Slave TX (async)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx master_ctx;
	struct async_ctx slave_ctx;

	struct spi_buf tx_buf_struct[1] = {
		{.buf = tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf master_rx_buf_struct[1] = {
		{.buf = master_rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set tx_set = {.buffers = tx_buf_struct, .count = 1};
	struct spi_buf_set master_rx_set = {.buffers = master_rx_buf_struct, .count = 1};

	init_tx_buffer(tx_buf, TEST_TRANSFER_SIZE, 0x10);
	clear_rx_buffer(master_rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(spi_slave, config, &tx_set, NULL,
					  spi_async_callback, &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	async_ctx_reset(&master_ctx);
	result.result = spi_transceive_cb(g_spi_master_dev, g_config_master,
					  NULL, &master_rx_set,
					  spi_async_callback, &master_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = wait_async_complete(&master_ctx, 1000);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, master_rx_buf, TEST_TRANSFER_SIZE);
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name);
	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/**
 * Test: Slave RX (asynchronous with callback)
 */
static int test_slave_rx_async(const struct device *spi_slave,
			       struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Slave RX (async)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx master_ctx;
	struct async_ctx slave_ctx;

	struct spi_buf master_tx_buf_struct[1] = {
		{.buf = master_tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf rx_buf_struct[1] = {
		{.buf = rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set master_tx_set = {.buffers = master_tx_buf_struct, .count = 1};
	struct spi_buf_set rx_set = {.buffers = rx_buf_struct, .count = 1};

	init_tx_buffer(master_tx_buf, TEST_TRANSFER_SIZE, 0x18);
	clear_rx_buffer(rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, master_tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(spi_slave, config, NULL, &rx_set,
					  spi_async_callback, &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	async_ctx_reset(&master_ctx);
	result.result = spi_transceive_cb(g_spi_master_dev, g_config_master,
					  &master_tx_set, NULL,
					  spi_async_callback, &master_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = wait_async_complete(&master_ctx, 1000);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, rx_buf, TEST_TRANSFER_SIZE);
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name);
	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/**
 * Test: Slave TX + RX (asynchronous with callback)
 */
static int test_slave_tx_rx_async(const struct device *spi_slave,
				  struct spi_config *config)
{
	struct spi_test_result result = {
		.test_name = "Slave TX + RX (async)",
		.data_size = TEST_TRANSFER_SIZE,
	};
	struct async_ctx master_ctx;
	struct async_ctx slave_ctx;

	struct spi_buf tx_buf_struct[1] = {
		{.buf = tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf rx_buf_struct[1] = {
		{.buf = rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf master_tx_buf_struct[1] = {
		{.buf = master_tx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf master_rx_buf_struct[1] = {
		{.buf = master_rx_buf, .len = TEST_TRANSFER_SIZE},
	};
	struct spi_buf_set tx_set = {.buffers = tx_buf_struct, .count = 1};
	struct spi_buf_set rx_set = {.buffers = rx_buf_struct, .count = 1};
	struct spi_buf_set master_tx_set = {.buffers = master_tx_buf_struct, .count = 1};
	struct spi_buf_set master_rx_set = {.buffers = master_rx_buf_struct, .count = 1};
	int master_data_ok;

	init_tx_buffer(tx_buf, TEST_TRANSFER_SIZE, 0x20);
	init_tx_buffer(master_tx_buf, TEST_TRANSFER_SIZE, 0x30);
	clear_rx_buffer(rx_buf, TEST_TRANSFER_SIZE);
	clear_rx_buffer(master_rx_buf, TEST_TRANSFER_SIZE);
	memcpy(result.expected, master_tx_buf, TEST_TRANSFER_SIZE);

	async_ctx_reset(&slave_ctx);
	result.result = spi_transceive_cb(spi_slave, config, &tx_set, &rx_set,
					  spi_async_callback, &slave_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	async_ctx_reset(&master_ctx);
	result.result = spi_transceive_cb(g_spi_master_dev, g_config_master,
					  &master_tx_set, &master_rx_set,
					  spi_async_callback, &master_ctx);
	if (result.result < 0) {
		result.passed = 0;
		print_test_result(&result);
		return -1;
	}

	result.result = wait_async_complete(&master_ctx, 1000);
	if (result.result >= 0)
		result.result = wait_async_complete(&slave_ctx, 1000);

	memcpy(result.actual, rx_buf, TEST_TRANSFER_SIZE);
	master_data_ok = compare_buffers(tx_buf, master_rx_buf,
					 TEST_TRANSFER_SIZE,
					 "Slave TX + RX (async) master-rx");
	result.passed = (result.result >= 0) &&
			compare_buffers(result.expected, result.actual,
					result.data_size, result.test_name) &&
			master_data_ok;
	print_test_result(&result);
	return result.passed ? 0 : -1;
}

/* ============================================================================
 * Multi-Transfer Tests
 * ============================================================================ */

/**
 * Test: Sequential master transfers (multiple in a row)
 */
static int test_master_sequential(const struct device *spi_master,
				  struct spi_config *config)
{
	int pass_count = 0;
	int fail_count = 0;
	struct async_ctx slave_ctx;

	printf("\n--- Sequential Master Transfers ---\n");

	for (int i = 0; i < 3; i++) {
		init_tx_buffer(tx_buf, TEST_TRANSFER_SIZE, 0x30 + i);
		init_tx_buffer(slave_tx_buf, TEST_TRANSFER_SIZE, 0x60 + i);
		clear_rx_buffer(rx_buf, TEST_TRANSFER_SIZE);
		clear_rx_buffer(slave_rx_buf, TEST_TRANSFER_SIZE);

		struct spi_buf tx_bufs[1] = {{.buf = tx_buf, .len = TEST_TRANSFER_SIZE}};
		struct spi_buf rx_bufs[1] = {{.buf = rx_buf, .len = TEST_TRANSFER_SIZE}};
		struct spi_buf slave_tx_bufs[1] = {{.buf = slave_tx_buf, .len = TEST_TRANSFER_SIZE}};
		struct spi_buf slave_rx_bufs[1] = {{.buf = slave_rx_buf, .len = TEST_TRANSFER_SIZE}};
		struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 1};
		struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = 1};
		struct spi_buf_set slave_tx_set = {.buffers = slave_tx_bufs, .count = 1};
		struct spi_buf_set slave_rx_set = {.buffers = slave_rx_bufs, .count = 1};

		async_ctx_reset(&slave_ctx);
		int ret = spi_transceive_cb(g_spi_slave_dev, g_config_slave,
					    &slave_tx_set, &slave_rx_set,
					    spi_async_callback, &slave_ctx);
		if (ret >= 0)
			ret = spi_transceive(spi_master, config, &tx_set, &rx_set);
		if (ret >= 0)
			ret = wait_async_complete(&slave_ctx, 1000);

		if (ret >= 0 &&
		    !compare_buffers(slave_tx_buf, rx_buf, TEST_TRANSFER_SIZE,
				     "Sequential master-rx"))
			ret = -1;
		if (ret >= 0 &&
		    !compare_buffers(tx_buf, slave_rx_buf, TEST_TRANSFER_SIZE,
				     "Sequential slave-rx"))
			ret = -1;

		printf("  Transfer %d: %s\n", i + 1, ret >= 0 ? "OK" : "FAIL");

		if (ret >= 0) {
			pass_count++;
		} else {
			fail_count++;
		}

		k_sleep(K_MSEC(10)); /* Small delay between transfers */
	}

	printf("Sequential test: %d passed, %d failed\n", pass_count, fail_count);
	return fail_count == 0 ? 0 : -1;
}

/**
 * Test: Interleaved async transfers (overlap execution)
 */
static int test_master_interleaved_async(const struct device *spi_master,
					 struct spi_config *config)
{
	struct async_ctx master_ctx;
	struct async_ctx slave_ctx;
	int fail_count = 0;

	printf("\n--- Interleaved Async Transfers ---\n");

	uint8_t __aligned(32) tx_buf1[TEST_TRANSFER_SIZE];
	uint8_t __aligned(32) rx_buf1[TEST_TRANSFER_SIZE];
	uint8_t __aligned(32) slave_tx_buf1[TEST_TRANSFER_SIZE];
	uint8_t __aligned(32) slave_rx_buf1[TEST_TRANSFER_SIZE];
	uint8_t __aligned(32) tx_buf2[TEST_TRANSFER_SIZE];
	uint8_t __aligned(32) rx_buf2[TEST_TRANSFER_SIZE];
	uint8_t __aligned(32) slave_tx_buf2[TEST_TRANSFER_SIZE];
	uint8_t __aligned(32) slave_rx_buf2[TEST_TRANSFER_SIZE];

	init_tx_buffer(tx_buf1, TEST_TRANSFER_SIZE, 0x40);
	init_tx_buffer(tx_buf2, TEST_TRANSFER_SIZE, 0x50);
	init_tx_buffer(slave_tx_buf1, TEST_TRANSFER_SIZE, 0x70);
	init_tx_buffer(slave_tx_buf2, TEST_TRANSFER_SIZE, 0x80);
	clear_rx_buffer(rx_buf1, TEST_TRANSFER_SIZE);
	clear_rx_buffer(rx_buf2, TEST_TRANSFER_SIZE);
	clear_rx_buffer(slave_rx_buf1, TEST_TRANSFER_SIZE);
	clear_rx_buffer(slave_rx_buf2, TEST_TRANSFER_SIZE);

	struct spi_buf tx_bufs1[1] = {{.buf = tx_buf1, .len = TEST_TRANSFER_SIZE}};
	struct spi_buf rx_bufs1[1] = {{.buf = rx_buf1, .len = TEST_TRANSFER_SIZE}};
	struct spi_buf slave_tx_bufs1[1] = {{.buf = slave_tx_buf1, .len = TEST_TRANSFER_SIZE}};
	struct spi_buf slave_rx_bufs1[1] = {{.buf = slave_rx_buf1, .len = TEST_TRANSFER_SIZE}};
	struct spi_buf tx_bufs2[1] = {{.buf = tx_buf2, .len = TEST_TRANSFER_SIZE}};
	struct spi_buf rx_bufs2[1] = {{.buf = rx_buf2, .len = TEST_TRANSFER_SIZE}};
	struct spi_buf slave_tx_bufs2[1] = {{.buf = slave_tx_buf2, .len = TEST_TRANSFER_SIZE}};
	struct spi_buf slave_rx_bufs2[1] = {{.buf = slave_rx_buf2, .len = TEST_TRANSFER_SIZE}};

	struct spi_buf_set tx_set1 = {.buffers = tx_bufs1, .count = 1};
	struct spi_buf_set rx_set1 = {.buffers = rx_bufs1, .count = 1};
	struct spi_buf_set slave_tx_set1 = {.buffers = slave_tx_bufs1, .count = 1};
	struct spi_buf_set slave_rx_set1 = {.buffers = slave_rx_bufs1, .count = 1};
	struct spi_buf_set tx_set2 = {.buffers = tx_bufs2, .count = 1};
	struct spi_buf_set rx_set2 = {.buffers = rx_bufs2, .count = 1};
	struct spi_buf_set slave_tx_set2 = {.buffers = slave_tx_bufs2, .count = 1};
	struct spi_buf_set slave_rx_set2 = {.buffers = slave_rx_bufs2, .count = 1};

	async_ctx_reset(&slave_ctx);
	int ret1 = spi_transceive_cb(g_spi_slave_dev, g_config_slave,
				     &slave_tx_set1, &slave_rx_set1,
				     spi_async_callback, &slave_ctx);
	async_ctx_reset(&master_ctx);
	if (ret1 >= 0)
		ret1 = spi_transceive_cb(spi_master, config, &tx_set1, &rx_set1,
					 spi_async_callback, &master_ctx);
	if (ret1 >= 0)
		ret1 = wait_async_complete(&master_ctx, 1000);
	if (ret1 >= 0)
		ret1 = wait_async_complete(&slave_ctx, 1000);
	printf("  Transfer 1 queued: %s\n", ret1 >= 0 ? "OK" : "FAIL");
	if (ret1 < 0)
		fail_count++;

	async_ctx_reset(&slave_ctx);
	int ret2 = spi_transceive_cb(g_spi_slave_dev, g_config_slave,
				     &slave_tx_set2, &slave_rx_set2,
				     spi_async_callback, &slave_ctx);
	async_ctx_reset(&master_ctx);
	if (ret2 >= 0)
		ret2 = spi_transceive_cb(spi_master, config, &tx_set2, &rx_set2,
					 spi_async_callback, &master_ctx);
	if (ret2 >= 0)
		ret2 = wait_async_complete(&master_ctx, 1000);
	if (ret2 >= 0)
		ret2 = wait_async_complete(&slave_ctx, 1000);
	printf("  Transfer 2 queued: %s\n", ret2 >= 0 ? "OK" : "FAIL");
	if (ret2 < 0)
		fail_count++;

	if (fail_count == 0) {
		printf("  Both transfers completed\n");
		return 0;
	}

	return -1;
}

/* ============================================================================
 * Different Data Size Tests
 * ============================================================================ */

/**
 * Test: Various data sizes (1, 2, 4, 8, 16, 32 bytes)
 */
static int test_various_sizes(const struct device *spi_master,
			      struct spi_config *config)
{
	struct async_ctx slave_ctx;
	printf("\n--- Various Transfer Sizes ---\n");

	uint8_t sizes[] = {1, 2, 4, 8, 16, 32};
	int pass_count = 0;

	for (int i = 0; i < ARRAY_SIZE(sizes); i++) {
		int size = sizes[i];
		init_tx_buffer(tx_buf, size, 0x60 + i);
		init_tx_buffer(slave_tx_buf, size, 0x90 + i);
		clear_rx_buffer(rx_buf, size);
		clear_rx_buffer(slave_rx_buf, size);

		struct spi_buf tx_bufs[1] = {{.buf = tx_buf, .len = size}};
		struct spi_buf rx_bufs[1] = {{.buf = rx_buf, .len = size}};
		struct spi_buf slave_tx_bufs[1] = {{.buf = slave_tx_buf, .len = size}};
		struct spi_buf slave_rx_bufs[1] = {{.buf = slave_rx_buf, .len = size}};
		struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 1};
		struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = 1};
		struct spi_buf_set slave_tx_set = {.buffers = slave_tx_bufs, .count = 1};
		struct spi_buf_set slave_rx_set = {.buffers = slave_rx_bufs, .count = 1};

		async_ctx_reset(&slave_ctx);
		int ret = spi_transceive_cb(g_spi_slave_dev, g_config_slave,
					    &slave_tx_set, &slave_rx_set,
					    spi_async_callback, &slave_ctx);
		if (ret >= 0)
			ret = spi_transceive(spi_master, config, &tx_set, &rx_set);
		if (ret >= 0)
			ret = wait_async_complete(&slave_ctx, 1000);

		if (ret >= 0 &&
		    !compare_buffers(slave_tx_buf, rx_buf, size,
				     "Size test master-rx"))
			ret = -1;
		if (ret >= 0 &&
		    !compare_buffers(tx_buf, slave_rx_buf, size,
				     "Size test slave-rx"))
			ret = -1;

		printf("  Size %d bytes: %s\n", size, ret >= 0 ? "OK" : "FAIL");

		if (ret >= 0) {
			pass_count++;
		}
	}

	printf("Size test: %d/%zu passed\n", pass_count, ARRAY_SIZE(sizes));
	return pass_count == ARRAY_SIZE(sizes) ? 0 : -1;
}

/* ============================================================================
 * Test Suite Runner
 * ============================================================================ */

/**
 * Execute all SPI tests
 */
int spi_test_suite_run(const struct device *spi_master, const struct device *spi_slave,
		       struct spi_config *config_m, struct spi_config *config_s)
{
	int total_tests = 0;
	int passed_tests = 0;

	g_spi_master_dev = spi_master;
	g_spi_slave_dev = spi_slave;
	g_config_master = config_m;
	g_config_slave = config_s;

	printf("\n========================================\n");
	printf("     SPI TEST SUITE BEGINNING\n");
	printf("========================================\n\n");

	/* Master synchronous tests */
	printf("[MASTER SYNC TESTS]\n");
	if (test_master_tx_only(spi_master, config_m) == 0) passed_tests++;
	total_tests++;
	if (test_master_rx_only(spi_master, config_m) == 0) passed_tests++;
	total_tests++;
	if (test_master_tx_rx(spi_master, config_m) == 0) passed_tests++;
	total_tests++;

	/* Master asynchronous tests */
	printf("\n[MASTER ASYNC TESTS]\n");
	if (test_master_tx_async(spi_master, config_m) == 0) passed_tests++;
	total_tests++;
	if (test_master_rx_async(spi_master, config_m) == 0) passed_tests++;
	total_tests++;
	if (test_master_tx_rx_async(spi_master, config_m) == 0) passed_tests++;
	total_tests++;

	/* Slave synchronous tests */
	printf("\n[SLAVE SYNC TESTS]\n");
	if (test_slave_tx_only(spi_slave, config_s) == 0) passed_tests++;
	total_tests++;
	if (test_slave_rx_only(spi_slave, config_s) == 0) passed_tests++;
	total_tests++;
	if (test_slave_tx_rx(spi_slave, config_s) == 0) passed_tests++;
	total_tests++;

	/* Slave asynchronous tests */
	printf("\n[SLAVE ASYNC TESTS]\n");
	if (test_slave_tx_async(spi_slave, config_s) == 0) passed_tests++;
	total_tests++;
	if (test_slave_rx_async(spi_slave, config_s) == 0) passed_tests++;
	total_tests++;
	if (test_slave_tx_rx_async(spi_slave, config_s) == 0) passed_tests++;
	total_tests++;

	/* Sequential and interleaved tests */
	printf("\n[MULTI-TRANSFER TESTS]\n");
	if (test_master_sequential(spi_master, config_m) == 0) passed_tests++;
	total_tests++;
	if (test_master_interleaved_async(spi_master, config_m) == 0) passed_tests++;
	total_tests++;

	/* Variable size tests */
	if (test_various_sizes(spi_master, config_m) == 0) passed_tests++;
	total_tests++;

	/* Summary */
	printf("\n========================================\n");
	printf("     TEST RESULTS SUMMARY\n");
	printf("========================================\n");
	printf("Total Tests: %d\n", total_tests);
	printf("Passed: %d\n", passed_tests);
	printf("Failed: %d\n", total_tests - passed_tests);
	printf("Success Rate: %d%%\n", (passed_tests * 100) / total_tests);
	printf("========================================\n\n");

	return (total_tests - passed_tests) == 0 ? 0 : -1;
}
