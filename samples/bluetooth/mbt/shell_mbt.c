/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "btapp_mfg.h"
#include "upio.h"

/* Forward declaration for internal btapp function used by ble_rx */
int8_t bt_mfg_send_hci(uint8_t *cmd, uint8_t len, uint8_t *res, uint8_t res_len);

/* ---------- helper: convert Zephyr shell argv to btapp-style argv ----------
 * btapp functions expect argv[0] to be the first *parameter* (not the cmd
 * name), so we pass (argc-1) and &argv[1] from the shell handler.
 */

/* mbt init */
static int cmd_mbt_init(const struct shell *sh, size_t argc, char *argv[])
{
    ARG_UNUSED(sh);
    int8_t ret = bt_mfg_init(0, NULL);
    if (ret != 0) {
        shell_error(sh, "mbt init failed: %d", ret);
    }
    return ret;
}

/* mbt deinit */
static int cmd_mbt_deinit(const struct shell *sh, size_t argc, char *argv[])
{
    ARG_UNUSED(sh);
    int8_t ret = bt_mfg_deinit(0, NULL);
    if (ret != 0) {
        shell_error(sh, "mbt deinit failed: %d", ret);
    }
    return ret;
}

/* mbt reset */
static int cmd_mbt_reset(const struct shell *sh, size_t argc, char *argv[])
{
    ARG_UNUSED(sh);
    int8_t ret = bt_hci_reset(0, NULL);
    if (ret != 0) {
        shell_error(sh, "mbt reset failed: %d", ret);
    }
    return ret;
}

/* mbt ble_tx <chan> <len> <pattern> */
static int cmd_mbt_ble_tx(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc < 4) {
        shell_error(sh, "Usage: mbt ble_tx <chan> <len> <pattern>");
        return -EINVAL;
    }
    /* pass 3 params: argv[1..3] */
    int8_t ret = bt_le_tx_test(3, (const char **)&argv[1]);
    if (ret != 0) {
        shell_error(sh, "ble_tx failed: %d", ret);
    }
    return ret;
}

/* mbt ble_rx <chan> */
static int cmd_mbt_ble_rx(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc < 2) {
        shell_error(sh, "Usage: mbt ble_rx <chan>");
        return -EINVAL;
    }
    /* ble_rx is not a separate btapp function — it maps to le_receiver_test.
     * Build the HCI LE Receiver Test command directly. */
    uint8_t hci_le_rx[] = {0x01, 0x1d, 0x20, 0x01, 0x00};
    uint8_t hci_le_rx_event[] = {0x04, 0x0e, 0x04, 0x01, 0x1d, 0x20, 0x00};
    char *end;
    long chan = strtol(argv[1], &end, 10);
    if (*end != '\0' || chan < 0 || chan > 39) {
        shell_error(sh, "Invalid channel (0-39)");
        return -EINVAL;
    }
    hci_le_rx[4] = (uint8_t)chan;

    /* Use hci_send_any style via bt_mfg_send_hci */
    int8_t ret = bt_mfg_send_hci(hci_le_rx, sizeof(hci_le_rx),
                                  hci_le_rx_event, sizeof(hci_le_rx_event));
    if (ret != 0) {
        shell_error(sh, "ble_rx failed: %d", ret);
    } else {
        shell_print(sh, "SUCCESS");
    }
    return ret;
}

/* mbt ble_enhanced_tx <chan> <len> <pattern> <phy> */
static int cmd_mbt_ble_enhanced_tx(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc < 5) {
        shell_error(sh, "Usage: mbt ble_enhanced_tx <chan> <len> <pattern> <phy>");
        return -EINVAL;
    }
    int8_t ret = bt_le_enhanced_tx_test(4, (const char **)&argv[1]);
    if (ret != 0) {
        shell_error(sh, "ble_enhanced_tx failed: %d", ret);
    }
    return ret;
}

/* mbt ble_enhanced_rx <chan> <phy> <mod_idx> */
static int cmd_mbt_ble_enhanced_rx(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc < 4) {
        shell_error(sh, "Usage: mbt ble_enhanced_rx <chan> <phy> <mod_idx>");
        return -EINVAL;
    }
    int8_t ret = bt_le_enhanced_rx_test(3, (const char **)&argv[1]);
    if (ret != 0) {
        shell_error(sh, "ble_enhanced_rx failed: %d", ret);
    }
    return ret;
}

/* mbt ble_test_end */
static int cmd_mbt_ble_test_end(const struct shell *sh, size_t argc, char *argv[])
{
    ARG_UNUSED(sh);
    int8_t ret = bt_le_test_end(0, NULL);
    if (ret != 0) {
        shell_error(sh, "ble_test_end failed: %d", ret);
    }
    return ret;
}

/* mbt bt_tx <bdaddr> <freq> <mod> <logch> <pkttype> <pktlen> <power> */
static int cmd_mbt_bt_tx(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc < 8) {
        shell_error(sh, "Usage: mbt bt_tx <bdaddr> <freq> <mod> <logch> <pkttype> <pktlen> <power>");
        return -EINVAL;
    }
    int8_t ret = bt_radio_tx_test(7, (const char **)&argv[1]);
    if (ret != 0) {
        shell_error(sh, "bt_tx failed: %d", ret);
    }
    return ret;
}

/* mbt bt_rx <bdaddr> <freq> <mod> <logch> <pkttype> <pktlen> <period> */
static int cmd_mbt_bt_rx(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc < 8) {
        shell_error(sh, "Usage: mbt bt_rx <bdaddr> <freq> <mod> <logch> <pkttype> <pktlen> <period>");
        return -EINVAL;
    }
    int8_t ret = bt_radio_rx_test(7, (const char **)&argv[1]);
    if (ret != 0) {
        shell_error(sh, "bt_rx failed: %d", ret);
    }
    return ret;
}

/* mbt bt_test_end — same as ble_test_end (HCI LE Test End) */
static int cmd_mbt_bt_test_end(const struct shell *sh, size_t argc, char *argv[])
{
    ARG_UNUSED(sh);
    int8_t ret = bt_le_test_end(0, NULL);
    if (ret != 0) {
        shell_error(sh, "bt_test_end failed: %d", ret);
    }
    return ret;
}

/* mbt hci_send_any <hexstring> */
static int cmd_mbt_hci_send_any(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc < 2) {
        shell_error(sh, "Usage: mbt hci_send_any <hexstring>");
        return -EINVAL;
    }
    int8_t ret = bt_mfg_hci_send_any(1, (const char **)&argv[1]);
    if (ret != 0) {
        shell_error(sh, "hci_send_any failed: %d", ret);
    }
    return ret;
}

/* mbt uart_test — toggle BT_REGON then send raw test pattern on UART0 */
static int cmd_mbt_uart_test(const struct shell *sh, size_t argc, char *argv[])
{
    const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(ns16550_uart0));
    uint8_t test_bytes[] = { 0x55, 0xAA, 0x01, 0x03, 0x0C, 0x00 };
    int count = 16;

    if (!device_is_ready(uart_dev)) {
        shell_error(sh, "UART0 device not ready");
        return -ENODEV;
    }

    /* Power-cycle BT chip first — UART is only active after REGON HIGH */
    shell_print(sh, "REGON LOW...");
    UPIO_Init(NULL);
    UPIO_Set(UPIO_GENERAL, BT_REG_ON_GPIO, UPIO_OFF);
    k_msleep(50);
    shell_print(sh, "REGON HIGH...");
    UPIO_Set(UPIO_GENERAL, BT_REG_ON_GPIO, UPIO_ON);
    k_msleep(50);

    struct uart_config cfg = {
        .baudrate  = 115200,
        .parity    = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };
    int rc = uart_configure(uart_dev, &cfg);
    if (rc != 0) {
        shell_error(sh, "uart_configure failed: %d", rc);
        return rc;
    }

    shell_print(sh, "Sending %d x test pattern on UART0 (115200 baud, no flow ctrl)...", count);
    for (int i = 0; i < count; i++) {
        for (size_t j = 0; j < sizeof(test_bytes); j++) {
            uart_poll_out(uart_dev, test_bytes[j]);
        }
        k_msleep(10);
    }
    shell_print(sh, "Done. Check LA on UART0 TX pin.");
    return 0;
}

/* Register mbt subcommands */
SHELL_STATIC_SUBCMD_SET_CREATE(mbt_cmds,
    SHELL_CMD_ARG(init,          NULL, "Init BT (download FW)",           cmd_mbt_init,          1, 0),
    SHELL_CMD_ARG(deinit,        NULL, "Deinit BT",                       cmd_mbt_deinit,        1, 0),
    SHELL_CMD_ARG(reset,         NULL, "HCI Reset",                       cmd_mbt_reset,         1, 0),
    SHELL_CMD_ARG(ble_tx,        NULL, "BLE TX test <chan> <len> <pat>",  cmd_mbt_ble_tx,        4, 0),
    SHELL_CMD_ARG(ble_rx,        NULL, "BLE RX test <chan>",              cmd_mbt_ble_rx,        2, 0),
    SHELL_CMD_ARG(ble_enhanced_tx, NULL, "BLE enh TX <chan> <len> <pat> <phy>", cmd_mbt_ble_enhanced_tx, 5, 0),
    SHELL_CMD_ARG(ble_enhanced_rx, NULL, "BLE enh RX <chan> <phy> <mod>", cmd_mbt_ble_enhanced_rx, 4, 0),
    SHELL_CMD_ARG(ble_test_end,  NULL, "End BLE test",                    cmd_mbt_ble_test_end,  1, 0),
    SHELL_CMD_ARG(bt_tx,         NULL, "BR/EDR TX test <bdaddr> ...",     cmd_mbt_bt_tx,         8, 0),
    SHELL_CMD_ARG(bt_rx,         NULL, "BR/EDR RX test <bdaddr> ...",     cmd_mbt_bt_rx,         8, 0),
    SHELL_CMD_ARG(bt_test_end,   NULL, "End BR/EDR test",                 cmd_mbt_bt_test_end,   1, 0),
    SHELL_CMD_ARG(hci_send_any,  NULL, "Send raw HCI <hexstring>",        cmd_mbt_hci_send_any,  2, 0),
    SHELL_CMD_ARG(uart_test,     NULL, "Send raw test bytes on UART0",    cmd_mbt_uart_test,     1, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mbt, &mbt_cmds, "Bluetooth manufacturing test commands", NULL);
