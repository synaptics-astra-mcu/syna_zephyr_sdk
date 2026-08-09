/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
*/

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>
#include <asoc.h>

int main(int argc, char *argv[])
{
    irq_disable(DT_IRQN(DT_NODELABEL(i2c0)));
    release_asoc(0xE2300000);
}
