#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------ Init hook ----------------------------- */
/* Your fork’s SYS_INIT expects: int (*)(void) */
static int prompt_init(void)
{
    char prompt[32];
#if CONFIG_CPU_CORTEX_M85
    /* "zephyr,arch-cpu$ " */
    snprintk(prompt, sizeof(prompt), "syna,m85:~$ ");
#elif CONFIG_CPU_CORTEX_M52
    snprintk(prompt, sizeof(prompt), "syna,m52:~$ ");
#else
    return 0;
#endif

#ifdef CONFIG_SHELL
    const struct shell *sh = shell_backend_uart_get_ptr();

    if (sh) {
        shell_prompt_change(sh, prompt);
    }
#endif

    return 0;
}

SYS_INIT(prompt_init, APPLICATION, 0);
