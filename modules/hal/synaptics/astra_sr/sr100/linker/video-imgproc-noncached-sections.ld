/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * nonCached sections for internal MIPI objects; ensure .nonCached* is mapped
 * to SRAM (NOLOAD) to avoid orphan placement in FLASH.
 */

SECTION_DATA_PROLOGUE(.nonCached, (NOLOAD),)
{
	. = ALIGN(32);
	KEEP(*(.nonCached))
	KEEP(*(.nonCached.*))
	. = ALIGN(32);
} GROUP_DATA_LINK_IN(RAMABLE_REGION, RAMABLE_REGION)
