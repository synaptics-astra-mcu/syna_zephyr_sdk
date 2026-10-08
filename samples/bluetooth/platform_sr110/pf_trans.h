/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PF_TRANS_H
#define PF_TRANS_H

#include <stdint.h>

#define PF_TRANS_STATUS_STOP  0
#define PF_TRANS_STATUS_READY 1

int      pf_trans_init(uint32_t baud);
void     pf_trans_deinit(void);
uint8_t  pf_trans_get_status(void);
void     pf_trans_reconfig_baud(uint32_t baud);
int32_t  pf_trans_send(const uint8_t *p_data, uint32_t length);
int32_t  pf_trans_receive(unsigned char *p_data, unsigned int length, unsigned int timeout_ms);

#endif /* PF_TRANS_H */
