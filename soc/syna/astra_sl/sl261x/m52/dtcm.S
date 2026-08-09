/*
 * SPDX-CopyrightText: Copyright (c) Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

_ASM_FILE_PROLOGUE

GTEXT(soc_reset_hook)
SECTION_FUNC(TEXT, soc_reset_hook)
/* Write word-wise zeros to DTCM memory to initialize the ECC engine.
 * Use assembly to make sure the stack in DTCM is not accessed when calling or
 * leaving this function.
 */
    ldr     r0, =0x30000000
    ldr     r1, =0x30040000
    movs    r2, #0
1:  cmp     r0, r1
    itt     lo
    strlo   r2, [r0], #4
    blo     1b
    bx      lr
