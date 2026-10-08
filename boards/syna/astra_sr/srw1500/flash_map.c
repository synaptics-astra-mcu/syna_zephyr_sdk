/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/devicetree/partitions.h>
#include <zephyr/storage/flash_map.h>

#define SRW1500_FLASH_DEVICE DT_CHOSEN(zephyr_flash)

#define SRW1500_FLASH_AREA(node) \
    { \
        .fa_id = DT_PARTITION_ID(node), \
        .fa_off = DT_REG_ADDR(node), \
        .fa_size = DT_REG_SIZE(node), \
        .fa_dev = DEVICE_DT_GET(SRW1500_FLASH_DEVICE), \
    }

static const struct flash_area srw1500_flash_map[] = {
    SRW1500_FLASH_AREA(DT_NODELABEL(slot0_partition)),

#if DT_NODE_EXISTS(DT_NODELABEL(boot_partition))
    SRW1500_FLASH_AREA(DT_NODELABEL(boot_partition)),
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(slot1_partition))
    SRW1500_FLASH_AREA(DT_NODELABEL(slot1_partition)),
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(scratch_partition))
    SRW1500_FLASH_AREA(DT_NODELABEL(scratch_partition)),
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(storage_partition))
    SRW1500_FLASH_AREA(DT_NODELABEL(storage_partition)),
#endif
};

/*
 * These types must match:
 * zephyr/subsys/storage/flash_map/flash_map_priv.h
 */
const struct flash_area *flash_map = srw1500_flash_map;

const int flash_map_entries = ARRAY_SIZE(srw1500_flash_map);
