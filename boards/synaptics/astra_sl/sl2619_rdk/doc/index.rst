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

A detailed description of the hardware connections, IOs and peripherals can be found on the
`Synaptics Astra Machina Eval platform website`_.


Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The Synaptics SL261X SoC needs to be eMMC flashed prior to running a Zephyr application.
This can be achieved by following Synaptics Astra Machina Eval platform instructions.


Setup
=====

WSL
---

Open Windows PowerShell and run:

.. code-block:: console

   wsl --install
   wsl.exe --install Ubuntu-24.04

Now switch to the WSL terminal and run:

.. code-block:: console

   cd ~
   sudo apt update && sudo apt upgrade -y
   sudo apt install python3-pip python3-venv
   python3 -m venv ~/.venvs/syna_zephyr
   source ~/.venvs/syna_zephyr/bin/activate
   pip install --upgrade pip
   pip install west
   sudo apt install unzip
   sudo apt install ninja-build
   sudo apt install binutils-arm-none-eabi
   pip install cmake


Linux
-----

On a Linux host terminal, run:

.. code-block:: console

   cd ~
   sudo apt update && sudo apt upgrade -y
   sudo apt install python3-pip python3-venv
   python3 -m venv ~/.venvs/syna_zephyr
   source ~/.venvs/syna_zephyr/bin/activate
   pip install --upgrade pip
   pip install west
   sudo apt install unzip
   sudo apt install ninja-build
   sudo apt install binutils-arm-none-eabi
   pip install cmake

Initialization
==============

Next, obtain ``zephyr_srsdk`` either by ``west init`` or from a ``.zip`` file.

**Option 1: west init**

Initialize the workspace folder (``syna_zephyr``):

.. code-block:: console

   west init -m https://github.com/synaptics-astra-mcu/syna_zephyr_sdk --mr main syna_zephyr


**Option 2: .zip file**

.. code-block:: console

   cd ~/syna_zephyr
   unzip zephyr_srsdk-main.zip
   mv zephyr_srsdk-main zephyr_srsdk
   west init -l .

**Common setup (required for both options)**

.. code-block:: console

   cd ~/syna_zephyr/zephyr
   west update
   west sdk install --version 1.0.0 --toolchains arm-zephyr-eabi aarch64-zephyr-elf
   pip install -r ~/syna_zephyr/zephyr/scripts/requirements.txt

**Additional requirement (GICv2 support)**

.. note::

   This patch is required for proper GICv2 interrupt controller support.

Upstream PR:
``https://github.com/zephyrproject-rtos/zephyr/pull/105253``

.. code-block:: console

   cd ~/syna_zephyr/zephyr
   git config user.name "<USER>"
   git config user.email "<EMAIL ID>"
   git fetch origin pull/105253/head:pr-105253
   git cherry-pick 58ed79495d1


Building
========

Before building, set the toolchain environment for each target.

**A55 DDRLESS image**

.. code-block:: console

   cd syna_zephyr/zephyr

   export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
   export ZEPHYR_SDK_INSTALL_DIR=<path/to/zephyr-sdk-1.0.0>

   west build -b sl2619_rdk/sl2619/a55/ddrless \
       samples/subsys/shell/shell_module \
       -p auto -d build/a55_image


**M52 bootloader image (with A55 image bundled)**

.. code-block:: console

   export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
   export GNUARMEMB_TOOLCHAIN_PATH=<path/to/arm-gnu-toolchain>

   west build -b sl2619_rdk/sl2619/m52 \
       ../zephyr_srsdk/samples/boot_a55 \
       -p auto \
       -DSL261X_A55_OUT="../../build/a55_image" \
       -d build/<final_build_dir>


Serial Console Setup
--------------------

Connect a USB-TTL adaptor to the 40-pin GPIO connector (RX, TX, GND)
and open a serial terminal:

.. code-block:: console

   minicom -D <tty_device> -b 115200

Example:

``/dev/ttyUSB0``


Flashing Zephyr on SL2619
==========================

To boot Zephyr using U-Boot from the A55, an eMMC Linux image must first be programmed.


Step 1: Download a Prebuilt eMMC Image
--------------------------------------

Download from:
`Synaptics Astra SDK Releases`_

Example:

* **sl2619** — ``sl2619_scarthgap_<version>.zip``

Extract to get ``eMMCimg`` directory inside USB pen drive.

.. note::

   For first-time flashing, copy the contents of``emmc_image_list_full`` to ``emmc_image_list``. These files are located in the ``eMMCimg`` directory.


Step 2: Flash the eMMC from U-Boot
----------------------------------

.. code-block:: console

   => usb reset
   => usb2emmc eMMCimg

.. note::

   Use USB Type-C port if storage is not detected.


Step 3: Flash the Zephyr Image via U-Boot
-----------------------------------------

Generated file:
Copy the generated ``spi_boot.bin`` file from the build output to the USB drive.

.. code-block:: none

   build/<final_build_dir>/image/spi/spi_boot.bin


**3a – Load image**

.. code-block:: console

   => usb reset
   => fatload usb 0 0x10000000 spi_boot.bin


**3b – Flash to XSPI**

.. code-block:: console

   => sf probe
   => sf erase 0 0x400000
   => sf write 0x10000000 0x0 0x400000


**3c – Enable SPI boot**

Connect the **SD_BOOT** jumper.


Boot Flow Summary
-----------------

1. Flash eMMC (Linux)
2. Boot A55 Linux
3. Flash Zephyr to XSPI
4. Enable SPI boot (SD_BOOT)
5. Reset board
6. Zephyr runs


References
**********

.. target-notes::

.. _Synaptics website: https://www.synaptics.com/products/embedded-processors/sl2610-product-line
.. _Synaptics Astra Machina Eval platform website: https://synaptics-astra.github.io/doc/v/latest/hw/sl2600.html
.. _Synaptics Astra SDK Releases: https://synaptics-astra.github.io/doc/v/latest/linux/index.html
