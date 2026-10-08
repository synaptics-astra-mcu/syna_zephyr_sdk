.. zephyr:code-sample:: helium
   :name: CMSIS-DSP Helium support

   Use the CMSIS-DSP library to calculate the moving average of a signal.

Overview
********

This sample demonstrates how to use the CMSIS-DSP library to calculate the moving average of a
signal.

See ``zephyr/samples/modules/cmsis_dsp/moving_average/README.rst``.

Building and Running
*********************

The demo can be built as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/modules/cmsis_dsp/moving_average
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -DCONFIG_FPU=y

There is one more option that can be enabled: ``CONFIG_CMSIS_DSP_HELIUM_EXPERIMENTAL``. However,
it did not change the compilation of the moving_average sample.

The sample reported an execution time of 696 cycles with FPU/Helium vs. 2088 cycles without.
