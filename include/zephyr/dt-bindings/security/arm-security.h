/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_SECURITY_ARM_SECURITY_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_SECURITY_ARM_SECURITY_H_

/* Generic security attribution values used by the Arm security DT bindings. */
#define ARM_SECURE_ATTR_SECURE      0
#define ARM_SECURE_ATTR_NONSECURE   1
#define ARM_SECURE_ATTR_MIXED       2

/* SAU region tuple fields: <base size nsc>. */
#define ARM_SAU_REGION(base, size, nsc) base size nsc
#define ARM_SAU_REGION_NS(base, size)   ARM_SAU_REGION(base, size, 0)
#define ARM_SAU_REGION_NSC(base, size)  ARM_SAU_REGION(base, size, 1)

/* MPC controlled range tuple fields: <base size controller-offset attr>. */
#define ARM_MPC_RANGE(base, size, offset, attr) base size offset attr
#define ARM_MPC_RANGE_SECURE(base, size, offset) \
	ARM_MPC_RANGE(base, size, offset, ARM_SECURE_ATTR_SECURE)
#define ARM_MPC_RANGE_NONSECURE(base, size, offset) \
	ARM_MPC_RANGE(base, size, offset, ARM_SECURE_ATTR_NONSECURE)

/* MPC/TGU configured region tuple fields: <base size attr>. */
#define ARM_SECURITY_REGION(base, size, attr) base size attr
#define ARM_SECURITY_REGION_SECURE(base, size) \
	ARM_SECURITY_REGION(base, size, ARM_SECURE_ATTR_SECURE)
#define ARM_SECURITY_REGION_NONSECURE(base, size) \
	ARM_SECURITY_REGION(base, size, ARM_SECURE_ATTR_NONSECURE)

/* PPC peripheral tuple fields: <ns-bit priv-bit sresp-bit flags>. */
#define ARM_PPC_FLAG_NONSECURE      0x1
#define ARM_PPC_FLAG_NONPRIV        0x2
#define ARM_PPC_FLAG_ERROR_RESPONSE 0x4
#define ARM_PPC_FLAGS_NS_PRIV_SRESP 0x7
#define ARM_PPC_PERIPH(ns_bit, priv_bit, sresp_bit, flags) ns_bit priv_bit sresp_bit flags

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_SECURITY_ARM_SECURITY_H_ */
