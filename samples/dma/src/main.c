/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/cache.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_arm350.h>
#include "spi_tests.h"

static struct k_sem dma_mutex;

static void test_done(const struct device *dma_dev, void *arg,
		      uint32_t id, int status)
{
	if (status >= 0) {
		printf("DMA successful\n");
	} else {
		printf("DMA failed\n");
	}

	k_sem_give(&dma_mutex);
}

static int test_task(const struct device *dma, uint32_t chan_id, uint32_t blen, uint8_t *tx_data,
		     uint8_t *rx_data, uint32_t len, int type)
{
	struct dma_config dma_cfg = { 0 };
	struct dma_block_config dma_block_cfg = { 0 };
	int ret = 0;

	if (!device_is_ready(dma)) {
		printf("DMA controller device is not ready\n");
		return -1;
	}

	dma_cfg.channel_direction = MEMORY_TO_MEMORY;
	dma_cfg.source_data_size = 1U;
	dma_cfg.dest_data_size = 1U;
	dma_cfg.source_burst_length = 1U;
	dma_cfg.dest_burst_length = 1U;
	dma_cfg.dma_callback = test_done;
	dma_cfg.complete_callback_en = 1U;
	dma_cfg.error_callback_dis = 0U;
	dma_cfg.block_count = 1U;
	dma_cfg.head_block = &dma_block_cfg;
	dma_cfg.dma_slot = type;

	(void)memset(tx_data, 0, len);
	dma_block_cfg.block_size = len;
	dma_block_cfg.source_address = (uint32_t)rx_data;
	dma_block_cfg.dest_address = (uint32_t)tx_data;

	if (type) {
		sys_cache_data_flush_range(tx_data, len * len);
		sys_cache_data_flush_range(rx_data, len * len);
	} else {
		sys_cache_data_flush_range(tx_data, len);
		sys_cache_data_flush_range(rx_data, len);
	}

	if (dma_config(dma, chan_id, &dma_cfg)) {
		printf("ERROR: config\n");
		return -1;
	}

	if (dma_start(dma, chan_id)) {
		printf("ERROR: start\n");
		return -1;
	}

	if (k_sem_take(&dma_mutex, K_FOREVER)) {
		ret = -EACCES;
	}

	dma_stop(dma, chan_id);

	if (type) {
		sys_cache_data_invd_range(tx_data, len * len);
	} else {
		sys_cache_data_invd_range(tx_data, len);
	}

	return ret;
}

static uint32_t uart_user_data;
static uint8_t second_buf[256];

static void uart_read_callback(const struct device *dev,
			       struct uart_event *evt, void *user_data)
{
	printf("event %d\n", evt->type);
	if (evt->type == UART_RX_BUF_REQUEST) {
		uart_rx_buf_rsp(dev, second_buf, 8);
		printf("RX buf request\n");
	}
	if (evt->type == UART_RX_BUF_RELEASED) {
		printf("RX buf released\n");
	}
	if (evt->type == UART_RX_RDY) {
		sys_cache_data_invd_range(evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len);
		evt->data.rx.buf[evt->data.rx.offset + evt->data.rx.len] = 0;
		printf("received (off %d, len %d): %s\n", evt->data.rx.offset, evt->data.rx.len,
			evt->data.rx.buf + evt->data.rx.offset);
	}
	if (evt->type == UART_TX_ABORTED) {
		printf("TX aborted\n");
	}
}

struct spi_cs_control cs_ctrl = (struct spi_cs_control){
	.setup_ns = 100u,
	.hold_ns = 100u,
	.cs_is_gpio = 0,
};

int main(void)
{
	const struct device *const dma_dev = DEVICE_DT_GET(DT_ALIAS(dma0));
	const struct device *const uart_dev = DEVICE_DT_GET(DT_ALIAS(uart0));
	uint8_t tx_buf[256], rx_buf[256];
	int i, ret;
	const struct device *const spi_m_dev = DEVICE_DT_GET(DT_ALIAS(spi0));
	const struct device *const spi_s_dev = DEVICE_DT_GET(DT_ALIAS(spi1));
	struct spi_config config_m, config_s;

	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	k_sem_init(&dma_mutex, 0, 1);

	if (!device_is_ready(spi_m_dev) || !device_is_ready(spi_s_dev)) {
		printk("%s: device not ready.\n", spi_m_dev->name);
		return 0;
	}

	config_m.frequency = config_s.frequency = 1000000;
	config_m.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8);
	config_s.operation = SPI_OP_MODE_SLAVE | SPI_WORD_SET(8);
	config_m.slave = config_s.slave = 0;
	config_m.cs = config_s.cs = cs_ctrl;

	int test_result = spi_test_suite_run(spi_m_dev, spi_s_dev, &config_m, &config_s);

	if (test_result == 0) {
		printf("\n✓ All SPI tests passed!\n");
	} else {
		printf("\n✗ Some SPI tests failed!\n");
	}

	for (i = 0; i < 256; i++) {
		rx_buf[i] = i;
		tx_buf[i] = 0;
	}
	ret = test_task(dma_dev, 0, 1, tx_buf, rx_buf, 8, DMA_ARM350_ROTATE_90);
	printf("DMA: %d (%d %d %d %d)\n", ret, tx_buf[1], tx_buf[2], tx_buf[3], tx_buf[4]);
	for (i = 0; i < 64; i++) {
		if (i > 0 && (i % 8) == 0)
			printf("\n");
		printf("%2d ", tx_buf[i]);
	}
	printf("\n");

	ret = test_task(dma_dev, 0, 1, tx_buf, rx_buf, 8, DMA_ARM350_ROTATE_180);
	printf("DMA: %d (%d %d %d %d)\n", ret, tx_buf[1], tx_buf[2], tx_buf[3], tx_buf[4]);
	for (i = 0; i < 64; i++) {
		if (i > 0 && (i % 8) == 0)
			printf("\n");
		printf("%2d ", tx_buf[i]);
	}
	printf("\n");

	ret = test_task(dma_dev, 0, 1, tx_buf, rx_buf, 8, DMA_ARM350_ROTATE_270);
	printf("DMA: %d (%d %d %d %d)\n", ret, tx_buf[1], tx_buf[2], tx_buf[3], tx_buf[4]);
	for (i = 0; i < 64; i++) {
		if (i > 0 && (i % 8) == 0)
			printf("\n");
		printf("%2d ", tx_buf[i]);
	}
	printf("\n");

	ret = test_task(dma_dev, 0, 1, tx_buf, rx_buf, 8, DMA_ARM350_FLIP_HORIZONTAL);
	printf("DMA: %d (%d %d %d %d)\n", ret, tx_buf[1], tx_buf[2], tx_buf[3], tx_buf[4]);
	for (i = 0; i < 64; i++) {
		if (i > 0 && (i % 8) == 0)
			printf("\n");
		printf("%2d ", tx_buf[i]);
	}
	printf("\n");

	ret = test_task(dma_dev, 0, 1, tx_buf, rx_buf, 8, DMA_ARM350_FLIP_VERTICAL);
	printf("DMA: %d (%d %d %d %d)\n", ret, tx_buf[1], tx_buf[2], tx_buf[3], tx_buf[4]);
	for (i = 0; i < 64; i++) {
		if (i > 0 && (i % 8) == 0)
			printf("\n");
		printf("%2d ", tx_buf[i]);
	}
	printf("\n");

	ret = test_task(dma_dev, 0, 1, tx_buf, rx_buf, 8, DMA_ARM350_FLIP_DIAG);
	printf("DMA: %d (%d %d %d %d)\n", ret, tx_buf[1], tx_buf[2], tx_buf[3], tx_buf[4]);
	for (i = 0; i < 64; i++) {
		if (i > 0 && (i % 8) == 0)
			printf("\n");
		printf("%2d ", tx_buf[i]);
	}
	printf("\n");

	ret = test_task(dma_dev, 0, 1, tx_buf, rx_buf, 8, DMA_ARM350_FLIP_DIAG_ANTI);
	printf("DMA: %d (%d %d %d %d)\n", ret, tx_buf[1], tx_buf[2], tx_buf[3], tx_buf[4]);
	for (i = 0; i < 64; i++) {
		if (i > 0 && (i % 8) == 0)
			printf("\n");
		printf("%2d ", tx_buf[i]);
	}
	printf("\n");

	strcpy(tx_buf, "Hello World!\n");
	sys_cache_data_flush_range(tx_buf, sizeof(tx_buf));

	ret = uart_tx(uart_dev, tx_buf, strlen(tx_buf), 100);
	printf("uart_tx %d\n", ret);

	uart_callback_set(uart_dev, uart_read_callback, (void *)&uart_user_data);
	uart_rx_enable(uart_dev, rx_buf, 8, 100);

	while (true)
		;

	return 0;
}
