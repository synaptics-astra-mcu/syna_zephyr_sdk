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
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/sdio_device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/pinctrl/syna-srw1500-pinctrl.h>
#include <zephyr/sys/util.h>
#include "soc_regs.h"
#include "cache/cache_benchmark.h"
#include "npu_test.h"

#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define USER_STACKSIZE	2048

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
#include <zephyr/usb/usbd.h>

#define USB_VID 0x06CB
#define USB_PID 0x0066
#define USB_BCD 0x0200
#define CONFIG_USB_DEVICE_SN "26.09.20.01"

USBD_DEVICE_DEFINE(srw1500_usbd,
	DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), USB_VID, USB_PID);
USBD_DESC_LANG_DEFINE(srw1500_usb_lang);
USBD_DESC_MANUFACTURER_DEFINE(srw1500_usb_manufacturer, "Srw1500 Manufacturer");
USBD_DESC_PRODUCT_DEFINE(srw1500_usb_product, "Srw1500");
USBD_DESC_STRING_DEFINE(srw1500_usb_sn, CONFIG_USB_DEVICE_SN,
	USBD_DUT_STRING_SERIAL_NUMBER);
USBD_DESC_CONFIG_DEFINE(srw1500_usb_hs_desc, "High-speed configuration");
USBD_CONFIGURATION_DEFINE(srw1500_usb_hs_config, 0, 125, &srw1500_usb_hs_desc);

#endif

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

#define GPIO3_NODE DT_NODELABEL(gpio3)
#define SLOWCLK_TIMER_NODE DT_NODELABEL(timer_slowclk)
#define CLOCK_NODE DT_CLOCKS_CTLR(SLOWCLK_TIMER_NODE)
#define I2C0_NODE DT_NODELABEL(i2c0)
#define I2CS0_NODE DT_NODELABEL(i2cs0)
#define SPI_NODE DT_NODELABEL(spi0)
#define XSPI_NODE DT_NODELABEL(xspi)
#define TIMER0_NODE DT_NODELABEL(timer0)
#define WATCHDOG0_NODE DT_NODELABEL(watchdog0)
#define SLOWCLK_WATCHDOG_NODE DT_NODELABEL(watchdog_slowclk)
#define SDIO_DEVICE_NODE DT_NODELABEL(sdio_device0)

#define GPIO3_MCU_GPIO26_PIN 2 
#define GPIO3_MCU_GPIO27_PIN 3 


#define I2C_SABRE_SLAVE_ADDR 0x42
#define I2C_TARGET_OWN_ADDR I2C_SABRE_SLAVE_ADDR 

#define FLASH_TEST_ERASE_SIZE DT_PROP(XSPI_NODE, erase_block_size)
#define FLASH_TEST_OFFSET (0x480000)
#define FLASH_TEST_PATTERN_SIZE 32
#define FLASH_RWW_TEST_ERASE_SIZE 0x1000U
#define FLASH_RWW_TEST_ERASE_BLOCKS 0x280U
#define FLASH_RWW_TEST_TIMER_PERIOD_US 100U
#define FLASH_RWW_TEST_WRITE_SIZE 1024U
#define FLASH_RWW_TEST_THREAD_STACK_SIZE 2048U
#define FLASH_RWW_TEST_HIGH_PRIO K_PRIO_PREEMPT(1)
#define FLASH_RWW_TEST_ERASE_PRIO K_PRIO_PREEMPT(5)

#define PWM_NODE DT_NODELABEL(pwm0)

/* SPWM output line -> pad, per srw1500_rdk.dts pinctrl-0. */
#define PWM_CAPTURE_CHANNEL 4U
#define PWM_CAPTURE_PIN     "MCU_GPIO18"
#define PWM_OUT_ACTIVE      5U
#define PWM_OUT_ACTIVE_PIN  "GPADC_IN5"

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
static int srw1500_usb_debug_init(void)
{
	struct usbd_desc_node *descriptors[] = {
		&srw1500_usb_lang,
		&srw1500_usb_manufacturer,
		&srw1500_usb_product,
		&srw1500_usb_sn,
	};
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(descriptors); i++) {
		ret = usbd_add_descriptor(&srw1500_usbd, descriptors[i]);
		if (ret < 0) {
			return ret;
		}
	}

	if (usbd_caps_speed(&srw1500_usbd) != USBD_SPEED_HS) {
		return -ENOTSUP;
	}

	ret = usbd_add_configuration(&srw1500_usbd, USBD_SPEED_HS,
				     &srw1500_usb_hs_config);
	if (ret < 0) {
		return ret;
	}

	ret = usbd_register_class(&srw1500_usbd, "cdc_acm_0", USBD_SPEED_HS, 1);
	if (ret < 0) {
		return ret;
	}

	ret = usbd_device_set_code_triple(&srw1500_usbd, USBD_SPEED_HS,
				  USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	if (ret < 0) {
		return ret;
	}

	ret = usbd_device_set_bcd_usb(&srw1500_usbd, USBD_SPEED_HS, USB_BCD);
	if (ret < 0) {
		return ret;
	}

	ret = usbd_init(&srw1500_usbd);
	if (ret < 0) {
		return ret;
	}

	return usbd_enable(&srw1500_usbd);
}
#endif

PINCTRL_DT_DEV_CONFIG_DECLARE(SPI_NODE);

extern bool flash_cad_xspi_is_in_rww_fetch_window(const struct device *dev);

static const pinctrl_soc_pin_t gpio3_mcu_gpio26_pin = {
	.pinmux = SRW1500_GBL_PINMUX(26, 11, 0),
};

static const pinctrl_soc_pin_t gpio3_mcu_gpio27_pin = {
	.pinmux = SRW1500_GBL_PINMUX(27, 11, 0),
	.pincfg = BIT(SRW1500_INPUT_ENABLE_SHIFT),
	.flags = BIT(SRW1500_INPUT_ENABLE_SHIFT),
	.port = 29,
};

static struct sdio_device_dma_descriptor sdio_desc[1] __nocache __aligned(16);
static uint8_t sdio_buf[2048] __nocache __aligned(4);

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

volatile uint32_t rww_xip_irq_count;
volatile uint32_t rww_xip_result;
volatile uint32_t rww_high_prio_count;
volatile uint32_t rww_high_prio_result;
volatile int rww_erase_thread_ret;
volatile bool rww_task_switch_test_active;

static struct k_sem rww_test_sem;
static struct k_sem rww_erase_done_sem;
static struct k_thread rww_high_prio_thread;
static struct k_thread rww_erase_thread;
K_THREAD_STACK_DEFINE(rww_high_prio_stack, FLASH_RWW_TEST_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(rww_erase_stack, FLASH_RWW_TEST_THREAD_STACK_SIZE);

static int start_test_timer(const struct device *timer_dev);
static void stop_test_timer(const struct device *timer_dev);

#define RWW_TEST_RAMFUNC __attribute__((section(".itcm.rww_test"), noinline))

static const uint32_t rww_flash_data[256] = {
    0x11223344, 0x55667788, 0xAABBCCDD, 0xDEADBEEF,
    0x00000004, 0x00000005, 0x00000006, 0x00000007,
    0x00000008, 0x00000009, 0x0000000A, 0x0000000B,
    0x0000000C, 0x0000000D, 0x0000000E, 0x0000000F,

    0x00000010, 0x00000011, 0x00000012, 0x00000013,
    0x00000014, 0x00000015, 0x00000016, 0x00000017,
    0x00000018, 0x00000019, 0x0000001A, 0x0000001B,
    0x0000001C, 0x0000001D, 0x0000001E, 0x0000001F,

    0x00000020, 0x00000021, 0x00000022, 0x00000023,
    0x00000024, 0x00000025, 0x00000026, 0x00000027,
    0x00000028, 0x00000029, 0x0000002A, 0x0000002B,
    0x0000002C, 0x0000002D, 0x0000002E, 0x0000002F,

    0x00000030, 0x00000031, 0x00000032, 0x00000033,
    0x00000034, 0x00000035, 0x00000036, 0x00000037,
    0x00000038, 0x00000039, 0x0000003A, 0x0000003B,
    0x0000003C, 0x0000003D, 0x0000003E, 0x0000003F,

    0x00000040, 0x00000041, 0x00000042, 0x00000043,
    0x00000044, 0x00000045, 0x00000046, 0x00000047,
    0x00000048, 0x00000049, 0x0000004A, 0x0000004B,
    0x0000004C, 0x0000004D, 0x0000004E, 0x0000004F,

    0x00000050, 0x00000051, 0x00000052, 0x00000053,
    0x00000054, 0x00000055, 0x00000056, 0x00000057,
    0x00000058, 0x00000059, 0x0000005A, 0x0000005B,
    0x0000005C, 0x0000005D, 0x0000005E, 0x0000005F,

    0x00000060, 0x00000061, 0x00000062, 0x00000063,
    0x00000064, 0x00000065, 0x00000066, 0x00000067,
    0x00000068, 0x00000069, 0x0000006A, 0x0000006B,
    0x0000006C, 0x0000006D, 0x0000006E, 0x0000006F,

    0x00000070, 0x00000071, 0x00000072, 0x00000073,
    0x00000074, 0x00000075, 0x00000076, 0x00000077,
    0x00000078, 0x00000079, 0x0000007A, 0x0000007B,
    0x0000007C, 0x0000007D, 0x0000007E, 0x0000007F,

    0x00000080, 0x00000081, 0x00000082, 0x00000083,
    0x00000084, 0x00000085, 0x00000086, 0x00000087,
    0x00000088, 0x00000089, 0x0000008A, 0x0000008B,
    0x0000008C, 0x0000008D, 0x0000008E, 0x0000008F,

    0x00000090, 0x00000091, 0x00000092, 0x00000093,
    0x00000094, 0x00000095, 0x00000096, 0x00000097,
    0x00000098, 0x00000099, 0x0000009A, 0x0000009B,
    0x0000009C, 0x0000009D, 0x0000009E, 0x0000009F,

    0x000000A0, 0x000000A1, 0x000000A2, 0x000000A3,
    0x000000A4, 0x000000A5, 0x000000A6, 0x000000A7,
    0x000000A8, 0x000000A9, 0x000000AA, 0x000000AB,
    0x000000AC, 0x000000AD, 0x000000AE, 0x000000AF,

    0x000000B0, 0x000000B1, 0x000000B2, 0x000000B3,
    0x000000B4, 0x000000B5, 0x000000B6, 0x000000B7,
    0x000000B8, 0x000000B9, 0x000000BA, 0x000000BB,
    0x000000BC, 0x000000BD, 0x000000BE, 0x000000BF,

    0x000000C0, 0x000000C1, 0x000000C2, 0x000000C3,
    0x000000C4, 0x000000C5, 0x000000C6, 0x000000C7,
    0x000000C8, 0x000000C9, 0x000000CA, 0x000000CB,
    0x000000CC, 0x000000CD, 0x000000CE, 0x000000CF,

    0x000000D0, 0x000000D1, 0x000000D2, 0x000000D3,
    0x000000D4, 0x000000D5, 0x000000D6, 0x000000D7,
    0x000000D8, 0x000000D9, 0x000000DA, 0x000000DB,
    0x000000DC, 0x000000DD, 0x000000DE, 0x000000DF,

    0x000000E0, 0x000000E1, 0x000000E2, 0x000000E3,
    0x000000E4, 0x000000E5, 0x000000E6, 0x000000E7,
    0x000000E8, 0x000000E9, 0x000000EA, 0x000000EB,
    0x000000EC, 0x000000ED, 0x000000EE, 0x000000EF,

    0x000000F0, 0x000000F1, 0x000000F2, 0x000000F3,
    0x000000F4, 0x000000F5, 0x000000F6, 0x000000F7,
    0x000000F8, 0x000000F9, 0x000000FA, 0x000000FB,
    0x000000FC, 0x000000FD, 0x000000FE, 0x000000FF
};
static __attribute__((noinline)) uint32_t rww_flash_function(uint32_t count)
{
	volatile uint32_t value = count;

    // for (uint32_t i = 0; i < 10; i++) {
        value = value * 1664525U + 1013904223U;
        value ^= (rww_flash_data[value%256] >> 13);
        // value += i;
    // }

    return value;
}

static void rww_high_prio_thread_fn(void *p1, void *p2, void *p3)
{
	const struct device *timer_dev = DEVICE_DT_GET(TIMER0_NODE);

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		k_sem_take(&rww_test_sem, K_FOREVER);

		rww_high_prio_count++;
		rww_high_prio_result = rww_flash_function(rww_high_prio_count);
					start_test_timer(timer_dev);

	}
}

static void rww_erase_thread_fn(void *p1, void *p2, void *p3)
{
	const struct device *flash_dev = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	rww_erase_thread_ret = flash_erase(flash_dev, FLASH_TEST_OFFSET,
					   FLASH_RWW_TEST_ERASE_BLOCKS *
					   FLASH_RWW_TEST_ERASE_SIZE);
	k_sem_give(&rww_erase_done_sem);
}

static RWW_TEST_RAMFUNC void rww_test_isr(const struct device *dev, void *user_data)
{
	const struct device *flash_dev = DEVICE_DT_GET(XSPI_NODE);
	const struct device *timer_dev = DEVICE_DT_GET(TIMER0_NODE);

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (flash_cad_xspi_is_in_rww_fetch_window(flash_dev)) {
		if (rww_task_switch_test_active) {
			stop_test_timer(timer_dev);
			k_sem_give(&rww_test_sem);
		} else {
			rww_xip_result = rww_flash_function(rww_xip_irq_count);
			rww_xip_irq_count++;
		}
	}
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
	shell_print(shell, "GPIO output toggle test start");

	const struct device *gpio_dev;
	int ret;

	gpio_dev = DEVICE_DT_GET(GPIO3_NODE);
	if (!device_is_ready(gpio_dev)) {
		shell_print(shell, "Error: GPIO device not ready");
		return -ENODEV;
	}

	ret = pinctrl_configure_pins(&gpio3_mcu_gpio26_pin, 1, PINCTRL_REG_NONE);
	if (ret < 0) {
		shell_print(shell, "GPIO test pinctrl apply failed: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure(gpio_dev, GPIO3_MCU_GPIO26_PIN, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		shell_print(shell, "Error configuring GPIO: %d", ret);
		return ret;
	}

	gpio_pin_set(gpio_dev, GPIO3_MCU_GPIO26_PIN, 1);
	k_msleep(3000);
	gpio_pin_set(gpio_dev, GPIO3_MCU_GPIO26_PIN, 0);

	shell_print(shell, "GPIO output test done");
	return 0;
}

static int gpio_input_test(const struct shell *shell)
{
	shell_print(shell, "GPIO input interrupt test start");

	const struct device *gpio_dev;
	int ret;

	gpio_dev = DEVICE_DT_GET(GPIO3_NODE);
	if (!device_is_ready(gpio_dev)) {
		shell_print(shell, "Error: GPIO device not ready");
		return -ENODEV;
	}

	ret = pinctrl_configure_pins(&gpio3_mcu_gpio27_pin, 1, PINCTRL_REG_NONE);
	if (ret < 0) {
		shell_print(shell, "GPIO test pinctrl apply failed: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure(gpio_dev, GPIO3_MCU_GPIO27_PIN, GPIO_INPUT);
	if (ret < 0) {
		shell_print(shell, "Error configuring GPIO as input: %d", ret);
		return ret;
	}

	gpio_input_test_irq_count = 0;
	gpio_init_callback(&gpio_input_test_cb_data, gpio_input_test_isr,
			   BIT(GPIO3_MCU_GPIO27_PIN));

	ret = gpio_add_callback(gpio_dev, &gpio_input_test_cb_data);
	if (ret < 0) {
		shell_print(shell, "Error adding GPIO callback: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure(gpio_dev, GPIO3_MCU_GPIO27_PIN,
					   GPIO_INT_EDGE_RISING);
	if (ret < 0) {
		shell_print(shell, "Error configuring GPIO interrupt: %d", ret);
		gpio_remove_callback(gpio_dev, &gpio_input_test_cb_data);
		return ret;
	}

	shell_print(shell, "Waiting for a rising edge on GPIO...");

	while (gpio_input_test_irq_count == 0) {
		k_msleep(10);
	}

	gpio_remove_callback(gpio_dev, &gpio_input_test_cb_data);
	gpio_pin_interrupt_configure(gpio_dev, GPIO3_MCU_GPIO27_PIN, GPIO_INT_DISABLE);

	shell_print(shell, "GPIO rising edge detected.\nGPIO input test done");
	return 0;
}


static int pwm_apply(const struct shell *shell, const struct device *dev, uint32_t ch,
		     const char *name, uint32_t period_ns, uint32_t pulse_ns)
{
	int ret;

	ret = pwm_set(dev, ch, period_ns, pulse_ns, 0);
	if (ret < 0) {
		shell_print(shell, "FAIL: %s pwm_set returned %d", name, ret);
		return ret;
	}

	shell_print(shell, "PASS: PWM output period=%uus pulse=%uus duty=%u%%", period_ns / 1000U,
		    pulse_ns / 1000U,
		    (period_ns == 0U) ? 0U : (pulse_ns * 100U) / period_ns);
	return 0;
}


static int pwm_capture_test(const struct shell *shell, const struct device *pwm_dev,
			    uint32_t duty)
{
	uint64_t period_us;
	uint64_t pulse_us;
	uint64_t expected_pulse_us;
	uint64_t pulse_error_us;
	uint32_t measured_duty = (duty == 0U || duty == 100U) ? 50U : duty;
	int ret;

	shell_print(shell, "Generating %u%% duty", duty);
	ret = pwm_set(pwm_dev, PWM_OUT_ACTIVE, 1000000U, measured_duty * 10000U,
		      PWM_POLARITY_NORMAL);
	if (ret < 0) {
		shell_error(shell, "Output configuration failed: %d", ret);
		return ret;
	}

	k_msleep(1000);
	ret = pwm_capture_usec(pwm_dev, PWM_CAPTURE_CHANNEL,
			       PWM_CAPTURE_TYPE_BOTH | PWM_CAPTURE_MODE_SINGLE |
			       PWM_POLARITY_NORMAL,
			       &period_us, &pulse_us, K_SECONDS(2));
	if (ret < 0) {
		shell_error(shell, "Capture failed: %d%s", ret,
			    (ret == -EAGAIN) ?
			    " (timeout: check the GPADC_IN5-to-MCU_GPIO18 wire)" : "");
		goto out;
	}

	expected_pulse_us = measured_duty * 10U;
	pulse_error_us = (pulse_us > expected_pulse_us) ?
			 pulse_us - expected_pulse_us : expected_pulse_us - pulse_us;
	if (period_us < 900U || period_us > 1100U || pulse_us > period_us ||
	    pulse_error_us > 50U) {
		shell_error(shell, "Capture signal invalid: period=%llu us pulse=%llu us "
			    "(check the GPADC_IN5-to-MCU_GPIO18 wire)", period_us, pulse_us);
		ret = -EIO;
		goto out;
	}

	if (measured_duty == duty) {
		shell_print(shell, "Input captured period=%llu us pulse=%llu us duty=%llu%%",
			    period_us, pulse_us,
			    (pulse_us * 100U) / period_us);
		k_msleep(1000);
	} else {
		ret = pwm_set(pwm_dev, PWM_OUT_ACTIVE, 1000000U, duty * 10000U,
			      PWM_POLARITY_NORMAL);
		if (ret == 0) {
			shell_print(shell, "Capture connection verified; Capture is now steady %s",
				     (duty == 0U) ? "LOW" : "HIGH");
			k_msleep(1000);
		}
	}

	out:
	(void)pwm_set(pwm_dev, PWM_OUT_ACTIVE, 1000000U, 0U, PWM_POLARITY_NORMAL);
	return ret;
}
static int pwm_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *pwm_dev = DEVICE_DT_GET(PWM_NODE);
	static const uint32_t duty[] = {0, 25, 50, 75, 100};
	char *end;
	unsigned long capture_duty;
	uint64_t cycles = 0;
	int ret;

	if (!device_is_ready(pwm_dev)) {
		shell_print(shell, "FAIL: PWM device not ready");
		return -ENODEV;
	}

	if (argc == 3 && strcmp(argv[1], "capture") == 0) {
		capture_duty = strtoul(argv[2], &end, 10);
		if (*end != '\0' || (capture_duty != 0U && capture_duty != 25U &&
		    capture_duty != 50U && capture_duty != 75U && capture_duty != 100U)) {
			shell_error(shell, "Duty must be 0, 25, 50, 75, or 100");
			return -EINVAL;
		}
		return pwm_capture_test(shell, pwm_dev, (uint32_t)capture_duty);
	}
	if (argc != 1) {
		shell_error(shell, "Usage: pwm_test [capture <0|25|50|75|100>]");
		return -EINVAL;
	}

	ret = pwm_get_cycles_per_sec(pwm_dev, PWM_OUT_ACTIVE, &cycles);
	if (ret < 0) {
		shell_print(shell, "FAIL: pwm_get_cycles_per_sec returned %d", ret);
		return ret;
	}
	shell_print(shell, "PWM counter clock: %llu Hz", cycles);

	for (int i = 0; i < ARRAY_SIZE(duty); i++) {
		ret = pwm_apply(shell, pwm_dev, PWM_OUT_ACTIVE, PWM_OUT_ACTIVE_PIN,
				1000000U, duty[i] * 10000U);
		if (ret < 0) {
			return ret;
		}
		k_msleep(1000);
	}

	ret = pwm_apply(shell, pwm_dev, PWM_OUT_ACTIVE, PWM_OUT_ACTIVE_PIN, 1000000U, 0U);
	if (ret < 0) {
		return ret;
	}

	shell_print(shell, "\r\nPWM test is done");
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
	int ret;
	bool write_ok = false;
	bool read_ok = false;
	uint8_t master_tx[4] = {0x5b, 0x5a, 0x33, 0x0a};
	uint8_t master_rx[8] = {0};

	if (!device_is_ready(i2c_dev)) {
		shell_print(shell, "I2C master device not ready");
		return -ENODEV;
	}

	shell_print(shell, "\r\nI2C0 controller -> external target 0x%02x test\r\n",
		    I2C_SABRE_SLAVE_ADDR);

	ret = i2c_configure(i2c_dev, I2C_SPEED_SET(I2C_SPEED_FAST) | I2C_MODE_CONTROLLER);
	if (ret) {
		shell_print(shell, "I2C configure (400kHz) failed: %d", ret);
		return ret;
	}
	shell_print(shell, "I2C bus configured for Fast-mode (400kHz)");

	ret = i2c_write(i2c_dev, master_tx, sizeof(master_tx), I2C_SABRE_SLAVE_ADDR);
	if (ret) {
		shell_print(shell, "i2c_write failed: %d", ret);
	} else {
		write_ok = true;
		shell_print(shell, "Master TX: %02x %02x %02x %02x",
			    master_tx[0], master_tx[1], master_tx[2], master_tx[3]);
		shell_print(shell, "External target ACKed write");
	}

	ret = i2c_read(i2c_dev, master_rx, sizeof(master_rx), I2C_SABRE_SLAVE_ADDR);
	if (ret) {
		shell_print(shell, "i2c_read failed: %d", ret);
	} else {
		read_ok = true;
		shell_print(shell, "External target RX: %02x %02x %02x %02x %02x %02x %02x %02x",
			    master_rx[0], master_rx[1], master_rx[2], master_rx[3],
			    master_rx[4], master_rx[5], master_rx[6], master_rx[7]);
	}

	shell_print(shell, "\n=== I2C External Master Test Summary ===");
	shell_print(shell, "Write direction : %s", write_ok ? "PASS" : "FAIL");
	shell_print(shell, "Read direction  : %s", read_ok ? "PASS" : "FAIL");

	return (write_ok && read_ok) ? 0 : -1;
}

static uint8_t i2c_target_rx_buf[8];
static size_t i2c_target_rx_len;
static uint8_t i2c_target_tx_byte = 0xA5;
static size_t i2c_target_tx_count;
static bool i2c_target_stop_seen;

static int i2c_target_write_requested(struct i2c_target_config *config)
{
	i2c_target_rx_len = 0;
	return 0;
}

static int i2c_target_write_received(struct i2c_target_config *config, uint8_t val)
{
	if (i2c_target_rx_len < sizeof(i2c_target_rx_buf)) {
		i2c_target_rx_buf[i2c_target_rx_len++] = val;
	}
	return 0;
}

static int i2c_target_read_requested(struct i2c_target_config *config, uint8_t *val)
{
	i2c_target_tx_count = 0;
	*val = i2c_target_tx_byte++;
	i2c_target_tx_count++;
	return 0;
}

static int i2c_target_read_processed(struct i2c_target_config *config, uint8_t *val)
{
	*val = i2c_target_tx_byte++;
	i2c_target_tx_count++;
	return 0;
}

static int i2c_target_stop(struct i2c_target_config *config)
{
	i2c_target_stop_seen = true;
	return 0;
}

static const struct i2c_target_callbacks i2c_target_cbs = {
	.write_requested = i2c_target_write_requested,
	.write_received  = i2c_target_write_received,
	.read_requested  = i2c_target_read_requested,
	.read_processed  = i2c_target_read_processed,
	.stop            = i2c_target_stop,
};

static struct i2c_target_config i2c_target_cfg = {
	.address = I2C_TARGET_OWN_ADDR,
	.callbacks = &i2c_target_cbs,
};

static int i2c_loopback_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *i2c_dev = DEVICE_DT_GET(I2C0_NODE);
	const struct device *i2cs_dev = DEVICE_DT_GET(I2CS0_NODE);
	int ret;
	bool write_ok = false;
	bool read_ok = false;
	uint8_t master_tx[4] = {0x5b, 0x5a, 0x33, 0x0a};
	uint8_t master_rx[8] = {0};

	if (!device_is_ready(i2c_dev)) {
		shell_print(shell, "I2C master device not ready");
		return -ENODEV;
	}
	if (!device_is_ready(i2cs_dev)) {
		shell_print(shell, "I2C target device not ready");
		return -ENODEV;
	}

	i2c_target_rx_len = 0;
	i2c_target_tx_count = 0;
	i2c_target_tx_byte = 0xA5;
	i2c_target_stop_seen = false;

	ret = i2c_target_register(i2cs_dev, &i2c_target_cfg);
	if (ret) {
		shell_print(shell, "i2c_target_register failed: %d", ret);
		return ret;
	}

	ret = i2c_configure(i2c_dev, I2C_SPEED_SET(I2C_SPEED_FAST) | I2C_MODE_CONTROLLER);
	if (ret) {
		shell_print(shell, "I2C configure (400kHz) failed: %d", ret);
	} else {
		ret = i2c_write(i2c_dev, master_tx, sizeof(master_tx), I2C_TARGET_OWN_ADDR);
		if (ret) {
			shell_print(shell, "i2c_write failed: %d", ret);
		} else {
			write_ok = true;
		}

		ret = i2c_read(i2c_dev, master_rx, sizeof(master_rx), I2C_TARGET_OWN_ADDR);
		if (ret) {
			shell_print(shell, "i2c_read failed: %d", ret);
		} else {
			read_ok = true;
		}
	}

	ret = i2c_target_unregister(i2cs_dev, &i2c_target_cfg);
	if (ret) {
		shell_print(shell, "i2c_target_unregister failed: %d", ret);
		return ret;
	}

	shell_print(shell, "\n=== I2C Loopback Test Summary ===");
	shell_print(shell, "Master write   : %s", write_ok ? "PASS" : "FAIL");
	shell_print(shell, "Master TX      : %02x %02x %02x %02x",
		    master_tx[0], master_tx[1], master_tx[2], master_tx[3]);
	shell_print(shell, "Master read    : %s", read_ok ? "PASS" : "FAIL");
	shell_print(shell, "Master RX      : %02x %02x %02x %02x %02x %02x %02x %02x",
		    master_rx[0], master_rx[1], master_rx[2], master_rx[3],
		    master_rx[4], master_rx[5], master_rx[6], master_rx[7]);
	shell_print(shell, "Target write_received: %s",
		    i2c_target_rx_len > 0 ? "PASS" : "NOT EXERCISED");
	shell_print(shell, "Target read_requested/processed: %s ",
		    i2c_target_tx_count > 0 ? "PASS" : "NOT EXERCISED");
	shell_print(shell, "Target stop callback: %s", i2c_target_stop_seen ? "PASS" : "NOT EXERCISED");

	return (write_ok && read_ok) ? 0 : -1;
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

static int spi_loopback_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *spi_dev = DEVICE_DT_GET(SPI_NODE);
	uint8_t tx_buf[] = {
		0x5b, 0x5a, 0x33, 0x0a, 0xa5, 0x11,
		0x22, 0x33, 0x44, 0x55, 0x66, 0x77
	};
	uint8_t rx_buf[sizeof(tx_buf)] = {0};
	struct spi_buf tx = { .buf = tx_buf, .len = sizeof(tx_buf) };
	struct spi_buf rx = { .buf = rx_buf, .len = sizeof(rx_buf) };
	struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };
	int ret;

	ARG_UNUSED(shell);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(spi_dev)) {
		printk("SPI device not ready!\n");
		return -ENODEV;
	}

	ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
	if (ret < 0) {
		printk("SPI loopback transfer failed: %d\n", ret);
		return ret;
	}

	printk("SPI loopback RX: %02x %02x %02x %02x %02x %02x "
	       "%02x %02x %02x %02x %02x %02x\n",
	       rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4], rx_buf[5],
	       rx_buf[6], rx_buf[7], rx_buf[8], rx_buf[9], rx_buf[10], rx_buf[11]);

	if (memcmp(rx_buf, tx_buf, sizeof(tx_buf)) != 0) {
		printk("SPI loopback: FAIL\n");
		return -EIO;
	}

	printk("SPI loopback: PASS\n");
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

	ret = flash_erase(flash_dev, FLASH_TEST_OFFSET, FLASH_TEST_ERASE_SIZE);
	if (ret) {
		shell_print(shell, "Flash erase after write failed: %d", ret);
		return ret;
	}

	memset(actual, 0, sizeof(actual));
	ret = flash_read(flash_dev, FLASH_TEST_OFFSET, actual, sizeof(actual));
	if (ret) {
		shell_print(shell, "Flash read failed: %d", ret);
		return ret;
	}

	for (size_t i = 0; i < sizeof(actual); i++) {
		if (actual[i] != 0xFF) {
			shell_print(shell, "Erase verify failed at byte %u: 0x%02x",
				    (unsigned int)i, (unsigned int)actual[i]);
			return -EIO;
		}
	}

	shell_print(shell, "Flash erase/read test passed.");
	return 0;
}

static int start_test_timer(const struct device *timer_dev)
{
	uint32_t ticks = counter_us_to_ticks(timer_dev, FLASH_RWW_TEST_TIMER_PERIOD_US);
	struct counter_top_cfg top_cfg = {
		.callback = rww_test_isr,
		.user_data = NULL,
		.ticks = MAX(ticks, 1U),
		.flags = 0,
	};
	int ret;

	ret = counter_start(timer_dev);
	if (ret) {
		return ret;
	}

	ret = counter_set_top_value(timer_dev, &top_cfg);
	if (ret) {
		(void)counter_stop(timer_dev);
	}

	return ret;
}

static void stop_test_timer(const struct device *timer_dev)
{
	(void)counter_stop(timer_dev);
}

static int flash_rww_write_after_erase(const struct shell *shell,
				       const struct device *flash_dev)
{
	static uint8_t write_buf[FLASH_RWW_TEST_WRITE_SIZE];
	static uint8_t read_buf[FLASH_RWW_TEST_WRITE_SIZE];
	int ret;

	for (uint32_t i = 0; i < FLASH_RWW_TEST_WRITE_SIZE; i++) {
		write_buf[i] = (uint8_t)(0x5AU ^ i);
	}

	ret = flash_write(flash_dev, FLASH_TEST_OFFSET, write_buf, sizeof(write_buf));
	if (ret) {
		shell_print(shell, "RWW flash write failed: %d", ret);
		return ret;
	}

	memset(read_buf, 0, sizeof(read_buf));
	ret = flash_read(flash_dev, FLASH_TEST_OFFSET, read_buf, sizeof(read_buf));
	if (ret) {
		shell_print(shell, "RWW flash readback failed: %d", ret);
		return ret;
	}

	if (memcmp(read_buf, write_buf, sizeof(write_buf)) != 0) {
		shell_print(shell, "RWW flash write verify failed.");
		return -EIO;
	}

	return 0;
}

static int flash_rww_irq_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *flash_dev = DEVICE_DT_GET(XSPI_NODE);
	const struct device *timer_dev = DEVICE_DT_GET(TIMER0_NODE);
	uint32_t erase_irq_count;
	int write_ret = 0;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(flash_dev)) {
		shell_print(shell, "Flash device not ready.");
		return -ENODEV;
	}

	if (!device_is_ready(timer_dev)) {
		shell_print(shell, "Timer device not ready.");
		return -ENODEV;
	}

	rww_xip_irq_count = 0;
	rww_xip_result = 0;

	ret = start_test_timer(timer_dev);
	if (ret) {
		shell_print(shell, "Failed to start RWW test timer: %d", ret);
		return ret;
	}

	ret = flash_erase(flash_dev, FLASH_TEST_OFFSET,
			  FLASH_RWW_TEST_ERASE_BLOCKS * FLASH_RWW_TEST_ERASE_SIZE);
	erase_irq_count = rww_xip_irq_count;
	if (ret == 0) {
		write_ret = flash_rww_write_after_erase(shell, flash_dev);
	}

	stop_test_timer(timer_dev);

	printk("erase=%d write=%d erase_irq=%u xip_irq=%u result=0x%08x\n",
	       ret, write_ret, erase_irq_count, rww_xip_irq_count, rww_xip_result);

	if (ret == 0 && rww_xip_irq_count == 0U) {
		shell_print(shell, "RWW IRQ test did not observe an XIP fetch window.");
		return -EIO;
	}

	return ret ? ret : write_ret;
}

static int flash_rww_task_switch_test(const struct shell *shell, size_t argc,
				      char **argv)
{
	const struct device *flash_dev = DEVICE_DT_GET(XSPI_NODE);
	const struct device *timer_dev = DEVICE_DT_GET(TIMER0_NODE);
	uint32_t high_prio_count;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(flash_dev)) {
		shell_print(shell, "Flash device not ready.");
		return -ENODEV;
	}

	if (!device_is_ready(timer_dev)) {
		shell_print(shell, "Timer device not ready.");
		return -ENODEV;
	}

	k_sem_init(&rww_test_sem, 0, 1);
	k_sem_init(&rww_erase_done_sem, 0, 1);

	rww_xip_irq_count = 0;
	rww_high_prio_count = 0;
	rww_high_prio_result = 0;
	rww_erase_thread_ret = 0;
	rww_task_switch_test_active = true;

	ret = start_test_timer(timer_dev);
	if (ret) {
		rww_task_switch_test_active = false;
		shell_print(shell, "Failed to start RWW test timer: %d", ret);
		return ret;
	}

	k_thread_create(&rww_high_prio_thread, rww_high_prio_stack,
			K_THREAD_STACK_SIZEOF(rww_high_prio_stack),
			rww_high_prio_thread_fn, NULL, NULL, NULL,
			FLASH_RWW_TEST_HIGH_PRIO, 0, K_NO_WAIT);

	k_thread_create(&rww_erase_thread, rww_erase_stack,
			K_THREAD_STACK_SIZEOF(rww_erase_stack),
			rww_erase_thread_fn, (void *)flash_dev, NULL, NULL,
			FLASH_RWW_TEST_ERASE_PRIO, 0, K_NO_WAIT);

	k_sem_take(&rww_erase_done_sem, K_FOREVER);

	stop_test_timer(timer_dev);
	rww_task_switch_test_active = false;
	high_prio_count = rww_high_prio_count;
	k_thread_abort(&rww_high_prio_thread);

	printk("erase=%d high_prio_count=%u result=0x%08x\n",
	       rww_erase_thread_ret, high_prio_count, rww_high_prio_result);

	if (rww_erase_thread_ret) {
		shell_print(shell, "RWW task-switch erase failed: %d",
			    rww_erase_thread_ret);
		return rww_erase_thread_ret;
	}

	if (high_prio_count == 0U) {
		shell_print(shell,
			    "RWW task-switch test did not observe high-priority execution.");
		return -EIO;
	}

	shell_print(shell, "RWW task-switch test observed high_prio_count=%u.",
		    high_prio_count);

	return 0;
}

#if defined(CONFIG_FILE_SYSTEM_LITTLEFS)
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_mnt = {
	.type = FS_LITTLEFS,
	.mnt_point = "/lfs",
	.fs_data = &storage,
	.storage_dev = (void *)PARTITION_ID(storage_partition),
};

static int littlefs_test(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ret = fs_mount(&lfs_mnt);
	if (ret < 0) {
		ret = fs_mkfs(FS_LITTLEFS, (uintptr_t)PARTITION_ID(storage_partition),
                       NULL, 0);
		if (ret) {
            printk("LFS format failed: %d", ret);
            return ret;
        }
        ret = fs_mount(&lfs_mnt);
        if (ret) {
            printk("LFS mount after format failed: %d", ret);
            return ret;
        }

	}

	printk("LittleFS mounted at /lfs\n");

	struct fs_file_t file;
	fs_file_t_init(&file);

	ret = fs_open(&file, "/lfs/test.txt",
		      FS_O_CREATE | FS_O_WRITE | FS_O_READ);
	if (ret < 0) {
		printk("open failed: %d\n", ret);
		return ret;
	}

	fs_write(&file, "hello littlefs\n", 12);
	fs_seek(&file, 0, FS_SEEK_SET);

	char buf[32] = {0};
	fs_read(&file, buf, sizeof(buf) - 1);

	printk("read: %s\n", buf);

	fs_close(&file);

	return 0;
}
#elif defined(CONFIG_FAT_FILESYSTEM_ELM)
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
#endif

static int sdio_wait_for_f2_enabled(const struct shell *shell, const struct device *dev)
{
	bool enabled = false;
	int ret = 0;

	if (!WAIT_FOR(((ret = sdio_device_func_is_enabled(dev, SDIO_DEVICE_FUNC_2,
							    &enabled)) == 0) && enabled,
		      5000000, k_msleep(1))) {
		shell_print(shell, "Timed out waiting for host to enable SDIO F2");
		return ret < 0 ? ret : -ETIMEDOUT;
	}

	shell_print(shell, "F2 enabled received from host");
	return 0;
}

static int sdio_wait_for_dma_done(const struct shell *shell, const char *name,
				  int (*status_get)(const struct device *,
						    enum sdio_device_func, uint32_t *),
				  int (*error_get)(const struct device *,
						   enum sdio_device_func, uint32_t *),
				  const struct device *dev)
{
	uint32_t error = 0U;
	uint32_t status = 0U;
	int ret = 0;

	if (!WAIT_FOR(((ret = status_get(dev, SDIO_DEVICE_FUNC_2, &status)) == 0) &&
		      status == SDIO_DEVICE_DMA_STATUS_DONE,
		      5000000, k_msleep(1))) {
		(void)error_get(dev, SDIO_DEVICE_FUNC_2, &error);
		shell_print(shell, "%s DMA timed out: status=%u error=%u", name,
			    status, error);
		return ret < 0 ? ret : -ETIMEDOUT;
	}

	shell_print(shell, "%s DMA complete", name);
	return 0;
}

static int sdio_f2_rx_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev = DEVICE_DT_GET(SDIO_DEVICE_NODE);;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(dev)) {
		shell_print(shell, "SDIO device not ready");
		return -ENODEV;
	}

	memset(sdio_buf, 0, sizeof(sdio_buf));
	ret = sdio_device_setup_tx_rx_desc(sdio_desc, sdio_buf, sizeof(sdio_buf));
	if (ret < 0) {
		shell_print(shell, "RX descriptor setup failed: %d", ret);
		return ret;
	}

	shell_print(shell, "RX descriptor initialized");
	ret = sdio_device_rx_submit(dev, SDIO_DEVICE_FUNC_2, sdio_desc);
	if (ret < 0) {
		shell_print(shell, "F2 RX submit failed: %d", ret);
		return ret;
	}
	shell_print(shell, "F2 RX DMA init done");

	ret = sdio_wait_for_f2_enabled(shell, dev);
	if (ret < 0) {
		return ret;
	}

	ret = sdio_device_func_ready(dev, SDIO_DEVICE_FUNC_2);
	if (ret < 0) {
		shell_print(shell, "F2 enable failed: %d", ret);
		return ret;
	}
	shell_print(shell, "F2 ready set");

	ret = sdio_wait_for_dma_done(shell, "RX", sdio_device_get_rx_status,
				       sdio_device_get_rx_error, dev);
	if (ret < 0) {
		return ret;
	}

	shell_print(shell, "Data received: %u %u ...", sdio_buf[0], sdio_buf[1]);
	return 0;
}

static int sdio_f2_tx_test(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev = DEVICE_DT_GET(SDIO_DEVICE_NODE);;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(dev)) {
		shell_print(shell, "SDIO device not ready");
		return -ENODEV;
	}

	memset(sdio_buf, 0x5a, sizeof(sdio_buf));
	sdio_buf[0] = 0x00;
	sdio_buf[1] = 0x08;
	sdio_buf[2] = ~sdio_buf[0];
	sdio_buf[3] = ~sdio_buf[1];

	ret = sdio_device_setup_tx_rx_desc(sdio_desc, sdio_buf, sizeof(sdio_buf));
	if (ret < 0) {
		shell_print(shell, "TX descriptor setup failed: %d", ret);
		return ret;
	}

	shell_print(shell, "TX descriptor initialized");
	ret = sdio_device_tx_submit(dev, SDIO_DEVICE_FUNC_2, sdio_desc);
	if (ret < 0) {
		shell_print(shell, "F2 TX submit failed: %d", ret);
		return ret;
	}
	shell_print(shell, "F2 TX DMA data init done");

	ret = sdio_wait_for_f2_enabled(shell, dev);
	if (ret < 0) {
		return ret;
	}

	ret = sdio_device_func_ready(dev, SDIO_DEVICE_FUNC_2);
	if (ret < 0) {
		shell_print(shell, "F2 enable failed: %d", ret);
		return ret;
	}
	shell_print(shell, "F2 ready set");

	return sdio_wait_for_dma_done(shell, "TX", sdio_device_get_tx_status,
				      sdio_device_get_tx_error, dev);
}

static bool heap_allocation_in_sram_heap(const void *ptr, size_t size)
{
	uintptr_t start = DT_REG_ADDR(DT_NODELABEL(sram_heap0));
	uintptr_t end = start + DT_REG_SIZE(DT_NODELABEL(sram_heap0));
	uintptr_t address = (uintptr_t)ptr;

	return address >= start && address <= end && size <= end - address;
}

static int heap_test(const struct shell *shell, size_t argc, char **argv)
{
	const size_t malloc_size = 64;
	const size_t calloc_count = 16;
	const size_t calloc_size = sizeof(uint32_t);
	uint8_t *k_malloc_buf;
	uint32_t *k_calloc_buf;
	uint8_t *malloc_buf;
	uint32_t *calloc_buf;
	int ret = 0;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_malloc_buf = k_malloc(malloc_size);
	k_calloc_buf = k_calloc(calloc_count, calloc_size);
	malloc_buf = malloc(malloc_size);
	calloc_buf = calloc(calloc_count, calloc_size);
	shell_print(shell, "k_malloc=%p, k_calloc=%p", k_malloc_buf, k_calloc_buf);
	shell_print(shell, "malloc=%p, calloc=%p", malloc_buf, calloc_buf);
	if (k_malloc_buf == NULL || k_calloc_buf == NULL || malloc_buf == NULL ||
	    calloc_buf == NULL) {
		shell_error(shell, "One or more heap allocations failed");
		ret = -ENOMEM;
		goto out;
	}

	if (!heap_allocation_in_sram_heap(k_malloc_buf, malloc_size) ||
	    !heap_allocation_in_sram_heap(k_calloc_buf, calloc_count * calloc_size)) {
		shell_error(shell, "Kernel heap allocation is outside sram_heap0");
		ret = -ERANGE;
		goto out;
	}

	if (!heap_allocation_in_sram_heap(malloc_buf, malloc_size) ||
	    !heap_allocation_in_sram_heap(calloc_buf, calloc_count * calloc_size)) {
		shell_error(shell, "libc heap allocation is outside sram_heap0");
		ret = -ERANGE;
		goto out;
	}

	memset(k_malloc_buf, 0xA5, malloc_size);
	memset(malloc_buf, 0x5A, malloc_size);
	for (size_t i = 0; i < malloc_size; i++) {
		if (k_malloc_buf[i] != 0xA5) {
			shell_error(shell, "k_malloc memory read/write check failed");
			ret = -EIO;
			goto out;
		}
		if (malloc_buf[i] != 0x5A) {
			shell_error(shell, "malloc memory read/write check failed");
			ret = -EIO;
			goto out;
		}
	}

	for (size_t i = 0; i < calloc_count; i++) {
		if (k_calloc_buf[i] != 0) {
			shell_error(shell, "k_calloc did not zero its allocation");
			ret = -EIO;
			goto out;
		}
		if (calloc_buf[i] != 0) {
			shell_error(shell, "calloc did not zero its allocation");
			ret = -EIO;
			goto out;
		}
	}

	shell_print(shell, "Kernel and libc heap checks passed in sram_heap0");

out:
	free(calloc_buf);
	free(malloc_buf);
	k_free(k_calloc_buf);
	k_free(k_malloc_buf);
	return ret;
}

#if defined(CONFIG_TENSORFLOW_LITE_MICRO) && defined(CONFIG_ETHOS_U)
static int npu_test(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Starting TFLM Ethos-U NPU test");
	return npu_tflm_ethosu_test();
}
#endif

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
	SHELL_CMD(pwm_test, NULL,
		  "Use pwm_test for output test, PWM sweep for 0/25/50/75/100 duty cycle. "
		  "Use pwm_test capture <0|25|50|75|100> for input capture measurement. "
		  "Connect GPADC_IN5 directly to MCU_GPIO18",
		  pwm_test),
	SHELL_CMD(i2c_master_test, NULL,
		  "I2C0 master write/read test with external target at 0x42. "
		  "Wiring (Sabre is the slave): SRW1500 GPIO20 -> Sabre I2CS SDA, "
		  "GPIO21 -> Sabre I2CS SCL, Sabre GND -> SRW1500 GND",
		  i2c_master_test),
	SHELL_CMD(i2c_loopback_test, NULL,
		  "I2C single-board loopback test: I2C0 master <-> I2CS0 slave",
		  i2c_loopback_test),
	SHELL_CMD(spi_master_test, NULL, "Test spi master", spi_master_test),
	SHELL_CMD(spi_loopback_test, NULL,
		  "SPI0 wire loopback: connect GPIO2 (MOSI) directly to GPIO5 (MISO)",
		  spi_loopback_test),
	SHELL_CMD(timer_test, NULL, "Test timer callback", timer_test),
	SHELL_CMD(slowclk_timer_test, NULL, "Test slow clock timer callback", slowclk_timer_test),
	SHELL_CMD(watchdog_test, NULL, "Test watchdog callback", watchdog_test),
	SHELL_CMD(slowclk_watchdog_test, NULL, "Test slow clock watchdog callback", slowclk_watchdog_test),
	SHELL_CMD(flash_test, NULL, "Test flash erase, write, and read", flash_test),
	SHELL_CMD(flash_rww_irq_test, NULL, "Test RWW XIP execution from timer IRQ", flash_rww_irq_test),
	SHELL_CMD(flash_rww_task_switch_test, NULL, "Test RWW task switch into XIP fetch window",
		  flash_rww_task_switch_test),
	SHELL_CMD(cache_benchmark, NULL, "Benchmark XSPI reads through the OTF cache", cache_benchmark_test),
#if defined(CONFIG_FILE_SYSTEM_LITTLEFS)
	SHELL_CMD(littlefs_test, NULL, "Test RAM filesystem", littlefs_test),
#elif defined(CONFIG_FAT_FILESYSTEM_ELM)
	SHELL_CMD(ramfs_test, NULL, "Test RAM filesystem", ramfs_test),
#endif
	SHELL_CMD(sdio_f2_rx_test, NULL, "Receive 2 KiB from host via SDIO F2", sdio_f2_rx_test),
	SHELL_CMD(sdio_f2_tx_test, NULL, "Send 2 KiB to host via SDIO F2", sdio_f2_tx_test),
	SHELL_CMD(heap_test, NULL, "Test kernel and libc heap allocations in sram_heap0", heap_test),
#if defined(CONFIG_TENSORFLOW_LITE_MICRO) && defined(CONFIG_ETHOS_U)
	SHELL_CMD(npu_test, NULL, "Run TFLM Ethos-U NPU model test", npu_test),
#endif
	SHELL_CMD(trng_test, NULL, "Test trng", trng_test),
	SHELL_SUBCMD_SET_END /* Array terminator */
);

SHELL_CMD_REGISTER(tests, &tests, "srw1500 peripheral tests", NULL);

int main(void)
{
#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
	int ret = srw1500_usb_debug_init();

	if (ret < 0) {
		printk("USB debug initialization failed: %d\n", ret);
	}
#endif

	k_thread_create(&user_thread, user_stack, USER_STACKSIZE,
			user_function, NULL, NULL, NULL,
			-1, K_USER, K_MSEC(0));

	return 0;
}
