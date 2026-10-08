/*
 * Copyright (c) 2026 Synaptics, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sd/sdio.h>
#include <zephyr/sd/mmc.h>
#include <zephyr/drivers/disk.h>
#include <stdio.h>

#if defined(CONFIG_SDHC) && !defined(CONFIG_MMC_STACK) && defined(CONFIG_FILE_SYSTEM)
#include <zephyr/fs/fs.h>
#include <ff.h>

#define DISK_NAME "SD"
#define FATFS_MNTP "/"DISK_NAME":"

static FATFS fat_fs = { 0 };
static struct fs_mount_t fatfs_mnt = {
	.type = FS_FATFS,
	.mnt_point = FATFS_MNTP,
	.fs_data = &fat_fs,
};

static struct fs_file_t filep;
static struct fs_dirent entry;
#endif /* CONFIG_SDHC */

#if defined (CONFIG_MMC_STACK)
#include <zephyr/fs/fs.h>
/*#include <zephyr/fs/ext2.h>

static struct fs_mount_t ext2_mnt = {
	.type = FS_EXT2,
	.mnt_point = "/emmc",
	.storage_dev = "EMMC",
	.flags = 0,
};
static struct fs_file_t filep;
static struct fs_dirent entry;*/
#endif

#if defined(CONFIG_SDIO_STACK)
static const struct device *sdhc_dev = DEVICE_DT_GET(DT_ALIAS(sdhc1));
#endif
static struct sd_card card;
static uint8_t buffer[1024 * 4];

int main(void)
{
#if defined(CONFIG_SDIO_STACK)
	struct sdio_func sdio_func1, sdio_func2;
	uint8_t reg = 0xFF;
#endif
	int i;
	int ret;

	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

#if defined(CONFIG_MMC_STACK) && defined(CONFIG_SDHC)
	const struct device *emmc0 = DEVICE_DT_GET(DT_NODELABEL(emmc0));
	uint32_t sector_count;
	uint32_t sector_size;
	uint32_t *lbuffer = (uint32_t *)buffer;

	if (device_is_ready(emmc0)) {
		ret = sd_is_card_present(emmc0);
		if (ret != 1) {
			printf("SD card not present in slot\n");
			return 0;
		}

		card.bus_width = 8;
		ret = sd_init(emmc0, &card);
		if (ret != 0) {
			printf("Card initialization failed\n");
			return 0;
		}

		if (card.type != CARD_MMC) {
			printf("Card type is not MMC: %d\n", card.type);
		}
		printf("card.flags 0x%x, width %d\n", card.flags, card.bus_width);

		ret = mmc_ioctl(&card, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
		printf("SD card reports sector count of %d (ret %d)\n", sector_count, ret);

		ret = mmc_ioctl(&card, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
		printf("SD card reports sector size of %d (ret %d)\n", sector_size, ret);

		ret = mmc_read_blocks(&card, buffer, 49152, 1);
		printf("read 1 block from block offset 49152 (%d): 0x%x 0x%x 0x%x 0x%x\n", ret, lbuffer[0], lbuffer[1],
			lbuffer[2], lbuffer[3]);

		for (i = 0; i < 1024; i++)
			buffer[i] = i;

		for (i = 0; i < 1024; i++)
			buffer[i] = 0;

		ret = mmc_write_blocks(&card, buffer, 49152, 2);
		printf("write 2 blocks at block offset 49152: %d\n", ret);

		ret = mmc_read_blocks(&card, buffer, 49152, 1);
		printf("read 1 block from block offset 49152 (%d): 0x%x 0x%x 0x%x 0x%x\n", ret, lbuffer[0], lbuffer[1],
			lbuffer[2], lbuffer[3]);

		for (i = 0; i < 1024; i++)
			buffer[i] = i;

		ret = mmc_write_blocks(&card, buffer, 49152, 2);
		printf("write 2 blocks at block offset 49152: %d\n", ret);

		ret = mmc_read_blocks(&card, buffer, 49152, 2);
		printf("read 2 blocks from block offset 49152 (%d): 0x%x 0x%x 0x%x 0x%x\n", ret, lbuffer[0], lbuffer[1],
			lbuffer[2], lbuffer[3]);

		/*
		ext2_mnt.flags |= FS_MOUNT_FLAG_NO_FORMAT;
		ret = fs_mount(&ext2_mnt);
		printf("fs_mount %d\n", ret);
		ext2_mnt.flags |= FS_MOUNT_FLAG_READ_ONLY;
		ret = fs_mount(&ext2_mnt);
		printf("fs_mount %d\n", ret);
		*/

	/*
	ret = fs_mkfs(FS_EXT2, ext2_mnt.storage_dev, NULL, ext2_mnt.storage_dev);

	fs_mkfs_mp->flags = FS_MOUNT_FLAG_NO_FORMAT;
	ret = fs_mount(fs_mkfs_mp);
	ret = fs_unmount(fs_mkfs_mp);
	*/

	}

#elif defined(CONFIG_SDHC) && defined(CONFIG_FILE_SYSTEM) && DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(sdhc0))
	const struct device *sdhc0 = DEVICE_DT_GET(DT_NODELABEL(sdhc0));
	if (device_is_ready(sdhc0)) {
		printf("start sdhc0\n");

		fs_file_t_init(&filep);
		fatfs_mnt.flags = 0; //FS_MOUNT_FLAG_READ_ONLY;
		printf("start sdhc0 - 1\n");
		ret = fs_mount(&fatfs_mnt);
		printf("start sdhc0 - 2: %d\n", ret);
		if (ret == 0) {
			ret = fs_open(&filep, "/SD:/spi.txt", FS_O_READ);
		}
		printf("start sdhc0 - 3: %d\n", ret);
		if (ret == 0) {
			ret = fs_close(&filep);
		}
		printf("start sdhc0 - 4: %d\n", ret);
		if (ret == 0) {
			ret = fs_stat("/SD:/spi.txt", &entry);
		}
		if (ret == 0 && entry.size == 147) {
			printf("SD/MMC successful\n");
		} else {
			printf("SD/MMC failed\n");
		}
		/* fs_file_t_init(&filep);
		ret = fs_open(&filep, "/SD:/new.txt", FS_O_RDWR | FS_O_CREATE);
		ret = fs_write(&filep, tx_buf, 11);
		ret = fs_close(&filep);
		ret = fs_sync(&filep); */
	}
#elif defined(CONFIG_SDIO_STACK)

	ret = sd_is_card_present(sdhc_dev);
	if (ret != 1) {
		printf("SD card not present in slot\n");
		return 0;
	}

	ret = sd_init(sdhc_dev, &card);
	if (ret != 0) {
		printf("Card initialization failed\n");
		return 0;
	}

	/* Read from card common I/O area. */
	ret = sdio_read_byte(&card.func0, SDIO_CCCR_CCCR, &reg);
	if (ret != 0) {
		printf("SD card read failed\n");
	}

	/* Check to make sure CCCR read actually returned valid data */
	if (reg == 0xFF) {
		printf("CCCR read returned invalid data\n");
	}

	ret = sdio_init_func(&card, &sdio_func1, 1);
	if (ret) {
		printf("sdio_init_func 1, error: %d\n", ret);
		return ret;
	}
	ret = sdio_init_func(&card, &sdio_func2, 2);
	if (ret) {
		printf("sdio_init_func 2, error: %d\n", ret);
		return ret;
	}

	ret = sdio_set_block_size(&card.func0, card.func0.cis.max_blk_size);
	if (ret) {
		printf("Can't set block size for function 0, error: %d\n", ret);
		return ret;
	}

	ret = sdio_set_block_size(&sdio_func1, 64);
	if (ret) {
		printf("Can't set block size for function 1, error: %d\n", ret);
		return ret;
	}

	ret = sdio_set_block_size(&sdio_func2, 512);
	if (ret) {
		printf("Can't set block size for function 2, error: %d\n", ret);
		return ret;
	}

	ret = sdio_enable_func(&sdio_func1);
	if (ret != 0) {
		printf("sdio_enable_func 1 failed, error: %d\n", ret);
	}

	switch (card.card_voltage) {
	case SD_VOL_1_2_V:
		printf("Card voltage: 1.2V\n");
		break;
	case SD_VOL_1_8_V:
		printf("Card voltage: 1.8V\n");
		break;
	case SD_VOL_3_0_V:
		printf("Card voltage: 3.0V\n");
		break;
	case SD_VOL_3_3_V:
		printf("Card voltage: 3.3V\n");
		break;
	default:
		printf("Card voltage is not known value\n");
	}
	if (card.status != CARD_INITIALIZED) {
		printf("Card status is not OK\n");
	}
	switch (card.card_speed) {
	case SD_TIMING_SDR12:
		printf("Card timing: SDR12\n");
		break;
	case SD_TIMING_SDR25:
		printf("Card timing: SDR25\n");
		break;
	case SD_TIMING_SDR50:
		printf("Card timing: SDR50\n");
		break;
	case SD_TIMING_SDR104:
		printf("Card timing: SDR104\n");
		break;
	case SD_TIMING_DDR50:
		printf("Card timing: DDR50\n");
		break;
	default:
		printf("Card timing is not known value\n");
	}
	switch (card.type) {
	case CARD_SDIO:
		printf("Card type: SDIO\n");
		break;
	case CARD_SDMMC:
		printf("Card type: SDMMC\n");
		break;
	case CARD_COMBO:
		printf("Card type: combo card\n");
		break;
	default:
		printf("Card type is not known value\n");
	}
	if (card.sd_version >= SD_SPEC_VER3_0) {
		printf("Card spec: 3.0\n");
	} else if (card.sd_version >= SD_SPEC_VER2_0) {
		printf("Card spec: 2.0\n");
	} else if (card.sd_version >= SD_SPEC_VER1_1) {
		printf("Card spec: 1.1\n");
	} else if (card.sd_version >= SD_SPEC_VER1_0) {
		printf("Card spec: 1.0\n");
	} else {
		printf("Card spec is unknown value\n");
	}

	ret = sdio_write_byte(&sdio_func1, 0x1000a, 0x0); // (0x170000 >> 8) & 0xff
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	ret = sdio_write_byte(&sdio_func1, 0x1000b, 0x0); // (0x170000 >> 16) & 0xff
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	ret = sdio_write_byte(&sdio_func1, 0x1000c, 0x18); // (0x170000 >> 24) & 0xff
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	ret = sdio_read_addr(&sdio_func1, 0x0, buffer, 4);
	printf("Chip ID: 0x%02x 0x%02x 0x%02x 0x%02x\n",
		buffer[0], buffer[1], buffer[2], buffer[3]);

	ret = sdio_write_byte(&card.func0, 0x10, 64);
	ret = sdio_write_byte(&card.func0, 0x110, 64);
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}
	reg = 0;
	ret = sdio_read_byte(&card.func0, 0x110, &reg);
	printf("read_byte 0x110: 0x%x\n", reg);

	/* ret = sdio_write_byte(&card.func0, 0x210, 0);
	ret = sdio_write_byte(&card.func0, 0x211, 1);
	ret = sdio_write_byte(&card.func0, 0x1000e, 0x29);
	ret = sdio_write_byte(&card.func0, 0x1000e, 0x0);
	ret = sdio_write_byte(&card.func0, 0x4034, 0x2);
	ret = sdio_write_byte(&card.func0, 0x10008, 0x40);
	ret = sdio_write_byte(&card.func0, 0x10009, 0x10);
	ret = sdio_write_byte(&card.func0, 0x1001d, 0xc0); */

	ret = sdio_write_byte(&sdio_func1, 0x1000a, 0x0); // (0x170000 >> 8) & 0xff
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	ret = sdio_write_byte(&sdio_func1, 0x1000b, 0x1b); // (0x170000 >> 16) & 0xff
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	ret = sdio_write_byte(&sdio_func1, 0x1000c, 0x0); // (0x170000 >> 24) & 0xff
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	for (i = 0; i < 64; i++) {
		buffer[i] = i;
	}

	for (i = 0; i < 10; i++) {
		ret = sdio_write_addr(&sdio_func1, 0x0, buffer, 64);
		if (ret != 0) {
			printf("CMD53 write failed\n");
		}
	}

	for (i = 0; i < 64; i++) {
		buffer[i] = 0;
	}

	ret = sdio_read_addr(&sdio_func1, 0x0, buffer, 64);
	if (ret != 0) {
		printf("CMD53 read failed\n");
	}
	printf("buffer content 0x%x 0x%x 0x%x 0x%x\n", buffer[0], buffer[1], buffer[2], buffer[3]);

	for (i = 0; i < 16; i++) {
		if (buffer[i] != i) {
			printf("CMD53: read/write mismatch\n");
			break;
		}
	}

	/* ret = sdio_enable_func(&sdio_func2);
	if (ret != 0) {
		printf("SD enable_func 2 failed\n");
	}*/

	ret = sdio_write_addr(&sdio_func1, 0x0, buffer, 128);
	if (ret != 0) {
		printf("CMD53 write failed\n");
	}

	ret = sdio_read_addr(&sdio_func1, 0x0, buffer, 128);
	if (ret != 0) {
		printf("CMD53 read failed\n");
	}
	printf("buffer content 0x%x 0x%x 0x%x 0x%x\n", buffer[0], buffer[1], buffer[2], buffer[3]);

	ret = sdio_write_byte(&sdio_func1, 0x1000b, 0x10);
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	ret = sdio_write_byte(&sdio_func1, 0x1000c, 0x18);
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	ret = sdio_write_byte(&sdio_func1, 0x10008, 0x60);
	ret = sdio_write_byte(&sdio_func1, 0x10009, 0x10);
	ret = sdio_write_byte(&sdio_func1, 0x1001d, 0xd0);
	ret = sdio_write_byte(&sdio_func1, 0x1000e, 0x0);
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	ret = sdio_write_byte(&sdio_func1, 0x1000e, 0x10);
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	i = 0;
	reg = 0;
	while (((reg & 0x80) == 0) && (i < 10)) {
		ret = sdio_read_byte(&sdio_func1, 0x1000e, &reg);
		if (ret != 0) {
			printf("CMD52 read failed\n");
		}
		i++;
		k_sleep(K_MSEC(100));
	}

	ret = sdio_write_byte(&sdio_func1, 0x1000e, 0xd2);
	if (ret != 0) {
		printf("CMD52 write failed\n");
	}

	ret = sdio_read_byte(&sdio_func1, 0x1000e, &reg);
	if (ret != 0) {
		printf("CMD52 read failed\n");
	}
	printf("0x1000e: 0x%x\n", reg);

#endif /* CONFIG_SDHC */
	return 0;
}
