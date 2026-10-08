/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define I2C_NODE DT_NODELABEL(i2c0)

int main(void)
{
    const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);
    uint8_t byte;
    int ret;

    printk("I2C Test Application Started\n");

    if (!device_is_ready(i2c_dev)) {
        printk("I2C device not ready: %p\n", i2c_dev);
        if (i2c_dev == NULL) {
            printk("I2C device pointer is NULL\n");
        } else {
            printk("I2C device exists but not ready\n");
        }
        return 0;
    }

    printk("I2C Bus Scan Started\n");
    printk("Scanning for devices on I2C bus...\n");
    printk("Addr: ");
    
    for (uint16_t addr = 0; addr < 128; addr++) {
        if ((addr & 0x0F) == 0) {
            printk("\n0x%02X: ", addr);
        }

        /* Try to read one byte from the address */
        ret = i2c_read(i2c_dev, &byte, 1, addr);
        
        if (ret == 0) {
            printk("%02x ", addr);
        } else {
            printk("-- ");
        }
    }

    printk("\n\nI2C scan complete\n");

    while (1) {
        k_msleep(1000);
    }

    return 0;
}
