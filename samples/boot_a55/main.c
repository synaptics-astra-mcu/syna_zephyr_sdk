/*
 * Copyright (c) 2026, Synaptics, Inc. All rights reserved.
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
    release_asoc(0xE2300000);
}
