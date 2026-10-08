.. zephyr:code-sample:: emmc
   :name: eMMC
   :relevant-api: sdhc

   Detect the eMMC on the SDL261x-RDK board and run read & write tests.


Overview
********

The ``sdhc`` sample accesses the eMMC on the SL261x RDK board and runs
read & write tests. The sectors that are written should be in an area not
used by u-boot or Linux.


Building and Running
********************

Build the firmware image for ``samples/sdhc`` for the SL261x-M52:

.. zephyr-app-commands::
   :zephyr-app: samples/sdhc
   :board: sl2619_rdk/sl2619/m52
   :goals: build


Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.4.1 ***
   Hello World! sl2619_rdk/sl2619/m52
   card.flags 0x62, width 8
   SD card reports sector count of 62160896 (ret 0)
   SD card reports sector size of 512 (ret 0)
   read 1 block from block offset 49152 (0): 0x3020100 0x7060504 0xb0a0908 0xf0e0d0c
   write 2 blocks at block offset 49152: 0
   read 1 block from block offset 49152 (0): 0x0 0x0 0x0 0x0
   write 2 blocks at block offset 49152: 0
   read 2 blocks from block offset 49152 (0): 0x3020100 0x7060504 0xb0a0908 0xf0e0d0c
