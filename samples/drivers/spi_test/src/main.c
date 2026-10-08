/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

#define SPI_NODE DT_NODELABEL(spi0)

int main(void)
{
    int ret;
    const struct device *spi_dev = DEVICE_DT_GET(SPI_NODE);
    uint8_t pattern[] = { 0x3A, 0x55, 0x9B, 0x3A, 0x55, 0x9B };
    
    struct spi_config spi_cfg = {
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .frequency = 100000,
        .slave = 0,
    };
    
    struct spi_buf tx_buf = {
        .buf = pattern,
        .len = sizeof(pattern),
    };
    struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1,
    };

    if (!device_is_ready(spi_dev)) {
        printk("SPI device not ready\n");
        return 0;
    }

    printk("SPI continuous write start\n");

    while (1) {
        ret = spi_write(spi_dev, &spi_cfg, &tx);
        if (ret) {
            printk("spi_write failed: %d\n", ret);
            break;
        }

        printk("TX: %02x %02x %02x\n", pattern[0], pattern[1], pattern[2]);
        k_msleep(1);
    }

    return 0;
}
