/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/sys/printk.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/pinctrl.h>

#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define USER_STACKSIZE	2048

#if defined(CONFIG_FILE_SYSTEM_LITTLEFS)

#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/littlefs.h>

#define MKFS_FS_TYPE FS_LITTLEFS
#define MKFS_DEV_ID FIXED_PARTITION_ID(storage_partition)
#define MKFS_FLAGS 0

#elif defined(CONFIG_FAT_FILESYSTEM_ELM)

#include <zephyr/storage/disk_access.h>
#include <ff.h>

#define MKFS_FS_TYPE FS_FATFS
#define MKFS_DEV_ID "RAM:"
#define MKFS_FLAGS 0

#else
#error "No filesystem specified."
#endif

#ifndef CONFIG_USERSPACE
#error This sample requires CONFIG_USERSPACE.
#endif

struct k_thread user_thread;
K_THREAD_STACK_DEFINE(user_stack, USER_STACKSIZE);

static void user_function(void *p1, void *p2, void *p3)
{
	printf("Hello World from %s (%s)\n",
	       k_is_user_context() ? "UserSpace!" : "privileged mode.",
	       CONFIG_BOARD_TARGET);
	__ASSERT(k_is_user_context(), "User mode execution was expected");
}

#define GPIO2_NODE DT_NODELABEL(gpio2)
#define SLOWCLK_TIMER_NODE DT_NODELABEL(timer_slowclk)
#define CLOCK_NODE DT_CLOCKS_CTLR(SLOWCLK_TIMER_NODE)
#define I2C0_NODE DT_NODELABEL(i2c0)
#define SPI_NODE DT_NODELABEL(spi0)
#define XSPI_NODE DT_NODELABEL(xspi)
#define TIMER0_NODE DT_NODELABEL(timer0)
#define WATCHDOG0_NODE DT_NODELABEL(watchdog0)
#define SLOWCLK_WATCHDOG_NODE DT_NODELABEL(watchdog_slowclk)

#define GPIO2_MCU_GPIO20_PIN 4 /* MCU_GPIO20 (output toggle test) */
#define GPIO2_MCU_GPIO21_PIN 5 /* MCU_GPIO21 (input interrupt test) */

#define I2C_SABRE_SLAVE_ADDR 0x42
#define FLASH_TEST_ERASE_SIZE DT_PROP(XSPI_NODE, erase_block_size)
#define FLASH_TEST_OFFSET (0)
#define FLASH_TEST_PATTERN_SIZE 32

PINCTRL_DT_DEV_CONFIG_DECLARE(SPI_NODE);

static void slowclk_timer_callback(const struct device *dev, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

	printf("Received slowclock Timer callback.\n");
}

static void timer_callback(const struct device *dev, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

	printf("Received Timer callback.\n");
}

static void wdg_cb_warn(const struct device *dev, int channel_id)
{
	printf("Watchdog stage 0 callback (warning)!\n");
}

static void wdg_cb_expire(const struct device *dev, int channel_id)
{
	printf("Watchdog stage 1 callback (expire)!\n");
}

static int slowclk_timer_test(const struct shell *shell, size_t argc, char **argv)
{
	// Enable lfrcosc for slowclock peripherals
    *(volatile uint32_t*)(0x5802430C) = 1;

    const struct device *timer_dev = DEVICE_DT_GET(SLOWCLK_TIMER_NODE);
	if (!device_is_ready(timer_dev)) {
		shell_print(shell, "Timer device not ready");
		return -1;
	}

	uint32_t freq = DT_PROP(CLOCK_NODE, clock_frequency);
	shell_print(shell, "Slowclock timer clock frequency = %u Hz", freq);

	struct counter_top_cfg top_cfg = {
		.callback = slowclk_timer_callback,
		.user_data = NULL,
		.ticks = counter_us_to_ticks(timer_dev, 1000000),
		.flags = 0,
	};

	int err = counter_start(timer_dev);
	if (err) {
		shell_print(shell, "Failed to start counter: %d", err);
		return err;
	}

	shell_print(shell, "Counter started, waiting for timeout...");

	err = counter_set_top_value(timer_dev, &top_cfg);
	if (err) {
		shell_print(shell, "Failed to set top value: %d", err);
		return err;
	}

	k_msleep(1500);

	err = counter_stop(timer_dev);
	if (err) {
		shell_print(shell, "Failed to stop counter: %d", err);
		return err;
	}

	shell_print(shell, "Counter stopped.");
    return 0;
}

static struct gpio_callback gpio_input_test_cb_data;
static volatile uint32_t gpio_input_test_irq_count;

static void gpio_input_test_isr(const struct device *dev, struct gpio_callback *cb,
				 uint32_t pins)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(dev);

	gpio_input_test_irq_count = 1;
}

static int gpio_output_test(const struct shell *shell)
{
	const struct device *gpio_dev;
	int ret;

	shell_print(shell, "GPIO output toggle test start");

	gpio_dev = DEVICE_DT_GET(GPIO2_NODE);
	if (!device_is_ready(gpio_dev)) {
		shell_print(shell, "Error: GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure(gpio_dev, GPIO2_MCU_GPIO20_PIN, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		shell_print(shell, "Error configuring GPIO: %d", ret);
		return ret;
	}

	gpio_pin_set(gpio_dev, GPIO2_MCU_GPIO20_PIN, 1);
	k_msleep(3000);
	gpio_pin_set(gpio_dev, GPIO2_MCU_GPIO20_PIN, 0);

	shell_print(shell, "GPIO output test done");
	return 0;
}

static int gpio_input_test(const struct shell *shell)
{
	const struct device *gpio_dev;
	int ret;

	shell_print(shell, "GPIO input interrupt test start");

	gpio_dev = DEVICE_DT_GET(GPIO2_NODE);
	if (!device_is_ready(gpio_dev)) {
		shell_print(shell, "Error: GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure(gpio_dev, GPIO2_MCU_GPIO21_PIN, GPIO_INPUT);
	if (ret < 0) {
		shell_print(shell, "Error configuring GPIO as input: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure(gpio_dev, GPIO2_MCU_GPIO21_PIN, GPIO_INT_EDGE_FALLING);
	if (ret < 0) {
		shell_print(shell, "Error configuring GPIO interrupt: %d", ret);
		return ret;
	}

	gpio_input_test_irq_count = 0;
	gpio_init_callback(&gpio_input_test_cb_data, gpio_input_test_isr,
			   BIT(GPIO2_MCU_GPIO21_PIN));
	gpio_add_callback(gpio_dev, &gpio_input_test_cb_data);

	shell_print(shell, "Waiting for a falling edge on GPIO...");

	while (gpio_input_test_irq_count == 0) {
		k_msleep(10);
	}

	gpio_remove_callback(gpio_dev, &gpio_input_test_cb_data);
	gpio_pin_interrupt_configure(gpio_dev, GPIO2_MCU_GPIO21_PIN, GPIO_INT_DISABLE);

	shell_print(shell, "GPIO falling edge detected.\nGPIO input test done");
	return 0;
}

static int gpio_test(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(shell, "Usage: gpio_test <out|in>");
		shell_print(shell, "  out - output gpio toggle test");
		shell_print(shell, "  in  - input gpio interrupt test");
		return -EINVAL;
	}

	if (strcmp(argv[1], "out") == 0) {
		return gpio_output_test(shell);
	} else if (strcmp(argv[1], "in") == 0) {
		return gpio_input_test(shell);
	}

	shell_print(shell, "Invalid option '%s', use out or in", argv[1]);
	return -EINVAL;
}

static int i2c_master_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *i2c_dev = DEVICE_DT_GET(I2C0_NODE);

    if (!device_is_ready(i2c_dev)) {
        printk("I2C device not ready\n");
        return -1;
    }

    printk("I2C device ready, reading version...\n");

    uint8_t data_tx[4] = {0x5b, 0x5a, 0x33, 0x0a};

    int ret = i2c_write(i2c_dev, data_tx, sizeof(data_tx), I2C_SABRE_SLAVE_ADDR);
    if (ret) {
        printk("I2C write failed: %d\n", ret);
        return -1;
    }

    uint8_t data_rx[8] = {0};
    ret = i2c_read(i2c_dev, data_rx, sizeof(data_rx), I2C_SABRE_SLAVE_ADDR);
    if (ret) {
        printk("I2C read failed: %d\n", ret);
		return -1;
    } else {
        printk("version = %x %x %x %x\n", data_rx[4], data_rx[5], data_rx[6], data_rx[7]);
    }
	return 0;
}

static const struct spi_config spi_cfg = {
	.frequency = 1000000,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
	.slave = 0,
};

static int spi_master_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *spi_dev = DEVICE_DT_GET(SPI_NODE);
	const struct pinctrl_dev_config *spi_pcfg = PINCTRL_DT_DEV_CONFIG_GET(SPI_NODE);
	int ret;

	if (!device_is_ready(spi_dev)) {
		printk("SPI device not ready!\n");
		return -1;
	}

	ret = pinctrl_apply_state(spi_pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		printk("SPI pinctrl apply failed: %d\n", ret);
		return ret;
	}

	uint8_t tx_buf[12] = {0};
	uint8_t rx_buf[12] = {0};

	struct spi_buf tx_spi_buf = {
		.buf = tx_buf,
		.len = 0,
	};
	struct spi_buf_set tx_set = {
		.buffers = &tx_spi_buf,
		.count = 1,
	};

	struct spi_buf rx_spi_buf = {
		.buf = rx_buf,
		.len = 0,
	};
	struct spi_buf_set rx_set = {
		.buffers = &rx_spi_buf,
		.count = 1,
	};

	tx_buf[0] = 0x5B;
	tx_buf[1] = 0x5A;
	tx_buf[2] = 0x33;
	tx_buf[3] = 0x0A;

	tx_spi_buf.len = sizeof(tx_buf);
	rx_spi_buf.len = sizeof(rx_buf);

	ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
	if (ret < 0) {
		printk("SPI transfer failed: %d\n", ret);
		return -1;
	}

	printk("Command sent OK.\n");
	printk("version = %x, %x, %x, %x\n", rx_buf[8], rx_buf[9], rx_buf[10], rx_buf[11]);

	return 0;
}

static int timer_test(const struct shell *shell, size_t argc, char **argv)
{
    const struct device *timer_dev = DEVICE_DT_GET(TIMER0_NODE);
	if (!device_is_ready(timer_dev)) {
		shell_print(shell, "Timer device not ready");
		return -1;
	}

	struct counter_top_cfg top_cfg = {
		.callback = timer_callback,
		.user_data = NULL,
		.ticks = counter_us_to_ticks(timer_dev, 1000000),
		.flags = 0,
	};

	int err = counter_start(timer_dev);
	if (err) {
		shell_print(shell, "Failed to start counter: %d", err);
		return err;
	}

	shell_print(shell, "Counter started, waiting for timeout...");

	err = counter_set_top_value(timer_dev, &top_cfg);
	if (err) {
		shell_print(shell, "Failed to set top value: %d", err);
		return err;
	}

	k_msleep(1500);

	err = counter_stop(timer_dev);
	if (err) {
		shell_print(shell, "Failed to stop counter: %d", err);
		return err;
	}

	shell_print(shell, "Counter stopped.");
    return 0;
}

static int watchdog_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *wdt_dev = DEVICE_DT_GET(WATCHDOG0_NODE);

	if (!device_is_ready(wdt_dev)) {
		shell_print(shell, "WDT device not ready.");
		return -1;
	}

	struct wdt_timeout_cfg stage1 = {
		.callback = wdg_cb_expire,
		.next = NULL,
	};

	struct wdt_timeout_cfg stage0 = {
		.window = { .max = 1000 },	// timeout in ms
		.flags = WDT_FLAG_RESET_NONE,
		// .flags = WDT_FLAG_RESET_SOC,	// To test soc reset on watchdog expiry
		.callback = wdg_cb_warn,
		.next = &stage1,
	};

	wdt_install_timeout(wdt_dev, &stage0);
	wdt_setup(wdt_dev, 0);

	shell_print(shell, "Watchdog started, waiting for timeout...");

	k_msleep(1500);
	wdt_feed(wdt_dev, 0);

	k_msleep(5000);
	wdt_disable(wdt_dev);

	shell_print(shell, "Watchdog stopped.");

	return 0;
}

static int slowclk_watchdog_test(const struct shell *shell, size_t argc, char **argv)
{
	// Enable lfrcosc for slowclock peripherals
    *(volatile uint32_t*)(0x5802430C) = 1;

	const struct device *wdt_slowclk_dev = DEVICE_DT_GET(SLOWCLK_WATCHDOG_NODE);

	if (!device_is_ready(wdt_slowclk_dev)) {
		shell_print(shell, "Slow clock WDT device not ready.");
		return -1;
	}

	struct wdt_timeout_cfg stage1 = {
		.callback = wdg_cb_expire,
		.next = NULL,
	};

	struct wdt_timeout_cfg stage0 = {
		.window = { .max = 1000 },	// timeout in ms
		.flags = WDT_FLAG_RESET_NONE,
		// .flags = WDT_FLAG_RESET_SOC,	// To test soc reset on watchdog expiry
		.callback = wdg_cb_warn,
		.next = &stage1,
	};

	wdt_install_timeout(wdt_slowclk_dev, &stage0);
	wdt_setup(wdt_slowclk_dev, 0);

	shell_print(shell, "Slow clock Watchdog started, waiting for timeout...");

	k_msleep(1500);
	wdt_feed(wdt_slowclk_dev, 0);

	k_msleep(5000);
	wdt_disable(wdt_slowclk_dev);

	shell_print(shell, "Slow clock Watchdog stopped.");

	return 0;
}

static int flash_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *flash_dev = DEVICE_DT_GET(XSPI_NODE);
	uint8_t expected[FLASH_TEST_PATTERN_SIZE];
	uint8_t actual[FLASH_TEST_PATTERN_SIZE];
	int ret;

	if (!device_is_ready(flash_dev)) {
		shell_print(shell, "Flash device not ready.");
		return -ENODEV;
	}

	for (size_t i = 0; i < sizeof(expected); i++) {
		expected[i] = (uint8_t)(0xA5 ^ i);
	}

	shell_print(shell, "Erasing flash offset 0x%lx size 0x%x",
		    (unsigned long)FLASH_TEST_OFFSET, FLASH_TEST_ERASE_SIZE);

	ret = flash_erase(flash_dev, FLASH_TEST_OFFSET, FLASH_TEST_ERASE_SIZE);
	if (ret) {
		shell_print(shell, "Flash erase failed: %d", ret);
		return ret;
	}

	ret = flash_write(flash_dev, FLASH_TEST_OFFSET, expected, sizeof(expected));
	if (ret) {
		shell_print(shell, "Flash write failed: %d", ret);
		return ret;
	}

	memset(actual, 0, sizeof(actual));
	ret = flash_read(flash_dev, FLASH_TEST_OFFSET, actual, sizeof(actual));
	if (ret) {
		shell_print(shell, "Flash read failed: %d", ret);
		return ret;
	}

	if (memcmp(expected, actual, sizeof(expected)) != 0) {
		shell_print(shell, "Flash verify failed.");
		return -EIO;
	}

	shell_print(shell, "Flash write/read test passed.");
	return 0;
}

static FATFS fat_fs;
static struct fs_mount_t memfs_mnt = {
	.type = FS_FATFS,
	.mnt_point = "/RAM:",
	.fs_data = &fat_fs,
};

static int ramfs_test(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ret = fs_mount(&memfs_mnt);
	if (ret < 0) {
		printk("MEMFS mount failed: %d\n", ret);
		return ret;
	}

	printk("MEMFS mounted at /RAM:\n");

	struct fs_file_t file;
	fs_file_t_init(&file);

	ret = fs_open(&file, "/RAM:/test.txt",
		      FS_O_CREATE | FS_O_WRITE | FS_O_READ);
	if (ret < 0) {
		printk("open failed: %d\n", ret);
		return ret;
	}

	fs_write(&file, "hello ramfs\n", 12);
	fs_seek(&file, 0, FS_SEEK_SET);

	char buf[32] = {0};
	fs_read(&file, buf, sizeof(buf) - 1);

	printk("read: %s\n", buf);

	fs_close(&file);

	return 0;
}

static int trng_test(const struct shell *sh,
			 size_t argc,
			 char **argv)
{
	const struct device *dev;
	uint32_t buf[12];
	int ret;
	size_t words = 1;

	if (argc == 2) {
		words = strtoul(argv[1], NULL, 0);

		if (words == 0) {
			words = 1;
		}

		if (words > ARRAY_SIZE(buf)) {
			words = ARRAY_SIZE(buf);
		}
	}

	dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));

	if (!device_is_ready(dev)) {
		shell_error(sh, "Entropy device not ready");
		return -ENODEV;
	}

	ret = entropy_get_entropy(dev,
				  (uint8_t *)buf,
				  words * sizeof(uint32_t));

	if (ret) {
		shell_error(sh, "entropy_get_entropy() failed (%d)", ret);
		return ret;
	}

	shell_print(sh, "Generated %u words:", words);

	for (size_t i = 0; i < words; i++) {
		shell_print(sh, "[%02u] 0x%08x", i, buf[i]);
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(tests,
	SHELL_CMD(gpio_test, NULL, "Test GPIO: gpio_test <out|in>", gpio_test),
	SHELL_CMD(i2c_master_test, NULL, "Test i2c master", i2c_master_test),
	SHELL_CMD(spi_master_test, NULL, "Test spi master", spi_master_test),
	SHELL_CMD(timer_test, NULL, "Test timer callback", timer_test),
	SHELL_CMD(slowclk_timer_test, NULL, "Test slow clock timer callback", slowclk_timer_test),
	SHELL_CMD(watchdog_test, NULL, "Test watchdog callback", watchdog_test),
	SHELL_CMD(slowclk_watchdog_test, NULL, "Test slow clock watchdog callback", slowclk_watchdog_test),
	SHELL_CMD(flash_test, NULL, "Test flash erase, write, and read", flash_test),
	SHELL_CMD(ramfs_test, NULL, "Test RAM filesystem", ramfs_test),
	SHELL_CMD(trng_test, NULL, "Test trng", trng_test),
	SHELL_SUBCMD_SET_END /* Array terminator */
);

SHELL_CMD_REGISTER(tests, &tests, "srw1500 peripheral tests", NULL);

int main(void)
{
	k_thread_create(&user_thread, user_stack, USER_STACKSIZE,
			user_function, NULL, NULL, NULL,
			-1, K_USER, K_MSEC(0));

	return 0;
}
