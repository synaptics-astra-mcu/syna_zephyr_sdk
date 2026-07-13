/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_SYNA_SL261X_CLOCK_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_SYNA_SL261X_CLOCK_H_

#define CLK_ENABLE1		0x00

#define UART1_CTRL		0x04
#define UART2_CTRL		0x08
#define UART3_CTRL		0x0c
#define GPIO_CTRL		0x10
#define I2C0_CTRL		0x14
#define I2C1_CTRL		0x18
#define SPIM_CTRL		0x1C
#define SPIS_CTRL		0x20
#define I3C_CTRL		0x24
#define XSPI_CTRL		0x28
#define PVT_CTRL		0x2C
#define ADC_CTRL		0x30
#define PWM_CTRL		0x34
#define CAN0_CTRL		0x38
#define CAN1_CTRL		0x3c
#define PDM_CTRL		0x40
#define REF_CALIB_CTRL		0x44

/* Bit locations in CLK_ENABLE1 register */
#define CTRL_REG		16
#define CLK_ID			0
#define SER_ID			8
#define NO_CLK_ID		0xff

#define SYNA_UART0_CLK		(((1) << SER_ID) | 0)
#define SYNA_UART1_CLK		((NO_CLK_ID << SER_ID) |  2 | (UART1_CTRL << CTRL_REG))
#define SYNA_UART2_CLK		((NO_CLK_ID << SER_ID) |  3 | (UART2_CTRL << CTRL_REG))
#define SYNA_UART3_CLK		((NO_CLK_ID << SER_ID) |  4 | (UART3_CTRL << CTRL_REG))
#define SYNA_GPIO_CLK		((NO_CLK_ID << SER_ID) |  5 | (GPIO_CTRL << CTRL_REG))
#define SYNA_I2C0_CLK		((NO_CLK_ID << SER_ID) |  6 | (I2C0_CTRL << CTRL_REG))
#define SYNA_I2C1_CLK		((NO_CLK_ID << SER_ID) |  7 | (I2C1_CTRL << CTRL_REG))
#define SYNA_SPIM_CLK		((NO_CLK_ID << SER_ID) |  8 | (SPIM_CTRL << CTRL_REG))
#define SYNA_SPIS_CLK		((NO_CLK_ID << SER_ID) |  9 | (SPIS_CTRL << CTRL_REG))
#define SYNA_I3C_CLK		((NO_CLK_ID << SER_ID) | 10 | (I3C_CTRL << CTRL_REG))
#define SYNA_CAN0_CLK		((15 << SER_ID) | 16 | (CAN0_CTRL << CTRL_REG))
#define SYNA_CAN1_CLK		((16 << SER_ID) | 17 | (CAN1_CTRL << CTRL_REG))

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_SYNA_SL261X_CLOCK_H_ */
