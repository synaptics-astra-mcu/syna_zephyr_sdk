/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
/* Internal common source validation and image-transfer interface. */
#ifndef CON_BOOT_TRANSFER_H
#define CON_BOOT_TRANSFER_H

#include "con_boot.h"


int32_t con_boot_transfer_files(const con_boot_file_info *files,
                               size_t file_count, uint32_t timeout_ms);

#endif /* CON_BOOT_TRANSFER_H */
