/****************************************************************************
**
**  Name:          pf_trans_sr110.c
**
**  Description:   Platform transport (HCI UART) for SR110 on Zephyr RTOS
**
**
**  Copyright (c) 2019-2024, Synaptics, All Rights Reserved.
**  Synaptics Bluetooth Core. Proprietary and confidential.
******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "pf_trans.h"

#define debug_printf(...) printf(__VA_ARGS__)

/* BT HCI UART = uart0 */
#define BT_UART_NODE DT_NODELABEL(ns16550_uart0)

static const struct device *bt_uart_dev;
static uint8_t pf_trans_status = PF_TRANS_STATUS_STOP;

static struct k_mutex s_tx_mutex;
static struct k_mutex s_rx_mutex;

int pf_trans_init(uint32_t baud)
{
    int rc;

    debug_printf("Initializing BT UART transport at %u baud\n", baud);

    bt_uart_dev = DEVICE_DT_GET(BT_UART_NODE);
    if (!device_is_ready(bt_uart_dev)) {
        debug_printf("BT UART device not ready\n");
        return -1;
    }

    /*
     * Use no hardware flow control in polling mode. The poll_in/poll_out
     * approach paces data naturally, so AFCE (MCR auto flow control) is not
     * needed.  Enabling AFCE without verifying the CTS pin state at the UART
     * core level causes TX to be gated and no bytes reach the wire.
     */
    struct uart_config cfg = {
        .baudrate  = baud,
        .parity    = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    rc = uart_configure(bt_uart_dev, &cfg);
    if (rc != 0) {
        debug_printf("BT UART configure failed: %d\n", rc);
        return rc;
    }

    k_mutex_init(&s_tx_mutex);
    k_mutex_init(&s_rx_mutex);

    pf_trans_status = PF_TRANS_STATUS_READY;
    debug_printf("BT UART transport initialized successfully\n");
    return 0;
}

void pf_trans_deinit(void)
{
    debug_printf("Deinitializing BT UART transport\n");
    pf_trans_status = PF_TRANS_STATUS_STOP;
}

uint8_t pf_trans_get_status(void)
{
    return pf_trans_status;
}

void pf_trans_reconfig_baud(uint32_t baud)
{
    int rc;

    debug_printf("Reconfiguring UART baud rate to %u\n", baud);

    struct uart_config cfg = {
        .baudrate  = baud,
        .parity    = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    rc = uart_configure(bt_uart_dev, &cfg);
    if (rc != 0) {
        debug_printf("Failed to reconfigure baud rate: %d\n", rc);
    } else {
        debug_printf("Baud rate reconfigured to %u\n", baud);
    }
}

int32_t pf_trans_send(const uint8_t *p_data, uint32_t length)
{
    if (length == 0 || p_data == NULL) {
        return -1;
    }
    if (pf_trans_status != PF_TRANS_STATUS_READY) {
        debug_printf("UART transport not ready\n");
        return -1;
    }

    k_mutex_lock(&s_tx_mutex, K_FOREVER);
    for (uint32_t i = 0; i < length; i++) {
        uart_poll_out(bt_uart_dev, p_data[i]);
    }
    k_mutex_unlock(&s_tx_mutex);

    return (int32_t)length;
}

int32_t pf_trans_receive(unsigned char *p_data, unsigned int length, unsigned int timeout_ms)
{
    if (length == 0 || p_data == NULL) {
        return -1;
    }
    if (pf_trans_status != PF_TRANS_STATUS_READY) {
        debug_printf("UART transport not ready\n");
        return -1;
    }

    k_mutex_lock(&s_rx_mutex, K_FOREVER);

    uint32_t received = 0;
    uint32_t deadline_ms = k_uptime_get_32() + timeout_ms;

    while (received < length) {
        unsigned char c;
        int rc = uart_poll_in(bt_uart_dev, &c);
        if (rc == 0) {
            p_data[received++] = c;
        } else {
            /* No byte available yet — check timeout */
            if (k_uptime_get_32() >= deadline_ms) {
                break;
            }
            k_sleep(K_USEC(100));
        }
    }

    k_mutex_unlock(&s_rx_mutex);

    return (received == length) ? (int32_t)received : -1;
}