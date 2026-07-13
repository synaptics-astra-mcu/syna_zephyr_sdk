.. zephyr:code-sample:: m4
   :name: Cortex-M4 & M55 parallel execution

   Run firmware images on both cores M4 & M55 of the SR110.

Overview
********

To retrieve messages from both cores M4 & M55 two UART connections are required on the RDK board
and two terminal sessions on the host (minicom, putty, etc.).

Using the build instructions below, you should see the hello-world print on the M55 UART, while
the M4 UART shows the interactive shell of Zephyr. Of course other applications can run on each of
the cores.


Wiring
******

Connect two UART-to-USB converters to the pins 13 + 14 on J24 (+ GND) for UART0 (M4) and 13 + 14
on J25 (+ GND) for UART1 (M55), so there is a dedicated UART connection for each core.

Please note that the RDK board might not boot up from flash if the LP-UART at pins 8 + 9 on J24 is
used, as there is a conflict with the boot straps. Therefore, UART0 is chosen for the M4. There is
no interrupt for UART0 & 1 on the M4, so ``CONFIG_UART_INTERRUPT_DRIVEN`` is only enabled for the
M55 in the default configuration. The LP-UART on the M4 supports an interrupt, so you can enable
``CONFIG_UART_INTERRUPT_DRIVEN`` if the LP-UART is used instead.


Building and Running
********************

Firmware for M4:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/shell/shell_module
   :board: sr100_rdk/sr100/m4
   :goals: build
   :west-args: -d build/m4

Firmware for M55:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -d build/m55 -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD=../../build/m4

The M4 firmware might print the error messages below. These can be ignored, as the I2C bus and
the GPIO expander are already initialized and controlled by the M55 firmware:

.. code-block:: console

   [00:00:00.101,000] <err> pcal64xxa: unable to write to register 0x05, error -116
   [00:00:00.101,000] <err> pcal64xxa: failed to reset register 05: -116
   [00:00:00.101,000] <err> pcal64xxa: pca6416@20: failed to apply reset state
