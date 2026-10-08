/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT arm_apb_wdt

#define WDOGLOAD	0x00
#define WDOGVALUE	0x04
#define WDOGCONTROL	0x08
#define WDOGINTCLR	0x0c
#define WDOGRIS		0x10
#define WDOGMIS		0x14

#define WDOGCTRL_RESEN	BIT(1)
#define WDOGCTRL_INTEN	BIT(0)

#include <soc.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/reboot.h>

struct arm_apb_wdt_dev_config {
	mem_addr_t base;
	uint32_t clk_freq;
};

struct arm_apb_wdt_dev_data {
	wdt_callback_t cb;
	uint32_t timeout_ticks;
	uint8_t mode;
	bool timeout_valid;
	bool is_serviced;
};

/* When the watchdog counter rolls over, the SCU will generate a pre-warning
 * event which gets routed to the ISR below. If the watchdog is not serviced,
 * the SCU will only reset the MCU after the second time the counter rolls
 * over. Hence the reset will only happen after 2*cfg->window.max have
 * elapsed. We could potentially manually reset the MCU, but this way the
 * context information (i.e. that reset happened because of a watchdog) is
 * lost.
 */
static void arm_apb_wdt_isr(const struct device *dev)
{
	const struct arm_apb_wdt_dev_config *config = dev->config;
	struct arm_apb_wdt_dev_data *data = dev->data;

	data->is_serviced = false;

	if (data->cb) {
		data->cb(dev, 0);
	}

	/* Ensure that watchdog is serviced if RESET_NONE mode is used */
	if (data->mode == WDT_FLAG_RESET_NONE && !data->is_serviced) {
		sys_write32(1, config->base + WDOGINTCLR);
		data->is_serviced = true;
	}

	/* If WDT_FLAG_RESET_CPU_CORE or WDT_FLAG_RESET_SOC is used, spin in a loop.
	 * The first event is triggered after config->window.max milliseconds, and
	 * the actual reset will occur after a second period of the same length
	 * passes.
	 */
	if (data->mode != WDT_FLAG_RESET_NONE && !data->is_serviced) {
		while (true) {
		}
	}
}

/* NMI on this platform is sourced from the watchdog expiry signal, so
 * realistically this should be the only NMI handler present on applicable
 * devices.
 */
static void arm_apb_wdt_nmi_handler(void)
{
	const struct device *device = DEVICE_DT_INST_GET(0);
	const struct arm_apb_wdt_dev_config *config = device->config;
	uint32_t status = sys_read32(config->base + WDOGMIS);

	if (status != 0) {
		arm_apb_wdt_isr(device);
	} else {
		while (true) {
		}
	}
}

static int arm_apb_wdt_disable(const struct device *dev)
{
	const struct arm_apb_wdt_dev_config *config = dev->config;
	struct arm_apb_wdt_dev_data *data = dev->data;

	/* Disable the reset interrupt */
	sys_clear_bits(config->base + WDOGCONTROL, WDOGCTRL_RESEN | WDOGCTRL_INTEN);
	data->timeout_valid = false;

	return 0;
}

static int arm_apb_wdt_setup(const struct device *dev, uint8_t options)
{
	const struct arm_apb_wdt_dev_config *config = dev->config;
	struct arm_apb_wdt_dev_data *data = dev->data;
	int rc = 0;

	ARG_UNUSED(options);

	if (data->timeout_valid == false) {
		rc = -EINVAL;
		goto exit;
	}

	/* Set timeout value */
	sys_write32(data->timeout_ticks, config->base + WDOGLOAD);
	/* Enable & Start the watchdog timer */
	sys_set_bits(config->base + WDOGCONTROL, WDOGCTRL_RESEN | WDOGCTRL_INTEN);

exit:
	return rc;
}

static int arm_apb_wdt_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	const struct arm_apb_wdt_dev_config *config = dev->config;
	struct arm_apb_wdt_dev_data *data = dev->data;
	int rc = 0;
	uint64_t timeout_ticks;

	/* disable the watchdog if timeout was already installed */
	if (data->timeout_valid) {
		arm_apb_wdt_disable(dev);
		data->timeout_valid = false;
	}

	if (cfg->window.min != 0U || cfg->window.max == 0U) {
		rc = -EINVAL;
		goto exit;
	}

	if (cfg->flags == WDT_FLAG_RESET_NONE && cfg->callback == NULL) {
		rc = -EINVAL;
		goto exit;
	}

	if (cfg->flags == WDT_FLAG_RESET_CPU_CORE) {
		rc = -EINVAL;
		goto exit;
	}

	/* Check if the timeout can be represented */
	timeout_ticks = (uint64_t)config->clk_freq * cfg->window.max;
	timeout_ticks /= 1000;
	if (timeout_ticks > UINT32_MAX) {
		rc = -EINVAL;
		goto exit;
	}

	data->cb = cfg->callback;
	data->mode = cfg->flags;
	data->timeout_valid = true;
	data->timeout_ticks = timeout_ticks;

exit:
	return rc;
}

static int arm_apb_wdt_feed(const struct device *dev, int channel_id)
{
	ARG_UNUSED(channel_id);
	const struct arm_apb_wdt_dev_config *config = dev->config;
	struct arm_apb_wdt_dev_data *data = dev->data;

	sys_write32(data->timeout_ticks, config->base + WDOGLOAD);
	data->is_serviced = true;

	return 0;
}

static const struct wdt_driver_api arm_apb_wdt_api = {
	.setup = arm_apb_wdt_setup,
	.disable = arm_apb_wdt_disable,
	.install_timeout = arm_apb_wdt_install_timeout,
	.feed = arm_apb_wdt_feed,
};

static int arm_apb_wdt_init(const struct device *dev)
{
	z_arm_nmi_set_handler(arm_apb_wdt_nmi_handler);

#ifdef CONFIG_WDT_DISABLE_AT_BOOT
	return 0;
#else
#ifdef CONFIG_WDT_ARM_APB_DEFAULT_TIMEOUT_MAX
	int ret;
	const struct wdt_timeout_cfg cfg = {.window.max = CONFIG_WDT_ARM_APB_DEFAULT_TIMEOUT_MAX,
					    .flags = WDT_FLAG_RESET_SOC};

	ret = arm_apb_wdt_install_timeout(dev, &cfg);
	if (ret < 0) {
		return ret;
	}

	return arm_apb_wdt_setup(dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
#else
	return 0;
#endif
#endif
}

#define ARM_APB_WDT_INIT(n)                                                                       \
	static struct arm_apb_wdt_dev_data arm_apb_wdt_data;                                      \
	static const struct arm_apb_wdt_dev_config arm_apb_wdt_config = {                         \
		.base = DT_INST_REG_ADDR(n),                                                      \
		.clk_freq = DT_INST_PROP(n, clock_frequency),                                     \
	};                                                                                        \
	DEVICE_DT_INST_DEFINE(n, arm_apb_wdt_init, NULL, &arm_apb_wdt_data, &arm_apb_wdt_config,  \
			      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &arm_apb_wdt_api);

DT_INST_FOREACH_STATUS_OKAY(ARM_APB_WDT_INIT)
