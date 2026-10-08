/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

int main(void)
{
	/* Shell starts automatically via SYS_INIT.
	 * BT is initialised manually by the user with "bt init" from the shell.
	 * Nothing else needed here.
	 */
	return 0;
}
