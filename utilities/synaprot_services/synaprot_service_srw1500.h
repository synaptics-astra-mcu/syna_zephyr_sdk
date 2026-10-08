/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SYNA_PROT_SERVICE_SRW1500_H_
#define _SYNA_PROT_SERVICE_SRW1500_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @brief   Verify image signature using the Synaptics mailbox protocol
 *
 * @param[in]   pubkey      - Pointer to the public key
 * @param[in]   pubkey_len  - Length of the public key
 * @param[in]   hash        - Pointer to the hash
 * @param[in]   hash_len    - Length of the hash
 * @param[in]   sig         - Pointer to the signature
 * @param[in]   sig_len     - Length of the signature
 *
 * @return  0 on success, non-zero error code on failure
 */
int syna_image_verify_signature(const uint8_t *pubkey, size_t pubkey_len,
                                 const uint8_t *hash, size_t hash_len,
                                 const uint8_t *sig, size_t sig_len);

#endif // _SYNA_PROT_SERVICE_SRW1500_H_
