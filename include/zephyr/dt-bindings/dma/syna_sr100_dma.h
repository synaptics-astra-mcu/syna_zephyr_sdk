/*
 * Copyright (c) 2025 Synaptics, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Vendor-specific DMA peripheral triggering sources.
 *
 * A valid triggering source should be provided when DMA
 * is configured for peripheral to peripheral or memory to peripheral
 * transactions.
 */

#ifndef SYNA_SR100_DMA_H_
#define SYNA_SR100_DMA_H_

/** @brief id for I2C master 0 - RX */
#define DMA_SYNA_TRIG_I2C_M0_RX		0
/** @brief id for I2C master 0 - TX */
#define DMA_SYNA_TRIG_I2C_M0_TX		1
/** @brief id for I2C master 1 - RX */
#define DMA_SYNA_TRIG_I2C_M1_RX		2
/** @brief id for I2C master 1 - TX */
#define DMA_SYNA_TRIG_I2C_M1_TX		3
/** @brief id for I2C slave - RX */
#define DMA_SYNA_TRIG_I2C_S_RX		4
/** @brief id for I2C slave - TX */
#define DMA_SYNA_TRIG_I2C_S_TX		5
/** @brief id for SPI master - RX */
#define DMA_SYNA_TRIG_SPI_M_RX		6
/** @brief id for SPI master - TX */
#define DMA_SYNA_TRIG_SPI_M_TX		7
/** @brief id for SPI slave - RX */
#define DMA_SYNA_TRIG_SPI_S_RX		8
/** @brief id for SPI slave - TX */
#define DMA_SYNA_TRIG_SPI_S_TX		9
/** @brief id for UART 0 - RX */
#define DMA_SYNA_TRIG_UART_0_RX		10
/** @brief id for UART 0 - TX */
#define DMA_SYNA_TRIG_UART_0_TX		11
/** @brief id for UART 1 - RX */
#define DMA_SYNA_TRIG_UART_1_RX		12
/** @brief id for UART 1 - TX */
#define DMA_SYNA_TRIG_UART_1_TX		13
/** @brief id for I3C 0 - RX */
#define DMA_SYNA_TRIG_I3C_0_RX		14
/** @brief id for I3C 0 - TX */
#define DMA_SYNA_TRIG_I3C_0_TX		15
/** @brief id for I3C 1 - RX */
#define DMA_SYNA_TRIG_I3C_1_RX		16
/** @brief id for I3C 1 - TX */
#define DMA_SYNA_TRIG_I3C_1_TX		17
/** @brief id for I2S - RX */
#define DMA_SYNA_TRIG_I2S_RX		18
/** @brief id for I2S - TX */
#define DMA_SYNA_TRIG_I2S_TX		19
/** @brief id for Soundwire - RX */
#define DMA_SYNA_TRIG_SOUNDWIRE_RX	20
/** @brief id for Soundwire - TX */
#define DMA_SYNA_TRIG_SOUNDWIRE_TX	21
/** @brief id for UART 2 - RX */
#define DMA_SYNA_TRIG_UART_2_RX		22
/** @brief id for UART 2 - TX */
#define DMA_SYNA_TRIG_UART_2_TX		23
/** @brief id for LPS FMIN 0 */
#define DMA_SYNA_TRIG_LPS_FMIN0		24
/** @brief id for LPS FMIN 1-B0 */
#define DMA_SYNA_TRIG_LPS_FMIN1_B0	25
/** @brief id for LPS FMIN 1-B1 */
#define DMA_SYNA_TRIG_LPS_FMIN1_B1	26
/** @brief id for LPS FMIN 1-B2 */
#define DMA_SYNA_TRIG_LPS_FMIN1_B2	27
/** @brief id for LPS AMIN */
#define DMA_SYNA_TRIG_LPS_AMIN		28

#endif /* SYNA_SR100_DMA_H_ */
