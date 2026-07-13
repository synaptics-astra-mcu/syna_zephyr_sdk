.. zephyr:code-sample:: timer
   :name: ARM SSE timer

   Test ARM SSE timer driver instead of default Cortex-M SysTick timer

Overview
********

As an alternative to the default ARM Cortex-M SysTick timer, the SSE timer
driver can be enabled. It provides a higher resolution (64 bit vs 24 bit).
The timer is transparent to the application and can be tested with any Zephyr
sample.


Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -DDTC_OVERLAY_FILE=samples/drivers/timer/sr100_rdk_m55.overlay


Sample Output
*************

The LED will toggle every second.

.. code-block:: console

   *** Booting Zephyr OS build v4.4.1 ***
   LED state: OFF
   LED state: ON
   LED state: OFF
   LED state: ON
   LED state: OFF
   LED state: ON
   LED state: OFF
   LED state: ON
