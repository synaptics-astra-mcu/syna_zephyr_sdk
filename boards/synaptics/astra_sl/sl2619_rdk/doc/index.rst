.. zephyr:board:: sl2619_rdk

Overview
********

Information about Astra SL series and the evaluation board can be found
at the `Synaptics website`_.

* The Astra Machina SL261X Evaluation Platform Kit features the following
  key components:
    * Single and dual core Arm Cortex-A55 SoC domain
    * Arm Cortex-M52 with Helium System Manager domain
    * 1 TOPS Transformer-capable NPU
    * 3D GPU with Arm Mali-G31
    * MIPI-DSI & CSI with 2160p30 and HDR
    * 3x TDM/I2S with 16 channels, support for 8 digital mics
    * Hardware audio and camera mute
    * 2 USB-2.0, 2 SDIO 3.0, 4 TWSI I2C, 8 UART
    * 5 SPI, 2 xSPI, 99 GPIO
    * 12-bit ADC and up to 12 smart PWM modules in SM domain
    * DDR4/LPDDR4/DDR3L with inline ECC
    * PSA certified Level 3 (RoT), Level 2 (Product)
    * Secure boot, TRNG, RSA, AWS, SHA, ECC, HASH
    * Single and Dual GbE RGMII with Wake-on-LAN
    * 1588 PTP and TSN, 802.1 p/q VLAN tagging

.. image:: img/sl261x_rdk_board.png
     :align: center
     :alt: SL261X Astra Machina Eval Platform

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

A detailed description on the hardware connections, IOs and peripherals can be found on the
`Synaptics Astra Machina Eval platform website`_

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The Synaptics SL261X SoC needs to be eMMC flashed prior to running a Zephyr application. This can be
achieved by following Synaptics Astra Machina Eval platform instructions.

Initialization
==============

Install the Zephyr SDK (version 0.17.4) by following the
`Zephyr Getting Started Guide <https://docs.zephyrproject.org/latest/develop/getting_started/index.html>`_.

The first step is to initialize the workspace folder (``my-workspace``) where
the example application and all Zephyr modules will be cloned. Run the following
command:

.. code-block:: console

   west init -m https://github.com/syna-eepd/zephyr_srsdk --mr main my-workspace

   # update Zephyr modules
   cd my-workspace
   west update

For GICV2, please merge the following upstream PR in the zephyr repository:

``https://github.com/zephyrproject-rtos/zephyr/pull/105253``

Building
========

Before building, set the toolchain environment for each target.

**A55 DDRLESS image**

Set the Zephyr SDK toolchain and build the A55 image:

.. code-block:: console

   cd my-workspace/zephyr

   export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
   export ZEPHYR_SDK_INSTALL_DIR=<path/to/zephyr-sdk-0.17.4>

   west build -b sl2619_rdk/sl2619/a55/ddrless \
       samples/subsys/shell/shell_module \
       -p auto -d build/a55_image

**M52 bootloader image (with A55 image bundled)**

Switch to the GNU Arm Embedded toolchain and build the M52 image, passing
the previously built A55 output directory via ``SL261X_A55_OUT``:

.. code-block:: console

   export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
   export GNUARMEMB_TOOLCHAIN_PATH=<path/to/arm-gnu-toolchain>

   west build -b sl2619_rdk/sl2619/m52 \
       ../zephyr_srsdk/samples/boot_a55 \
       -p auto -DSL261X_A55_OUT="../../build/a55_image" -d build/<final_build_dir>

Serial Console Setup
--------------------

Before proceeding, connect a USB-TTL adaptor to the 40-pin GPIO connector
(RX, TX, GND) and open a serial terminal on the host:

.. code-block:: console

   $ minicom -D <tty_device> -b 115200

Replace ``<tty_device>`` with the appropriate serial device, for example
``/dev/ttyUSB0`` on Linux.

Flashing Zephyr on SL2619
==========================

To boot Zephyr using U-Boot from the A55, an eMMC Linux image
must first be programmed onto the board.

Step 1: Download a Prebuilt eMMC Image
---------------------------------------

Download the latest prebuilt eMMC image for your board from the
`Synaptics Astra SDK Releases`_ page on GitHub
(``https://github.com/synaptics-astra/sdk/releases``).

Select the image matching your board, for example:

* **sl2619** — ``sl2619_scarthgap_<version>.zip``

Extract the downloaded archive. The resulting directory (``eMMCimg``) contains
all partition sub-images and the image list files required for flashing.

.. note::

   When flashing the eMMC for the first time, copy ``emmc_image_list_full``
   to ``emmc_image_list`` inside the ``eMMCimg`` directory on the USB drive.
   This ensures the ``/factory_setting`` partition is formatted correctly.
   Skipping this step causes the system to boot into maintenance mode.

Step 2: Flash the eMMC from U-Boot
------------------------------------

Detailed SL261x flashing steps are also documented in `Synaptics Astra SDK Releases`_

Copy the extracted ``eMMCimg`` directory to a FAT32-formatted USB drive and
insert it into the board. At the U-Boot prompt run:

.. code-block:: console

   => usb reset
   => usb2emmc eMMCimg

.. note::

   If ``usb reset`` reports ``0 Storage Device(s) found`` and only one USB
   controller was detected, connect the USB drive to the USB Type-C USB 2.0
   port (may require a USB Type-C to USB Type-A adaptor).

The board will automatically reboot once flashing is complete and boot
into Linux on the A55.

Step 3: Flash the Zephyr Image via U-Boot
----------------------------------------------

After building the Zephyr application, the generated SPI image is located at:

.. code-block:: none

   build/<final_build_dir>/image/spi/spi_boot.bin

**3a – Copy image to USB and enter U-Boot**

Copy ``spi_boot.bin`` to a FAT32-formatted USB drive and insert it into the
board. Power up in eMMC boot mode and interrupt the U-Boot autoboot sequence
to reach the U-Boot prompt.

**3b – Write Zephyr image to XSPI flash**

Run the following commands at the U-Boot prompt:

.. code-block:: console

   => usb reset
   => fatload usb 0 0x10000000 spi_boot.bin
   => sf probe
   => sf erase 0 0x400000
   => sf write 0x10000000 0x0 0x400000

**3c – Connect SD_BOOT jumper**

After flashing, connect the **SD_BOOT** jumper near the XSPI flash to enable
SPI boot mode on next reset.

This sequence:

* Initializes USB
* Loads the Zephyr SPI image into RAM at ``0x10000000``
* Probes the SPI flash
* Erases the required flash region
* Writes the Zephyr image to XSPI flash

Reset or power-cycle the board. The Zephyr image will be loaded from
XSPI flash and execute on the core.

Boot Flow Summary
-----------------

1. Download prebuilt eMMC image from `Synaptics Astra SDK Releases`_
2. Flash eMMC from U-Boot using ``usb2emmc``
3. Board reboots into Linux on A55
4. Connect SD_BOOT jumper (enables SPI boot)
5. Flash Zephyr SPI image to XSPI from U-Boot
6. Reset board — Zephyr runs on SL2619

References
**********

.. target-notes::

.. _Synaptics website: https://www.synaptics.com/products/embedded-processors/sl2610-product-line
.. _Synaptics Astra Machina Eval platform website: https://synaptics-astra.github.io/doc/v/latest/hw/sl2600.html
.. _Synaptics Astra SDK Releases: https://synaptics-astra.github.io/doc/v/latest/linux/index.html
