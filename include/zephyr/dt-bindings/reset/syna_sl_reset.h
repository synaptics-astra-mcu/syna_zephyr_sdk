/*
 * Copyright (C) 2026 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_SYNA_SL_RESET_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_SYNA_SL_RESET_H_

/**
 * Encode register offset, sticky and config bit.
 *
 * - 0..5: config bit
 * - 6: reserved
 * - 7: sticky
 * - 8..31: offset
 *
 * @param offset register offset
 * @param bit config bit
 * @param sticky sticky or not
 */
#define SYNA_SL_RESET(offset, bit, sticky) ((offset << 8U) | (sticky << 7U) | (bit))

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_SYNA_SL_RESET_H_ */
