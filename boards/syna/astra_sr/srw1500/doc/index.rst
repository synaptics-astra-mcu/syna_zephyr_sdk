.. zephyr:board:: srw1500_rdk

Overview
********

The Synaptics Astra™ SRW1500 Series is an advanced AI-native connected MCU
with 2.4/5/6 GHz single-chip IEEE 802.11ax and 802.11be support, a 1x1 MAC,
baseband, and radio, plus integrated Bluetooth 6.0 and an 802.15.4 radio.
It is purpose-built for intelligent IoT systems that demand real-time inference,
low-latency responsiveness, and advanced wireless connectivity.

The Astra Machina Micro SRW1500 Evaluation Platform Kit features the
following key components:

* Arm Cortex-M52 running at 200 MHz with Arm Helium vector extensions, 1 MB SRAM
* Integrated Arm Ethos-U55 running at 200 MHz, with a 50 GOPS NPU for
  efficient on-device AI inference
* Embedded security architecture including Arm TrustZone®, AES, SAU, and MPU
* Comprehensive peripherals: SDIO, SPI, UART, I²C, PDM, ADC, DAC, PWM, and USB
* Tri-band 1x1 Wi-Fi 7 (2.4/5/6 GHz) operation
* Synaptics Smart Co-Ex™ technology for optimized 2.4 GHz Wi-Fi and
  Bluetooth coexistence
* Supports FreeRTOS and Zephyr operating systems

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

The default board devicetree enables the following peripherals:

* UART0/1 for the console and shell
* I²C master/slave for sensors and other peripherals
* SPI master/slave for sensors and other peripherals
* Slow clock timer and system counter
* Timers
* Watchdogs
* xSPI flash interface
* Security attribution, memory protection, and peripheral protection blocks

UART0 is enabled on the default FTDI UART pins at 115200 baud. To use the GPADC
pins for the FTDI UART, add the ``uart0_ftdi_gpadc.overlay`` overlay from
``zephyr_srsdk/boards/syna/astra_sr/srw1500/`` to the build command.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Setup
=====

Linux Host Setup
----------------

Install host dependencies:

.. code-block:: console

   cd ~
   sudo apt update && sudo apt upgrade -y
   sudo apt install python3-pip python3-venv
   python -m venv ~/.venvs/syna_zephyr
   source ~/.venvs/syna_zephyr/bin/activate
   python -m pip install --upgrade pip
   python -m pip install pycryptodome pexpect west cmake
   sudo apt install zip unzip usbutils openocd gperf
   sudo apt install ninja-build
   sudo apt install binutils-arm-none-eabi

Initialization
==============

Initialize the workspace using HTTPS authentication:

.. code-block:: console

   west init -m https://github.com/syna-eepd/zephyr_srsdk.git --mr main ~/syna_zephyr

Alternatively, initialize the workspace using SSH authentication:

.. code-block:: console

   west init -m git@github.com:syna-eepd/zephyr_srsdk.git --mr main ~/syna_zephyr

Install Zephyr dependencies and clone the upstream projects:

.. code-block:: console

   cd ~/syna_zephyr
   west update --group-filter +optional
   python -m pip install -r ~/syna_zephyr/zephyr/scripts/requirements.txt
   west sdk install --version 1.0.0

For image generation, clone the Synaptics SRSDK tools repository:

.. code-block:: console

   cd ~/syna_zephyr
   git clone https://github.com/synaptics-astra-mcu/srsdk_tools.git

Tools specific to srw1500 are located in ``~/syna_zephyr/srsdk_tools/srw1500_tools``.

.. note::

   Access to ``https://github.com/syna-eepd/zephyr_srsdk.git`` requires an
   authorized GitHub account. If ``west sdk install --version 1.0.0`` fails due
   to a token limit error, generate a GitHub token and retry with
   ``west sdk install --version 1.0.0 --personal-access-token <token>``.

**Apply patches**

.. code-block:: console

   cd ~/syna_zephyr
   west syna-patch apply

Building
========

The SRW1500 smoke test sample can be built as a RAM image or as an XIP flash
image.

**XIP flash image**

Sysbuild and MCUboot
====================

The SRW1500 platform supports building the application together with
MCUboot using Zephyr Sysbuild. Sysbuild is used to configure and build
the MCUboot bootloader and the application as part of a single build.

MCUboot configuration
---------------------

The MCUboot configuration for SRW1500 is maintained under the SRW1500
board configuration. The following files are used to configure the
Sysbuild and MCUboot build:

* ``sysbuild.conf`` - Enables Sysbuild and MCUboot integration.
* ``sysbuild.cmake`` - Provides additional Sysbuild configuration.
* ``sysbuild/mcuboot.conf`` - Contains MCUboot-specific Kconfig options.
* ``sysbuild/mcuboot.overlay`` - Defines the MCUboot flash partitions
  and devicetree configuration.

The MCUboot configuration includes support for:

* Single application slot.
* XIP execution.

Build Command
-------------

.. code-block:: console

   west build --pristine -b srw1500_rdk \
      zephyr_srsdk/samples/srw1500_smoke_test/ \
      --sysbuild \
      -- \
      -DEXTRA_CONF_FILE="$PWD/zephyr_srsdk/boards/syna/astra_sr/srw1500/srw1500_rdk_xip.conf" \
      -DEXTRA_DTC_OVERLAY_FILE="$PWD/zephyr_srsdk/boards/syna/astra_sr/srw1500/srw1500_rdk_xip.overlay;\
      $PWD/zephyr_srsdk/boards/syna/astra_sr/srw1500/uart0_ftdi_gpadc.overlay"

The ``--sysbuild`` option builds MCUboot together with the application.

The ``srw1500_rdk_xip.conf`` configuration enables XIP support, and the
specified devicetree overlays configure the SRW1500 XIP memory layout and
UART0 FTDI/GPADC configuration.

.. note::

   Currently, the MCUboot + Application build is supported only for flash-based builds.

   UART0 FTDI/GPADC configuration is provided by the ``uart0_ftdi_gpadc.overlay`` 
   file needs to be included in the build for SRW1500 Dev Board P214 onwards.

USB console and shell
=====================

FTDI UART is used for the console, shell, and logs by default. To use the USB
COM port for the XIP build, open
``boards/syna/astra_sr/srw1500/srw1500_rdk_xip.overlay`` and uncomment these
two lines in the ``chosen`` node:

.. code-block:: devicetree

   zephyr,console = &cdc_acm_uart0;
   zephyr,shell-uart = &cdc_acm_uart0;

Then set the following options in
``boards/syna/astra_sr/srw1500/srw1500_rdk_xip.conf`` and rebuild:

.. code-block:: cfg

   CONFIG_LOG_BACKEND_UART=n
   CONFIG_SHELL_LOG_BACKEND=y

Only one log backend should be enabled to prevent duplicate output.


Image Outputs
=============

The generated flash/XIP image boots from flash address ``0x38000000``:

.. code-block:: console

   build/srw_1500_smoke_test/zephyr/srw1500_zephyr_full_flash.bin

OTP Provisioning
================

Configure the OTP provisioning mode in
``zephyr_srsdk/samples/srw1500_smoke_test/prj.conf`` according to the
target board.

.. code-block:: cfg

   CONFIG_OTP_PROVISIONED_ENABLED=n
   CONFIG_OTP_PROVISIONED_V2_ENABLED=y

Loading and Executing Images
============================

Strap settings:

* STRAP 0: PD
* STRAP 1: PU
* STRAP 2: PU
* STRAP 3: PD
* STRAP 4: PD
* STRAP 5: PD
* STRAP 6: PD
* STRAP 7: PD
* STRAP 8: PU
* STRAP 9: PU

.. note::

   To program the flash image, STRAP 2 must be pulled up to select host-boot
   mode.

Flash XSPI Image
----------------

#. Power up the board using a USB Type-C cable.
#. Install the OpenOCD Windows driver using Zadig:

   * Download Zadig from its official website.
   * Select **Options > List All Devices**.
   * Select FTDI Interface 1 and install the WinUSB driver.

#. Copy ``srw1500_fw_flash_zephyr.bin`` to the directory containing
   ``openocd_flash_erase.py``.
#. Run:

   .. code-block:: console

      python openocd_flash_erase.py srw1500_fw_flash_zephyr.bin

#. After programming completes, set STRAP 2 to pull-down and power-cycle the
   board.
#. Connect to UART0 through FTDI interface 0 at 115200 baud.

Flash RAM Image
---------------

#. Power up the board using a USB Type-C cable.
#. Run the following command to download the image through UART0:

   .. code-block:: console

      python uart_fw_download.py m52_fw_RAM.bin COM12 230400

.. note::

   Connect an external USB-to-UART TTL converter for firmware download.

   UART0 pin connections:

   * TX: J8 Pin 1
   * RX: J8 Pin 2
   * GND: J6 Pin 6

Testing
=======

Once the ``srw1500_smoke_test`` image is running, connect to UART0 and use the
shell to run the I2C tests:

.. code-block:: console

   uart:~$ i2c_master_test
   uart:~$ i2c_loopback_test

``i2c_master_test`` exercises I2C0 as a controller against an external target
device at address ``I2C_SABRE_SLAVE_ADDR``. ``i2c_loopback_test`` exercises
both I2C0 (controller) and I2CS0 (target) on the same board, verifying the
full master/slave clock and reset bring-up path.
