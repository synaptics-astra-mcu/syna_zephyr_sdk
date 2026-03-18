/*
 * Copyright (c) 2026, Synaptics, Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
*/

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#if CONFIG_ASOC
#include <asoc.h>
#endif
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/device.h>
#include <zephyr/shell/shell.h>


#include <zephyr/shell/shell.h>
#if defined(CONFIG_SHELL_BACKEND_SERIAL)
#include <zephyr/shell/shell_uart.h>
#endif
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

static const struct device *get_console_uart(void)
{
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)
    return DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#else
    return NULL;
#endif
}

void amp_stop_shell_and_uart(void)
{
    /* 1) Stop shell so it won't read or print anymore */
#if defined(CONFIG_SHELL) && defined(CONFIG_SHELL_BACKEND_SERIAL)
    const struct shell *sh = NULL;
    /* This getter exists in many Zephyr releases. If it's missing in yours,
       you can skip this block or store the shell* at init time and reuse it. */
    extern const struct shell *shell_backend_uart_get_ptr(void);
    sh = shell_backend_uart_get_ptr();
    if (sh) {
        shell_stop(sh);          /* portable replacement for shell_suspend() */
    }
#endif

    const struct device *uart = get_console_uart();
    if (!uart || !device_is_ready(uart)) {
        return; /* Nothing else to do */
    }

    /* 3) Quiesce RX/TX depending on driver API */
#if defined(CONFIG_UART_ASYNC_API)
    /* Stop DMA/IRQ RX immediately for async drivers */
    (void)uart_rx_disable(uart);
#elif defined(CONFIG_UART_INTERRUPT_DRIVEN)
    /* For interrupt-driven API, disable RX/TX IRQs explicitly */
    uart_irq_rx_disable(uart);
    uart_irq_tx_disable(uart);
#endif


}

static int cmd_boota55(const struct shell *sh, size_t argc, char *argv[])
{

#if CONFIG_ASOC
    shell_print(sh, "Booting A55...\n");
    amp_stop_shell_and_uart();
    k_sleep(K_MSEC(1000)); /* Give some time for the shell to stop and UART to quiesce */
    release_asoc(0xE2300000);
#else
    shell_print(sh, "ASOC not enabled, cannot boot A55");
#endif

	return 0;
}

#define HELP_NONE "[none]"

SHELL_STATIC_SUBCMD_SET_CREATE(
	syna_cmds,
	SHELL_CMD_ARG(boot_a55, NULL, HELP_NONE, cmd_boota55, 1, 0),
	SHELL_SUBCMD_SET_END);

static int cmd_default_handler(const struct shell *sh, size_t argc, char **argv)
{
	shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);

	return -EINVAL;
}

SHELL_CMD_REGISTER(syna, &syna_cmds, "syna cmds", cmd_default_handler);