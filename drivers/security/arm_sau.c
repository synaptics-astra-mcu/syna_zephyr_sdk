/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arm_security_common.h"
#include <cmsis_core.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/security/srw1500-security.h>

typedef struct
{
  uint32_t CTRL;                   /*!< Offset: 0x000 (R/W)  SAU Control Register */
  uint32_t TYPE;                   /*!< Offset: 0x004 (R/ )  SAU Type Register */
  uint32_t RNR;                    /*!< Offset: 0x008 (R/W)  SAU Region Number Register */
  uint32_t RBAR;                   /*!< Offset: 0x00C (R/W)  SAU Region Base Address Register */
  uint32_t RLAR;                   /*!< Offset: 0x010 (R/W)  SAU Region Limit Address Register */
  uint32_t SFSR;                   /*!< Offset: 0x014 (R/W)  Secure Fault Status Register */
  uint32_t SFAR;                   /*!< Offset: 0x018 (R/W)  Secure Fault Address Register */
} SAU_Type;

#define SCS_BASE            (0xE000E000UL)

#define SAU_BASE          (SCS_BASE +  0x0DD0UL)                     /*!< Security Attribution Unit */
#define SAU               ((SAU_Type       *)     SAU_BASE         ) /*!< Security Attribution Unit */

#ifndef SAU_CTRL_ENABLE_Msk
#define SAU_CTRL_ENABLE_Msk        (1UL << 0)
#endif

#ifndef SAU_RBAR_BADDR_Msk
#define SAU_RBAR_BADDR_Msk        (0xFFFFFFE0UL)
#endif

#ifndef SAU_RLAR_LADDR_Msk
#define SAU_RLAR_LADDR_Msk        (0xFFFFFFE0UL)
#endif

#ifndef SAU_RLAR_ENABLE_Msk
#define SAU_RLAR_ENABLE_Msk       (1UL << 0)
#endif

#ifndef SAU_RLAR_NSC_Msk
#define SAU_RLAR_NSC_Msk          (1UL << 1)
#endif

LOG_MODULE_REGISTER(arm_sau, CONFIG_LOG_DEFAULT_LEVEL);

struct arm_sau_region {
	uint32_t base;
	uint32_t size;
};

static int arm_sau_program_regions(const struct arm_sau_region *regions,
					   uint8_t region_count)
{
	SAU->CTRL &= ~(SAU_CTRL_ENABLE_Msk);

	for (uint8_t i = 0; i < region_count; i++) {
		const struct arm_sau_region *r = &regions[i];
		uint32_t limit;

		if (r->size == 0U) {
			continue;
		}

		limit = r->base + r->size - 1U;
		SAU->RNR = i;
		SAU->RBAR = r->base & SAU_RBAR_BADDR_Msk;
		SAU->RLAR = (limit & SAU_RLAR_LADDR_Msk) | SAU_RLAR_ENABLE_Msk;
	}

	SAU->CTRL |= SAU_CTRL_ENABLE_Msk;
	arm_barrier();
	return 0;
}

#if DT_NODE_HAS_STATUS(DT_NODELABEL(sau_cfg), okay)

static const struct arm_sau_region srw1500_sau_regions[] = {
	{ .base = 0x00000000U, .size = 0x00000000U },
	{ .base = 0x220a0000U, .size = 0x00050000U },
	{ .base = 0x00000000U, .size = 0x00000000U },
	{ .base = 0x40004000U, .size = 0x0fffc000U },
	{ .base = 0xa0000000U, .size = 0x10000000U },
	{ .base = 0xc0000000U, .size = 0x10000000U },
	{ .base = 0x00000000U, .size = 0x00000000U },
	{ .base = 0x00000000U, .size = 0x00000000U },
};

static int srw1500_sau_init(void)
{
	return arm_sau_program_regions(srw1500_sau_regions,
					       ARRAY_SIZE(srw1500_sau_regions));
}

SYS_INIT(srw1500_sau_init, PRE_KERNEL_1, CONFIG_ARM_SECURITY_INIT_PRIORITY);
#endif
