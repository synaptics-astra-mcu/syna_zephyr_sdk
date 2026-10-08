#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

void main(void)
{
    printk("Initializing SDIO application...\n");

    while (1) {
        k_msleep(1000);
    }
}