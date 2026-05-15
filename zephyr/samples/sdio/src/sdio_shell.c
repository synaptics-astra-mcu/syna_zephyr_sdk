#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/sd/sdio.h>
#include <zephyr/devicetree.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <zephyr/sys/util.h>

#define WIFI_SEQ_TEST 0

static bool sdio_ready = false;
static struct sd_card sdio_card = { 0 };
static struct k_mutex sdio_ops_mutex;
static struct sdio_func sdio_func1, sdio_func2;
static uint8_t buffer[1024 * 8 + 256];

static int cmd_sdio_init(const struct shell *sh, size_t argc, char **argv)
{
    int ret;
    uint8_t reg = 0xFF;

    shell_print(sh, "Initializing SDIO interface...");

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(sdhc1))
    const struct device *sdio = DEVICE_DT_GET(DT_NODELABEL(sdhc1));

    shell_print(sh, "  Initializing mutex...");
    k_mutex_init(&sdio_ops_mutex);

    if (!device_is_ready(sdio)) {
        shell_error(sh, "SDIO device not ready");
        return -ENODEV;
    }

    shell_print(sh, "  Checking card presence...");
    ret = sd_is_card_present(sdio);
    if (ret != 1) {
        shell_error(sh, "SDIO card not detected");
        return -ENODEV;
    }

    shell_print(sh, "  Initializing SDIO card...");
    ret = sd_init(sdio, &sdio_card);
    if (ret) {
        shell_error(sh, "SDIO initialization failed: %d", ret);
        return ret;
    }

    shell_print(sh, "  10us Reading CCCR register...");
    ret = sdio_read_byte(&sdio_card.func0, SDIO_CCCR_CCCR, &reg);
    if (ret || reg == 0xFF) {
        shell_error(sh, "SDIO communication failed (CCCR read error)");
        return -EIO;
    }

    ret = sdio_init_func(&sdio_card, &sdio_func1, 1);
	if (ret) {
		shell_print(sh,"sdio_init_func 1, error: %d\n", ret);
		return ret;
	}
	ret = sdio_init_func(&sdio_card, &sdio_func2, 2);
	if (ret) {
		shell_print(sh,"sdio_init_func 2, error: %d\n", ret);
		return ret;
	}

	ret = sdio_set_block_size(&sdio_card.func0, sdio_card.func0.cis.max_blk_size);
	if (ret) {
		shell_print(sh,"Can't set block size for function 0, error: %d\n", ret);
		return ret;
	}

	ret = sdio_set_block_size(&sdio_func1, 64);
	if (ret) {
		shell_print(sh,"Can't set block size for function 1, error: %d\n", ret);
		return ret;
	}

	ret = sdio_set_block_size(&sdio_func2, 256);
	if (ret) {
		shell_print(sh,"Can't set block size for function 2, error: %d\n", ret);
		return ret;
	}

    shell_print(sh,"[rs.log] fn0: num=%d bs=%d card=%p manf_id=0x%04X manf_code=0x%04X func_id=0x%02X max_blk_size=%d max_speed=0x%02X rdy_timeout=%d\n",
        sdio_card.func0.num, sdio_card.func0.block_size, sdio_card.func0.card, sdio_card.func0.cis.manf_id, sdio_card.func0.cis.manf_code, sdio_card.func0.cis.func_id, sdio_card.func0.cis.max_blk_size, sdio_card.func0.cis.max_speed, sdio_card.func0.cis.rdy_timeout);
    
    shell_print(sh,"[rs.log] fn1: num=%d bs=%d card=%p manf_id=0x%04X manf_code=0x%04X func_id=0x%02X max_blk_size=%d max_speed=0x%02X rdy_timeout=%d\n",
        sdio_func1.num, sdio_func1.block_size, sdio_func1.card, sdio_func1.cis.manf_id, sdio_func1.cis.manf_code, sdio_func1.cis.func_id, sdio_func1.cis.max_blk_size, sdio_func1.cis.max_speed, sdio_func1.cis.rdy_timeout);

    shell_print(sh,"[rs.log] fn2: num=%d bs=%d card=%p manf_id=0x%04X manf_code=0x%04X func_id=0x%02X max_blk_size=%d max_speed=0x%02X rdy_timeout=%d\n",
        sdio_func2.num, sdio_func2.block_size, sdio_func2.card, sdio_func2.cis.manf_id, sdio_func2.cis.manf_code, sdio_func2.cis.func_id, sdio_func2.cis.max_blk_size, sdio_func2.cis.max_speed, sdio_func2.cis.rdy_timeout);

	ret = sdio_enable_func(&sdio_func1);
	if (ret != 0) {
		shell_print(sh,"sdio_enable_func 1 failed, error: %d\n", ret);
	}
/*
    ret = sdio_enable_func(&sdio_func2);
	if (ret != 0) {
		shell_print(sh,"sdio_enable_func 2 failed, error: %d\n", ret);
	}
*/
    switch (sdio_card.card_voltage) {
	case SD_VOL_1_2_V:
		shell_print(sh,"Card voltage: 1.2V\n");
		break;
	case SD_VOL_1_8_V:
		shell_print(sh,"Card voltage: 1.8V\n");
		break;
	case SD_VOL_3_0_V:
		shell_print(sh,"Card voltage: 3.0V\n");
		break;
	case SD_VOL_3_3_V:
		shell_print(sh,"Card voltage: 3.3V\n");
		break;
	default:
		shell_print(sh,"Card voltage is not known value\n");
	}
	if (sdio_card.status != CARD_INITIALIZED) {
		shell_print(sh,"Card status is not OK\n");
	}
	switch (sdio_card.card_speed) {
	case SD_TIMING_SDR12:
		shell_print(sh,"Card timing: SDR12\n");
		break;
	case SD_TIMING_SDR25:
		shell_print(sh,"Card timing: SDR25\n");
		break;
	case SD_TIMING_SDR50:
		shell_print(sh,"Card timing: SDR50\n");
		break;
	case SD_TIMING_SDR104:
		shell_print(sh,"Card timing: SDR104\n");
		break;
	case SD_TIMING_DDR50:
		shell_print(sh,"Card timing: DDR50\n");
		break;
	default:
		shell_print(sh,"Card timing is not known value\n");
	}
	switch (sdio_card.type) {
	case CARD_SDIO:
		shell_print(sh,"Card type: SDIO\n");
		break;
	case CARD_SDMMC:
		shell_print(sh,"Card type: SDMMC\n");
		break;
	case CARD_COMBO:
		shell_print(sh,"Card type: combo card\n");
		break;
	default:
		shell_print(sh,"Card type is not known value\n");
	}
	if (sdio_card.sd_version >= SD_SPEC_VER3_0) {
		shell_print(sh,"Card spec: 3.0\n");
	} else if (sdio_card.sd_version >= SD_SPEC_VER2_0) {
		shell_print(sh,"Card spec: 2.0\n");
	} else if (sdio_card.sd_version >= SD_SPEC_VER1_1) {
		shell_print(sh,"Card spec: 1.1\n");
	} else if (sdio_card.sd_version >= SD_SPEC_VER1_0) {
		shell_print(sh,"Card spec: 1.0\n");
	} else {
		shell_print(sh,"Card spec is unknown value\n");
	}

    sdio_ready = true;
    shell_print(sh, "SDIO initialization successful");
    return 0;
#else
    shell_error(sh, "SDIO support not available in device tree");
    return -ENOTSUP;
#endif
}

static int cmd_sdio_test(const struct shell *sh, size_t argc, char **argv) {

    int ret = -EAGAIN;
    uint8_t reg = 0xFF;
    int i;

    if (!sdio_ready) {
        shell_error(sh, "SDIO interface not initialized. Please run 'sdio init' first.");
        return ret;
    }

#if WIFI_SEQ_TEST

    shell_print(sh, "SDIO WiFi Seq Check\n");

    ret = sdio_write_byte(&sdio_card.func0, 0x2, (uint8_t)(2));
    if (ret) {
        shell_print(sh,"SDIO 0x2 write failed 2 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x2 write successful %d\n", 2);
    }

    ret = sdio_read_byte(&sdio_card.func0, 0x2, &reg);
    if (ret) {
        shell_print(sh,"SDIO 0x2 read failed ret=%d reg=%02X\n", ret, reg);
    } else {
        shell_print(sh,"SDIO 0x2 read successful %d\n", reg);
    }

    ret = sdio_write_byte(&sdio_card.func0, 0x10, (uint8_t)(64));
    if (ret) {
        shell_print(sh,"SDIO 0x10 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x10 write successful %d\n", 64);
    }

    ret = sdio_read_byte(&sdio_card.func0, 0x10, &reg);
    if (ret) {
        shell_print(sh,"SDIO 0x10 read failed ret=%d reg=%02X\n", ret, reg);
    } else {
        shell_print(sh,"SDIO 0x10 read successful %d\n", reg);
    }

    ret = sdio_write_byte(&sdio_card.func0, 0x10, (uint8_t)(64));
    if (ret) {
        shell_print(sh,"SDIO 0x10 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x10 write successful %d\n", 64);
    }

    ret = sdio_write_byte(&sdio_card.func0, 0x110, (uint8_t)(64));
    if (ret) {
        shell_print(sh,"SDIO 0x110 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x110 write successful %d\n", 64);
    }

    ret = sdio_write_byte(&sdio_card.func0, 0x210, (uint8_t)(0));
    if (ret) {
        shell_print(sh,"SDIO 0x210 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x210 write successful %d\n", 0);
    }

    ret = sdio_write_byte(&sdio_card.func0, 0x211, (uint8_t)(1));
    if (ret) {
        shell_print(sh,"SDIO 0x211 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x211 write successful %d\n", 1);
    }

    ret = sdio_write_byte(&sdio_card.func0, 0x4, (uint8_t)(7));
    if (ret) {
        shell_print(sh,"SDIO 0x4 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x4 write successful %d\n", 7);
    }

    ret = sdio_read_byte(&sdio_card.func0, 0x13, &reg);
    if (ret) {
        shell_print(sh,"SDIO 0x13 read failed ret=%d reg=%02X\n", ret, reg);
    } else {
        shell_print(sh,"SDIO 0x13 read successful %d\n", reg);
    }

    ret = sdio_write_byte(&sdio_card.func0, 0x13, (uint8_t)(3));
    if (ret) {
        shell_print(sh,"SDIO 0x13 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x13 write successful %d\n", 3);
    }

    ret = sdio_read_byte(&sdio_card.func0, 0x3, &reg);
    if (ret) {
        shell_print(sh,"SDIO 0x3 read failed ret=%d reg=%02X\n", ret, reg);
    } else {
        shell_print(sh,"SDIO 0x3 read successful %d\n", reg);
    }

    ret = sdio_write_byte(&sdio_func1, 0x1000E, (uint8_t)(41));
    if (ret) {
        shell_print(sh,"SDIO 0x1000E write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000E write successful %d\n", 41);
    }

    ret = sdio_read_byte(&sdio_func1, 0x1000E, &reg);
    if (ret) {
        shell_print(sh,"SDIO 0x1000E read failed ret=%d reg=%02X\n", ret, reg);
    } else {
        shell_print(sh,"SDIO 0x1000E read successful %d\n", reg);
    }

    ret = sdio_write_byte(&sdio_func1, 0x1000E, (uint8_t)(0));
    if (ret) {
        shell_print(sh,"SDIO 0x1000E write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000E write successful %d\n", 0);
    }

    ret = sdio_read_byte(&sdio_func1, 0x1000F, &reg);
    if (ret) {
        shell_print(sh,"SDIO 0x1000F read failed ret=%d reg=%02X\n", ret, reg);
    } else {
        shell_print(sh,"SDIO 0x1000F read successful %d\n", reg);
    }

    ret = sdio_write_byte(&sdio_card.func0, 0x2, (uint8_t)(6));
    if (ret) {
        shell_print(sh,"SDIO 0x2 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x2 write successful %d\n", 6);
    }

    ret = sdio_write_byte(&sdio_card.func0, 0xF2, (uint8_t)(7));
    if (ret) {
        shell_print(sh,"SDIO 0xF2 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0xF2 write successful %d\n", 7);
    }

    ret = sdio_write_byte(&sdio_func1, 0x1000C, (uint8_t)(24));
    if (ret) {
        shell_print(sh,"SDIO 0x1000C write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000C write successful %d\n", 24);
    }

    ret = sdio_write_byte(&sdio_func1, 0x4040, (uint8_t)(4));
    if (ret) {
        shell_print(sh,"SDIO 0x4040 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x4040 write successful %d\n", 4);
    }

    ret = sdio_write_byte(&sdio_card.func0, 0x4, (uint8_t)(5));
    if (ret) {
        shell_print(sh,"SDIO 0x4 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x4 write successful %d\n", 5);
    }

    ret = sdio_read_byte(&sdio_card.func0, 0x3, &reg);
    if (ret) {
        shell_print(sh,"SDIO 0x3 read failed ret=%d reg=%02X\n", ret, reg);
    } else {
        shell_print(sh,"SDIO 0x3 read successful %d\n", reg);
    }

    ret = sdio_read_addr(&sdio_func1, 0x8000, buffer, 4);

    if (!ret) {
        shell_print(sh,"SDIO read fifo successful first 4 bytes %02X%02X%02X%02X\n",
               buffer[0], buffer[1], buffer[2], buffer[3]);
    } else {
        shell_print(sh,"SDIO read fifo failed\n");
    }

    shell_print(sh, "SDIO WiFi Seq Check End\n");

#else

    shell_print(sh, "SDIO Seq Check\n");

    ret = sdio_write_byte(&sdio_card.func0, 0x10, 64);
    if (ret) {
        shell_print(sh,"SDIO 0x10 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x10 write successful %d\n", 64);
    }

	ret = sdio_write_byte(&sdio_card.func0, 0x110, 64);
	if (ret) {
        shell_print(sh,"SDIO 0x110 write failed 64 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x110 write successful %d\n", 64);
    }

	ret = sdio_read_byte(&sdio_card.func0, 0x110, &reg);
	if (ret) {
        shell_print(sh,"SDIO 0x110 read failed ret=%d reg=%02X\n", ret, reg);
    } else {
        shell_print(sh,"SDIO 0x110 read successful %d\n", reg);
    }

	ret = sdio_write_byte(&sdio_func1, 0x1000a, 0x0); // (0x170000 >> 8) & 0xff
	if (ret) {
        shell_print(sh,"SDIO 0x1000a write failed 0x0 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000a write successful %d\n", 0x0);
    }

	ret = sdio_write_byte(&sdio_func1, 0x1000b, 0x1b); // (0x170000 >> 16) & 0xff
	if (ret) {
        shell_print(sh,"SDIO 0x1000b write failed 0x1b ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000b write successful %d\n", 0x1b);
    }

	ret = sdio_write_byte(&sdio_func1, 0x1000c, 0x0); // (0x170000 >> 24) & 0xff
	if (ret) {
        shell_print(sh,"SDIO 0x1000c write failed 0x0 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000c write successful %d\n", 0x0);
    }

	for (i = 0; i < 64; i++) {
		buffer[i] = i;
	}

	for (i = 0; i < 10; i++) {
		ret = sdio_write_addr(&sdio_func1, 0x0, buffer, 64);
		if (ret) {
           shell_print(sh,"SDIO 0x0 write failed for 64 bytes ret=%d\n", ret);
        } else {
            shell_print(sh,"SDIO 0x0 write successful bytes=%d\n", 64);
        }
	}

	for (i = 0; i < 64; i++) {
		buffer[i] = 0;
	}

	ret = sdio_read_addr(&sdio_func1, 0x0, buffer, 64);
	if (ret) {
        shell_print(sh,"SDIO 0x0 read failed for 64 bytes ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x0 read successful bytes=%d\n", 64);
    }

	shell_print(sh,"buffer content 0x%x 0x%x 0x%x 0x%x\n", buffer[0], buffer[1], buffer[2], buffer[3]);

	for (i = 0; i < 16; i++) {
		if (buffer[i] != i) {
			shell_print(sh,"CMD53: read/write mismatch\n");
			break;
		}
	}

	ret = sdio_write_addr(&sdio_func1, 0x0, buffer, 128);
	if (ret) {
        shell_print(sh,"SDIO 0x0 write failed for 128 bytes ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x0 write successful bytes=%d\n", 128);
    }

    for (i = 0; i < 128; i++) {
		buffer[i] = 0;
	}

	ret = sdio_read_addr(&sdio_func1, 0x0, buffer, 128);
	if (ret) {
        shell_print(sh,"SDIO 0x0 read failed for 128 bytes ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x0 read successful bytes=%d\n", 128);
    }

	shell_print(sh,"buffer content 0x%x 0x%x 0x%x 0x%x\n", buffer[0], buffer[1], buffer[2], buffer[3]);

	ret = sdio_write_byte(&sdio_func1, 0x1000b, 0x10);
	if (ret) {
        shell_print(sh,"SDIO 0x1000b write failed 0x10 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000b write successful %d\n", 0x10);
    }

	ret = sdio_write_byte(&sdio_func1, 0x1000c, 0x18);
	if (ret) {
        shell_print(sh,"SDIO 0x1000c write failed 0x18 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000c write successful %d\n", 0x18);
    }

	ret = sdio_write_byte(&sdio_func1, 0x10008, 0x60);
    if (ret) {
        shell_print(sh,"SDIO 0x10008 write failed 0x60 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x10008 write successful %d\n", 0x60);
    }

	ret = sdio_write_byte(&sdio_func1, 0x10009, 0x10);
    if (ret) {
        shell_print(sh,"SDIO 0x10009 write failed 0x10 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x10009 write successful %d\n", 0x10);
    }

	ret = sdio_write_byte(&sdio_func1, 0x1001d, 0xd0);
    if (ret) {
        shell_print(sh,"SDIO 0x1001d write failed 0xd0 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1001d write successful %d\n", 0xd0);
    }

	ret = sdio_write_byte(&sdio_func1, 0x1000e, 0x0);
    if (ret) {
        shell_print(sh,"SDIO 0x1000e write failed 0x0 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000e write successful %d\n", 0x0);
    }

	ret = sdio_write_byte(&sdio_func1, 0x1000e, 0x10);
	if (ret) {
        shell_print(sh,"SDIO 0x1000e write failed 0x10 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000e write successful %d\n", 0x10);
    }

	i = 0;
	reg = 0;
	while (((reg & 0x80) == 0) && (i < 10)) {
		ret = sdio_read_byte(&sdio_func1, 0x1000e, &reg);
		if (ret) {
           shell_print(sh,"SDIO 0x1000e read failed ret=%d reg=%02X\n", ret, reg);
        } else {
           shell_print(sh,"SDIO 0x1000e read successful %d\n", reg);
        }
		i++;
		k_sleep(K_MSEC(100));
	}

	ret = sdio_write_byte(&sdio_func1, 0x1000e, 0xd2);
	if (ret) {
        shell_print(sh,"SDIO 0x1000e write failed 0xd2 ret=%d\n", ret);
    } else {
        shell_print(sh,"SDIO 0x1000e write successful %d\n", 0xd2);
    }

	ret = sdio_read_byte(&sdio_func1, 0x1000e, &reg);
	if (ret) {
        shell_print(sh,"SDIO 0x1000e read failed ret=%d reg=%02X\n", ret, reg);
    } else {
        shell_print(sh,"SDIO 0x1000e read successful %d\n", reg);
    }

    shell_print(sh, "SDIO Seq Check End\n");

#endif

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sdio,
    SHELL_CMD(init, NULL, "Initialize SDIO interface", cmd_sdio_init),
    SHELL_CMD(test, NULL, "SDIO Sequence test", cmd_sdio_test),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sdio, &sub_sdio, "SDIO commands", NULL);