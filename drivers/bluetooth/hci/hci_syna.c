/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Zephyr BT HCI UART vendor setup driver for Synaptics SYNA.
 *
 * Implements bt_h4_vnd_setup() called by the Zephyr H:4 HCI UART driver
 * (h4.c) when CONFIG_BT_HCI_SETUP=y.  The full sequence mirrors brcm_patchram_plus
 * and btapp_mfg:
 *
 *   [115200]  1. Assert BT_REGON (GPIO25) and wait for chip startup
 *   [115200]  2. HCI_Reset
 *   [115200]  3. VS_UPDATE_UART_BAUDRATE → 3 Mbps  (chip acks at 115200, then switches)
 *   [115200→3M] 4. Host UART changes to 3 Mbps
 *   [3M    ]  5. DOWNLOAD_MINIDRIVER
 *   [3M    ]  6. WRITE_RAM loop (patchram records)
 *   [3M    ]  7. LAUNCH_RAM (last patchram record — chip reboots to 115200)
 *   [3M→115200] 8. Host UART changes back to 115200
 *   [115200]  9. Wait for chip reboot (stabilisation delay)
 *   [115200] 10. HCI_Reset
 *   [115200] 11. VS_UPDATE_UART_BAUDRATE → 3 Mbps again
 *   [115200→3M] 12. Host UART changes to 3 Mbps
 *   [3M    ]     BT HOST stack continues at 3 Mbps
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/drivers/bluetooth.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/byteorder.h>

#define LOG_LEVEL CONFIG_BT_HCI_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(syna_bt_hci);

#include "syna_hci_snoop.h"

#define DT_DRV_COMPAT syna_bt_hci_uart

/* Time after asserting REGON before sending HCI commands */
#define BT_POWER_ON_SETTLING_TIME_MS   500
/* Time to settle after baudrate switch before sending next command */
#define BT_BAUD_SWITCH_DELAY_MS        50
/* Time to wait after LAUNCH_RAM for chip to reboot */
#define BT_STABILIZATION_DELAY_MS      250

/* SYNA patchram (BRCM HCD format) exported from the .c firmware file */
extern const uint8_t  syna_patchram[];
extern const int      syna_patch_ram_length;

/* Vendor-specific HCI opcodes (same as BRCM family) */
#define BT_HCI_VND_OP_DOWNLOAD_MINIDRIVER  BT_OP(BT_OGF_VS, 0x002E)
#define BT_HCI_VND_OP_WRITE_RAM            BT_OP(BT_OGF_VS, 0x004C)
#define BT_HCI_VND_OP_LAUNCH_RAM           BT_OP(BT_OGF_VS, 0x004E)
/* VS_UPDATE_UART_BAUDRATE: opcode 0xFC18 */
#define BT_HCI_VND_OP_UPDATE_BAUDRATE      BT_OP(BT_OGF_VS, 0x0018)

/* ── UART baudrate helpers ────────────────────────────────────────────────── */

/*
 * Change the host-side UART baudrate using Zephyr's runtime-configure API.
 * Must be called AFTER the chip has already acknowledged the baudrate switch.
 *
 * NOTE: uart_ns16550_configure() writes IER=0x00 at the end, disabling all
 * UART interrupts.  We must re-enable the RX interrupt so the H4 driver can
 * continue to receive HCI events at the new baudrate.
 */
static int uart_set_baudrate(const struct device *uart, uint32_t baudrate)
{
	struct uart_config cfg = {0};
	int err;

	err = uart_config_get(uart, &cfg);
	if (err) {
		LOG_ERR("uart_config_get failed: %d", err);
		return err;
	}

	cfg.baudrate = baudrate;
	err = uart_configure(uart, &cfg);
	if (err) {
		LOG_ERR("uart_configure(%u) failed: %d", baudrate, err);
		return err;
	}

	/* Re-enable RX interrupt: uart_configure() clears IER (NS16550 quirk) */
	uart_irq_rx_enable(uart);

	LOG_INF("Host UART baudrate -> %u bps", baudrate);
	return 0;
}

/*
 * Send VS_UPDATE_UART_BAUDRATE HCI command.
 *
 * Parameters (6 bytes):
 *   [0..1] = 0x00, 0x00  (legacy encoded-baud field, unused in extended mode)
 *   [2..5] = baudrate as LE32
 *
 * The chip acknowledges at the CURRENT baudrate and then immediately switches.
 * The caller must change the host UART AFTER this returns.
 */
static int hci_set_baudrate(uint32_t baudrate)
{
	struct net_buf *buf;
	int err;

	buf = bt_hci_cmd_alloc(K_FOREVER);
	if (!buf) {
		return -ENOMEM;
	}

	net_buf_add_u8(buf, 0x00);        /* encoded_baud[0] */
	net_buf_add_u8(buf, 0x00);        /* encoded_baud[1] */
	net_buf_add_le32(buf, baudrate);  /* target baudrate LE32 */

	err = bt_hci_cmd_send_sync(BT_HCI_VND_OP_UPDATE_BAUDRATE, buf, NULL);
	if (err) {
		LOG_ERR("VS_UPDATE_UART_BAUDRATE(%u) HCI cmd failed: %d", baudrate, err);
	}
	return err;
}

/* ── Patchram download ────────────────────────────────────────────────────── */

/*
 * Download the SYNA patchram firmware.
 *
 * The BRCM HCD format is a sequence of HCI command records:
 *   [opcode: 2 bytes LE] [length: 1 byte] [data: length bytes]
 * terminated by a LAUNCH_RAM opcode that boots the new firmware.
 *
 * Sequence (mirrors btapp_mfg / brcm_patchram_plus):
 *   1. VS_UPDATE_UART_BAUDRATE(fast)  — chip acks at 115200, switches to fast
 *   2. Host UART → fast baud
 *   3. DOWNLOAD_MINIDRIVER            — at fast baud
 *   4. WRITE_RAM × N + LAUNCH_RAM    — at fast baud (LAUNCH_RAM is last record)
 *   5. Host UART → 115200            — chip reboots to default
 */
static int bt_firmware_download(const struct device *uart)
{
	const uint8_t *data = syna_patchram;
	uint32_t remaining = (uint32_t)syna_patch_ram_length;
	const uint32_t fast_baud = CONFIG_BT_HCI_SYNA_BAUD_RATE;
	struct net_buf *buf;
	int err;

	LOG_INF("Downloading SYNA patchram (%d bytes) via baudrate switch to %u",
		syna_patch_ram_length, fast_baud);

	/* Switch chip + host to fast baud before download */
	err = hci_set_baudrate(fast_baud);
	if (err) {
		return err;
	}

	err = uart_set_baudrate(uart, fast_baud);
	if (err) {
		return err;
	}

	k_msleep(BT_BAUD_SWITCH_DELAY_MS);

	/* DOWNLOAD_MINIDRIVER — tells chip to expect patchram */
	err = bt_hci_cmd_send_sync(BT_HCI_VND_OP_DOWNLOAD_MINIDRIVER, NULL, NULL);
	if (err) {
		LOG_ERR("DOWNLOAD_MINIDRIVER failed: %d", err);
		return err;
	}

	/* Suppress snoop output during WRITE_RAM flood */
	hci_snoop_pause();

	/* Send all patchram records (WRITE_RAM × N then LAUNCH_RAM) */
	while (remaining > 0) {
		uint16_t op_code = sys_get_le16(data);
		uint8_t  data_len = data[2];

		if (remaining < (uint32_t)(data_len + 3)) {
			LOG_ERR("Truncated patchram record at opcode 0x%04x", op_code);
			return -EINVAL;
		}

		if (data_len > CONFIG_BT_BUF_CMD_TX_SIZE) {
			LOG_ERR("Patchram record 0x%04x param_len %d > CONFIG_BT_BUF_CMD_TX_SIZE=%d",
				op_code, data_len, CONFIG_BT_BUF_CMD_TX_SIZE);
			return -ENOMEM;
		}

		buf = bt_hci_cmd_alloc(K_FOREVER);
		if (!buf) {
			LOG_ERR("Unable to allocate HCI buffer");
			return -ENOMEM;
		}

		net_buf_add_mem(buf, &data[3], data_len);

		err = bt_hci_cmd_send_sync(op_code, buf, NULL);
		if (err) {
			LOG_ERR("Patchram record opcode 0x%04x failed: %d", op_code, err);
			return err;
		}

		data      += data_len + 3;
		remaining -= data_len + 3;
	}

	LOG_INF("Patchram download complete (LAUNCH_RAM sent at %u bps)", fast_baud);

	/* Re-enable snoop now that the WRITE_RAM flood is over */
	hci_snoop_resume();

	/* Chip reboots after LAUNCH_RAM and resets UART to 115200 */
	err = uart_set_baudrate(uart, 115200);
	if (err) {
		return err;
	}

	return 0;
}

/* ── bt_h4_vnd_setup entry point ─────────────────────────────────────────── */

/*
 * Called by h4.c before the BT HOST stack sends its first HCI_Reset.
 * CONFIG_BT_HCI_SETUP=y must be set.
 */
int bt_h4_vnd_setup(const struct device *dev, const struct bt_hci_setup_params *params)
{
	const uint32_t fast_baud = CONFIG_BT_HCI_SYNA_BAUD_RATE;
	int err;

	ARG_UNUSED(params);

	if (!device_is_ready(dev)) {
		LOG_ERR("BT UART device not ready");
		return -EINVAL;
	}

#if DT_INST_NODE_HAS_PROP(0, bt_reg_on_gpios)
	struct gpio_dt_spec bt_reg_on = GPIO_DT_SPEC_INST_GET(0, bt_reg_on_gpios);

	if (!gpio_is_ready_dt(&bt_reg_on)) {
		LOG_ERR("BT_REGON GPIO not ready");
		return -EIO;
	}

	err = gpio_pin_configure_dt(&bt_reg_on, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Failed to configure BT_REGON: %d", err);
		return err;
	}

	/* Brief LOW before asserting to discharge any residual */
	k_msleep(50);

	err = gpio_pin_set_dt(&bt_reg_on, 1);
	if (err) {
		LOG_ERR("Failed to assert BT_REGON: %d", err);
		return err;
	}

	LOG_DBG("BT_REGON asserted");
#endif

	k_msleep(BT_POWER_ON_SETTLING_TIME_MS);

	/* Initial HCI_Reset at 115200 to bring controller to known state */
	err = bt_hci_cmd_send_sync(BT_HCI_OP_RESET, NULL, NULL);
	if (err) {
		LOG_ERR("Initial HCI_Reset failed: %d", err);
		return err;
	}

	/* Download patchram:
	 *   switch to fast baud → download → revert host to 115200 */
	err = bt_firmware_download(dev);
	if (err) {
		LOG_ERR("Patchram download failed: %d", err);
		return err;
	}

	/* Wait for chip to reboot with new firmware (UART now 115200 both sides) */
	k_msleep(BT_STABILIZATION_DELAY_MS);

	/* Post-patchram HCI_Reset at 115200 */
	err = bt_hci_cmd_send_sync(BT_HCI_OP_RESET, NULL, NULL);
	if (err) {
		LOG_ERR("Post-patchram HCI_Reset failed: %d", err);
		return err;
	}

	/* Switch to fast baud for normal operation */
	err = hci_set_baudrate(fast_baud);
	if (err) {
		return err;
	}

	err = uart_set_baudrate(dev, fast_baud);
	if (err) {
		return err;
	}

	LOG_INF("SYNA initialized, HCI running at %u bps", fast_baud);
	return 0;
}

