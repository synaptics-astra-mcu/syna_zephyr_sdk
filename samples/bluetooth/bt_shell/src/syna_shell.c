/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <stdlib.h>

/*
 * AON POR mask register (0x50350038):
 *   Clearing bit 11 (AON_POR_RST_EVENT_SW_RESET_MASK) allows SW_RESET
 *   event to trigger a full SoC POR reset.
 */
#define SR100_AON_POR_MASK         0x50350038UL
#define AON_POR_SW_RESET_BIT       11

/* Global reset trigger (0x50330600): write 1 to trigger immediate SoC reset */
#define SR100_GLOBAL_RST_TRIGGER   0x50330600UL

/*
 * AON memory BCM_AON_GP_SW_Cache (0x50352030):
 *   If this equals BCM_SPK_FAIL_MAGIC after reset, the APBL skips flash
 *   boot and enters External Host (ROM download) mode.
 */
#define SR100_BCM_AON_GP_SW_CACHE  0x50352030UL
#define BCM_SPK_FAIL_MAGIC         0x6662666FUL

/*
 * On boot, clear the BCM_AON_GP_SW_Cache flag so normal reboots do NOT
 * accidentally re-enter ROM download mode.
 */
static int clear_bcm_aon_cache(void)
{
	sys_write32(0U, SR100_BCM_AON_GP_SW_CACHE);
	return 0;
}
SYS_INIT(clear_bcm_aon_cache, PRE_KERNEL_1, 0);

/* ---- reboot ---- */

static int cmd_reboot(const struct shell *sh, size_t argc, char *argv[])
{
	shell_print(sh, "Rebooting...");
	k_sleep(K_MSEC(100)); /* flush shell output before reboot */
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

SHELL_CMD_REGISTER(reboot, NULL, "Reboot the device", cmd_reboot);

/* ---- reboot_rom ---- */

static int cmd_reboot_rom(const struct shell *sh, size_t argc, char *argv[])
{
	shell_print(sh, "Rebooting to ROM download mode...");
	k_sleep(K_MSEC(100));

	/* Signal APBL to enter External Host (download) mode after reset */
	sys_write32(BCM_SPK_FAIL_MAGIC, SR100_BCM_AON_GP_SW_CACHE);

	barrier_dmem_fence_full();

	/* Trigger chip reset */
	sys_write32(1U, SR100_GLOBAL_RST_TRIGGER);

	while (1) {
	}

	CODE_UNREACHABLE;
	return 0;
}

SHELL_CMD_REGISTER(reboot_rom, NULL,
	"Reboot to ROM download mode (SoC POR reset)", cmd_reboot_rom);

/* ---- build_info ---- */

static int cmd_build_info(const struct shell *sh, size_t argc, char *argv[])
{
	shell_print(sh, "Build: %s %s", __DATE__, __TIME__);
	return 0;
}

SHELL_CMD_REGISTER(build_info, NULL, "Show firmware build date/time", cmd_build_info);

/* ---- GPIO control ---- */

static const struct device *const gpioa_dev = DEVICE_DT_GET(DT_NODELABEL(gpioa));

/*
 * gpio <pin> <0|1>  — configure gpioa pin as output and drive low/high
 * gpio <pin>        — configure gpioa pin as input and read current value
 *
 * Examples:
 *   gpio 25 1   -> drive GPIO25 high
 *   gpio 25 0   -> drive GPIO25 low
 *   gpio 25     -> read GPIO25
 */
static int cmd_gpio_ctrl(const struct shell *sh, size_t argc, char *argv[])
{
	if (!device_is_ready(gpioa_dev)) {
		shell_error(sh, "gpioa device not ready");
		return -ENODEV;
	}

	char *end;
	long pin = strtol(argv[1], &end, 10);

	if (*end != '\0' || pin < 0 || pin > 31) {
		shell_error(sh, "Invalid pin: %s (0-31)", argv[1]);
		return -EINVAL;
	}

	/* Read mode: gpio <pin> */
	if (argc == 2) {
		int ret = gpio_pin_configure(gpioa_dev, (gpio_pin_t)pin, GPIO_INPUT);

		if (ret < 0) {
			shell_error(sh, "Configure GPIO%ld as input failed: %d", pin, ret);
			return ret;
		}
		int val = gpio_pin_get(gpioa_dev, (gpio_pin_t)pin);

		if (val < 0) {
			shell_error(sh, "Read GPIO%ld failed: %d", pin, val);
			return val;
		}
		shell_print(sh, "GPIO%ld = %d", pin, val);
		return 0;
	}

	/* Write mode: gpio <pin> <0|1> */
	long val = strtol(argv[2], &end, 10);

	if (*end != '\0' || (val != 0 && val != 1)) {
		shell_error(sh, "Invalid value: %s (use 0 or 1)", argv[2]);
		return -EINVAL;
	}

	int ret = gpio_pin_configure(gpioa_dev, (gpio_pin_t)pin, GPIO_OUTPUT);

	if (ret < 0) {
		shell_error(sh, "Configure GPIO%ld as output failed: %d", pin, ret);
		return ret;
	}

	ret = gpio_pin_set(gpioa_dev, (gpio_pin_t)pin, (int)val);
	if (ret < 0) {
		shell_error(sh, "Set GPIO%ld failed: %d", pin, ret);
		return ret;
	}

	shell_print(sh, "GPIO%ld -> %ld", pin, val);
	return 0;
}

SHELL_CMD_ARG_REGISTER(gpio, NULL,
	"Control/read gpioa pin\n"
	"  gpio <pin> <0|1>  set output\n"
	"  gpio <pin>        read input",
	cmd_gpio_ctrl, 2, 1);
