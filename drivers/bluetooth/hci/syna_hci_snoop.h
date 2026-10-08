/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_HCI_SNOOP_H_
#define SYNA_HCI_SNOOP_H_

#include <stdint.h>

#ifdef CONFIG_BT_HCI_SNOOP

/**
 * @brief Log a TX HCI packet to the snoop console output.
 *
 * Non-blocking: enqueues into a k_msgq and returns immediately.
 * Silently drops the entry if the queue is full.
 */
void hci_snoop_tx(const uint8_t *data, uint32_t len);

/**
 * @brief Log an RX HCI packet to the snoop console output.
 *
 * Non-blocking: enqueues into a k_msgq and returns immediately.
 * Silently drops the entry if the queue is full.
 */
void hci_snoop_rx(const uint8_t *data, uint32_t len);

/**
 * @brief Pause snoop output (e.g. during patchram WRITE_RAM flood).
 */
void hci_snoop_pause(void);

/**
 * @brief Resume snoop output after a previous hci_snoop_pause().
 */
void hci_snoop_resume(void);

#else /* !CONFIG_BT_HCI_SNOOP */

static inline void hci_snoop_tx(const uint8_t *data, uint32_t len)   { ARG_UNUSED(data); ARG_UNUSED(len); }
static inline void hci_snoop_rx(const uint8_t *data, uint32_t len)   { ARG_UNUSED(data); ARG_UNUSED(len); }
static inline void hci_snoop_pause(void)  {}
static inline void hci_snoop_resume(void) {}

#endif /* CONFIG_BT_HCI_SNOOP */

#endif /* SYNA_HCI_SNOOP_H_ */
