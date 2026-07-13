/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dma_arm350.h
 * @brief DMA header file for ARM DMA-350 controllers.
 *
 * This file defines macros to select 2D features of the ARM DMA-350 controller
 * like rotation, flipping/mirroring and transposing.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_ARM350_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_ARM350_H_

/**
 * In order to configure the DMA350 for rotation or mirroring (flip), the user should supply a
 * command as the DMA slot parameter, like so:
 * dma_slot = DMA_ARM350_ROTATE_90
 *
 * It is assumed that the data is organized in a square in memory, i.e., the size of a row
 * equals the size of a column.
 * head block source address: input buffer address
 * head block destination address: output buffer address
 * head block block size: size of each row in bytes, equals size of each column
 */

/** 90 degree clock-wise rotation */
#define DMA_ARM350_ROTATE_90		1
/** 180 degree rotation */
#define DMA_ARM350_ROTATE_180		2
/** 270 degree clock-wise / 90 degree counter clock-wise rotation */
#define DMA_ARM350_ROTATE_270		3
/** Horizontal flip (mirror across vertical axis) */
#define DMA_ARM350_FLIP_HORIZONTAL	4
/** Vertical flip (mirror across horizontal axis) */
#define DMA_ARM350_FLIP_VERTICAL	5
/** Diagonal flip (across top-left <-> bottom-right axis) */
#define DMA_ARM350_FLIP_DIAG		6
/** Anti-diagonal flip (across top-right <-> bottom-left axis) */
#define DMA_ARM350_FLIP_DIAG_ANTI	7

#endif /* ZEPHYR_INCLUDE_DRIVERS_DMA_ARM350_H_ */
