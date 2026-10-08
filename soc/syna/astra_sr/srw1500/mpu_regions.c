/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/arch/arm/mpu/arm_mpu_mem_cfg.h>

#define SRW1500_XSPI_NODE		DT_NODELABEL(xspi)
#define SRW1500_XSPI_XIP_BASE		DT_REG_ADDR_BY_NAME(SRW1500_XSPI_NODE, xspi_xip)
#define SRW1500_XSPI_XIP_SIZE		DT_REG_SIZE_BY_NAME(SRW1500_XSPI_NODE, xspi_xip)
#define SRW1500_XSPI_STIG_DATA_NODE	DT_NODELABEL(xspi_stig_data_partition)
#define SRW1500_XSPI_CODE_NODE		DT_NODELABEL(xspi_code_partition)
#define SRW1500_XSPI_DATA_NODE		DT_NODELABEL(storage_partition)

#define SRW1500_EXPECTED_XSPI_XIP_BASE	0x38000000U
#define SRW1500_EXPECTED_XSPI_XIP_SIZE	0x02000000U
#define SRW1500_XSPI_PARTITION_BASE(node_id)                                                        \
	(SRW1500_XSPI_XIP_BASE + DT_REG_ADDR(node_id))
#define SRW1500_XSPI_PARTITION_SIZE(node_id) DT_REG_SIZE(node_id)
#define SRW1500_XSPI_PARTITION_END(node_id)                                                         \
	(DT_REG_ADDR(node_id) + DT_REG_SIZE(node_id))
#define SRW1500_FLASH_DATA_SIZE                                                                     \
	(SRW1500_XSPI_XIP_SIZE - DT_REG_ADDR(SRW1500_XSPI_DATA_NODE))

#define SRW1500_FLASH_NOCACHE_ATTR(base, size)                                                     \
	{                                                                                          \
		.rbar = NOT_EXEC | P_RW_U_RW_Msk | NON_SHAREABLE_Msk,                               \
		.mair_idx = MPU_MAIR_INDEX_SRAM_NOCACHE,                                           \
		.r_limit = REGION_LIMIT_ADDR(base, size),                                          \
		IF_ENABLED(CONFIG_ARM_MPU_PXN, (.pxn = PRIV_EXEC_NEVER,))                          \
	}

#define SRW1500_SRAM_TEXT_ATTR(base, size)                                                         \
	{                                                                                          \
		.rbar = RO_Msk | NON_SHAREABLE_Msk,                                                \
		.mair_idx = MPU_MAIR_INDEX_SRAM,                                                   \
		.r_limit = REGION_LIMIT_ADDR(base, size),                                          \
		IF_ENABLED(CONFIG_ARM_MPU_PXN, (.pxn = !PRIV_EXEC_NEVER,))                         \
	}

#define SRW1500_FLASH_XIP_ATTR(base, size)                                                         \
	{                                                                                          \
		.rbar = RO_Msk | NON_SHAREABLE_Msk,                                                \
		.mair_idx = MPU_MAIR_INDEX_FLASH,                                                  \
		.r_limit = REGION_LIMIT_ADDR(base, size),                                          \
		IF_ENABLED(CONFIG_ARM_MPU_PXN, (.pxn = !PRIV_EXEC_NEVER,))                         \
	}

#ifdef CONFIG_ARM_MPU_SRAM_WRITE_THROUGH
#define ARM_MPU_SRAM_REGION_ATTR REGION_RAM_WT_ATTR
#else
#define ARM_MPU_SRAM_REGION_ATTR REGION_RAM_ATTR
#endif

BUILD_ASSERT(IS_ENABLED(CONFIG_XIP) && IS_ENABLED(CONFIG_FLASH_CAD_XSPI),
	     "SRW1500 custom MPU regions are only for XSPI XIP builds");
BUILD_ASSERT(DT_NODE_EXISTS(SRW1500_XSPI_STIG_DATA_NODE),
	     "SRW1500 XSPI STIG data partition is required");
BUILD_ASSERT(DT_NODE_EXISTS(SRW1500_XSPI_CODE_NODE),
	     "SRW1500 XSPI code partition is required");
BUILD_ASSERT(DT_NODE_EXISTS(SRW1500_XSPI_DATA_NODE),
	     "SRW1500 XSPI data partition is required");
BUILD_ASSERT(SRW1500_XSPI_XIP_BASE == SRW1500_EXPECTED_XSPI_XIP_BASE,
	     "SRW1500 XSPI XIP aperture must start at 0x38000000");
BUILD_ASSERT(SRW1500_XSPI_XIP_SIZE == SRW1500_EXPECTED_XSPI_XIP_SIZE,
	     "SRW1500 XSPI XIP aperture must be 32 MiB");
BUILD_ASSERT(DT_REG_ADDR(SRW1500_XSPI_STIG_DATA_NODE) == 0U,
	     "SRW1500 XSPI STIG data partition must start at the XIP aperture base");
BUILD_ASSERT(SRW1500_XSPI_PARTITION_END(SRW1500_XSPI_STIG_DATA_NODE) ==
		     DT_REG_ADDR(SRW1500_XSPI_CODE_NODE),
	     "SRW1500 XSPI STIG data and code partitions must be contiguous");
BUILD_ASSERT(SRW1500_XSPI_PARTITION_END(SRW1500_XSPI_CODE_NODE) ==
		     DT_REG_ADDR(SRW1500_XSPI_DATA_NODE),
	     "SRW1500 XSPI code and data partitions must be contiguous");
BUILD_ASSERT(SRW1500_XSPI_XIP_SIZE > DT_REG_ADDR(SRW1500_XSPI_DATA_NODE),
	     "SRW1500 XSPI XIP aperture must cover the data/storage MPU region");
BUILD_ASSERT(SRW1500_XSPI_PARTITION_END(SRW1500_XSPI_DATA_NODE) <= SRW1500_XSPI_XIP_SIZE,
	     "SRW1500 XSPI data partition must fit within the XIP aperture");
#if CONFIG_FLASH_BASE_ADDRESS != SRW1500_XSPI_XIP_BASE
BUILD_ASSERT((CONFIG_FLASH_BASE_ADDRESS + (CONFIG_FLASH_SIZE * 1024U)) <= CONFIG_SRAM_BASE_ADDRESS,
	     "SRW1500 SRAM text MPU region must not overlap SRAM data");
#endif

static const struct arm_mpu_region mpu_regions[] = {
#if CONFIG_FLASH_BASE_ADDRESS != SRW1500_XSPI_XIP_BASE
	MPU_REGION_ENTRY("SRAM_TEXT",
			 CONFIG_FLASH_BASE_ADDRESS,
			 SRW1500_SRAM_TEXT_ATTR(CONFIG_FLASH_BASE_ADDRESS,
						CONFIG_FLASH_SIZE * 1024U)),
#endif

	MPU_REGION_ENTRY("FLASH_STIG_DATA",
			 SRW1500_XSPI_PARTITION_BASE(SRW1500_XSPI_STIG_DATA_NODE),
			 SRW1500_FLASH_NOCACHE_ATTR(
				 SRW1500_XSPI_PARTITION_BASE(SRW1500_XSPI_STIG_DATA_NODE),
				 SRW1500_XSPI_PARTITION_SIZE(SRW1500_XSPI_STIG_DATA_NODE))),

	MPU_REGION_ENTRY("FLASH_0",
			 SRW1500_XSPI_PARTITION_BASE(SRW1500_XSPI_CODE_NODE),
			 SRW1500_FLASH_XIP_ATTR(
				 SRW1500_XSPI_PARTITION_BASE(SRW1500_XSPI_CODE_NODE),
				 SRW1500_XSPI_PARTITION_SIZE(SRW1500_XSPI_CODE_NODE))),

	MPU_REGION_ENTRY("FLASH_DATA",
			 SRW1500_XSPI_PARTITION_BASE(SRW1500_XSPI_DATA_NODE),
			 SRW1500_FLASH_NOCACHE_ATTR(
				 SRW1500_XSPI_PARTITION_BASE(SRW1500_XSPI_DATA_NODE),
				 SRW1500_FLASH_DATA_SIZE)),

	MPU_REGION_ENTRY("SRAM_0",
			 CONFIG_SRAM_BASE_ADDRESS,
			 ARM_MPU_SRAM_REGION_ATTR(CONFIG_SRAM_BASE_ADDRESS,
						  CONFIG_SRAM_SIZE * 1024U)),
};

const struct arm_mpu_config mpu_config = {
	.num_regions = ARRAY_SIZE(mpu_regions),
	.mpu_regions = mpu_regions,
};
