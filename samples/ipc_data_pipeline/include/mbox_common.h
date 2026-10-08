/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * @brief Shared mailbox message types and timestamp formatting utilities for HOST/CLIENT.
 *
 * @file mbox_common.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MBOX_COMMON_H
#define MBOX_COMMON_H

#include <stdint.h>

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/* Identifies the source interface of a mailbox message. */
typedef uint32_t source_t;

/* Mailbox payload header shared by HOST and CLIENT. */
struct mbox_message {
	source_t source;    /* Source interface identifier */
	uint32_t counter;   /* Message counter for sequence tracking */
	int64_t timestamp;  /* Kernel timestamp when message was created */
};

/* ============================================================================
 * Utility Macros
 * ============================================================================ */

/* Timestamp formatting macros for HH:MM:SS.mmm display */
#define TS_HRS(ms)  ((int)((ms) / 3600000))
#define TS_MIN(ms)  ((int)(((ms) / 60000) % 60))
#define TS_SEC(ms)  ((int)(((ms) / 1000) % 60))
#define TS_MS(ms)   ((int)((ms) % 1000))
#define TS_FMT      "%02d:%02d:%02d.%03d"
#define TS_ARGS(ms) TS_HRS(ms), TS_MIN(ms), TS_SEC(ms), TS_MS(ms)

#endif /* MBOX_COMMON_H */
