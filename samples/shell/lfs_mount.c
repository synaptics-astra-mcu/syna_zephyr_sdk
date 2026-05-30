/*
 * lfs_mount.c — Auto-mount LittleFS on /lfs backed by storage_partition.
 */

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(lfs_mount, LOG_LEVEL_INF);

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);

static struct fs_mount_t lfs_mnt = {
    .type        = FS_LITTLEFS,
    .fs_data     = &storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point   = "/lfs",
};

static int lfs_mount_init(void)
{
    int ret = fs_mount(&lfs_mnt);

    if (ret == -ENODEV) {
        LOG_WRN("Flash device not ready, skipping LFS mount");
        return 0;
    }

    if (ret != 0) {
        LOG_ERR("LFS mount failed: %d — attempting format", ret);
        ret = fs_mkfs(FS_LITTLEFS, (uintptr_t)FIXED_PARTITION_ID(storage_partition),
                       NULL, 0);
        if (ret) {
            LOG_ERR("LFS format failed: %d", ret);
            return ret;
        }
        ret = fs_mount(&lfs_mnt);
        if (ret) {
            LOG_ERR("LFS mount after format failed: %d", ret);
            return ret;
        }
    }

    LOG_INF("LittleFS mounted on /lfs");
    return 0;
}

SYS_INIT(lfs_mount_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
