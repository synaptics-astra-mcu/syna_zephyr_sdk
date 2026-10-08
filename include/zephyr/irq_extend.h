/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Extended IRQ api's for clear and set interrupt. Note: Works only for arm architecture.
 */
#ifndef ZEPHYR_INCLUDE_IRQ_EXTEND_H_
#define ZEPHYR_INCLUDE_IRQ_H_

#include <cmsis_core.h>

/**
 * @brief Clear an pending IRQ.
 *
 * This routine clears pending interrupts from source @a irq.
 *
 * @param irq IRQ line.
 */
#define irq_clearpending(irq) NVIC_ClearPendingIRQ(irq)

/**
 * @brief Set an IRQ.
 *
 * This routine sets interrupts from source @a irq.
 *
 * @param irq IRQ line.
 */
#define irq_setpending(irq) NVIC_SetPendingIRQ(irq)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_IRQ_EXTEND_H_ */
