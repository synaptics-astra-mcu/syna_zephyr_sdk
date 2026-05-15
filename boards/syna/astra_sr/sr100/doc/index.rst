.. zephyr:board:: sr100_rdk

Overview
********

The Synaptics Astra™ SR100 Series of AI MCUs is designed to deliver
high-performance, AI-Native, multimodal compute to consumer, enterprise, and
industrial Internet of Things (IoT) workloads.

* The Astra Machina Micro SR100 Evaluation Platform Kit features the following
  key components:

  * Synaptics SR110 (122-FCCSP) Audio & Vision AI processor
  * Debug IC Synaptics SR100 (84-WCCSP)
  * Storage: 128 Mbit QSPI NOR Flash
  * PMIC: Buck-Boost DC/DC for SR100 VBAT
  * Highly sensitive ambient light sensor: TCS34303
  * 3-axis accelerometer: MC3479
  * M.2 E-key 2230 receptacle: Supports SDIO, UART, and PCM for Wi-Fi/BT modules
  * 2x USB 2.0 Type-C™ ports: Peripheral mode (Hi-Speed) and system power input
  * Push buttons for system reset and wake-up
  * Slide switches for image programming, mute control, and debugging
  * Several LEDs, including two user-controllable LEDs

* Daughter card interface options:

  * 2x MIPI CSI-2® 2-lane RX interfaces (1.5 Gb/s max bandwidth): CSI0 on
    Samtec™ connector (shared with DVP), CSI1 on 15-pin FPC connector
  * 1x MIPI CSI-2® TX interface (1.5 Gb/s max bandwidth) on 15-pin FPC connector
  * SWD JTAG daughter card for debugging
  * 2x 20-pin headers with GPIOs are for additional application
  * 4-pin header for UART debugging
  * 3-pin header for PIR

* System power supply:

  * USB Type-C
  * 2-pin, 2.0 mm pitch header for 1-cell Li-ion battery
  * 3-pin header for system power source selection

More information about Astra MCU series and the evaluation board can be found
at the `Synaptics website`_.

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

A detailed description of the hardware connections, IOs and peripherals can be found on the
`Synaptics Astra MCU website`_ and in the `Synaptics Platform Guide`_.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The Synaptics SR110 SoC must be flashed before running a Zephyr application. This can be
achieved by following Synaptics Astra Machina Eval platform instructions.

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
   pip install pycryptodome pexpect
   pip install --upgrade pip
   pip install west
   sudo apt install zip unzip usbutils openocd
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
   pip install pycryptodome pexpect
   pip install --upgrade pip
   pip install west
   sudo apt install zip unzip usbutils openocd
   sudo apt install ninja-build
   sudo apt install binutils-arm-none-eabi
   pip install cmake


Initialization
==============

Next, obtain ``zephyr_srsdk`` either by ``west init`` or from a ``.zip`` file.

Option 1: west init

The first step is to initialize the workspace folder (``~/syna_zephyr``) where
the example application and all Zephyr modules will be cloned. Run the following
command:

.. code-block:: console

   west init -m https://github.com/synaptics-astra-mcu/syna_zephyr_sdk.git --mr main  ~/syna_zephyr

Option 2: .zip file

Copy the ``zephyr_srsdk-main.zip`` file into ``~/syna_zephyr`` and run:

.. code-block:: console

   cd ~/syna_zephyr
   unzip zephyr_srsdk-main.zip
   mv zephyr_srsdk-main zephyr_srsdk
   west init -l .


**Common setup (required for both options)**

.. code-block:: console

   cd ~/syna_zephyr
   west update
   pip install -r ~/syna_zephyr/zephyr/scripts/requirements.txt
   west sdk install --version 1.0.0

For the image generation scripts, clone the following repository:

.. code-block:: console

   cd ~/syna_zephyr
   git clone https://github.com/synaptics-astra-mcu/srsdk_tools.git

These tools require the python package ``pycryptodome`` and the executable
``arm-none-eabi-objcopy`` from the ARM GNU toolchain in your PATH.

Building
========

To build an application, use the standard Zephyr command. Here is an example for the
:zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: sr100_rdk/sr100/m55
   :goals: build

You can also try the shell and blinky examples:

.. code-block:: console

   cd ~/syna_zephyr/zephyr_srsdk
   export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
   export ZEPHYR_SDK_INSTALL_DIR=<path/to/zephyr-sdk-1.0.0>
   west build -b sr100_rdk/sr100/m55 ../zephyr/samples/subsys/shell/shell_module
   west build -b sr100_rdk/sr100/m55 ../zephyr/samples/basic/blinky


For a clean rebuild:

.. code-block:: bash

   cd ~/syna_zephyr/zephyr_srsdk
   export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
   export ZEPHYR_SDK_INSTALL_DIR=<path/to/zephyr-sdk-1.0.0>
   west build -p always -b sr100_rdk/sr100/m55 ../zephyr/samples/subsys/shell/shell_module

For building the M4 firmware, specify the SoC variant ``m4``. When building the
M55 firmware, the configuration option ``CONFIG_SR100_RELEASE_M4_RESET`` needs
to be set so the M55 will take the M4 out of reset. The path to the M4 firmware
can be specified with the variable ``M4_BUILD``.

.. code-block:: console

   west build -p always -b sr100_rdk/sr100/m4 ../zephyr/samples/hello_world -d m4
   west build -p always -b sr100_rdk/sr100/m55 ../zephyr/samples/hello_world -d m55 -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD="../../m4"

Flashing
========

For installation on the target, the Astra image generator scripts need to be
invoked to create a combined image (SPK, APBL/bootloader & M55-firmware).

The default path to the image generator is ``~/syna_zephyr/srsdk_tools`` but can
be modified with the parameter ``-DSRSDK_IMGGEN=path`` when calling ``west build``.
The resulting image file is stored in ``build/zephyr/zephyr_flash.bin``.

Connect the SR110 board's J14 port using a USB-C connector to your host machine for flashing (top-view-of-astra-machina-micro-sr110_).
Make sure you can see the CMSIS-DAP endpoint using ``lsusb``:

Debugging on WSL
----------------

To use ``west debugserver``, the CMSIS-DAP device must be attached to WSL.

Use ``usbipd`` from Windows PowerShell with admin permission to list and attach the device:

.. code-block:: bash
   winget install --id=dorssel.usbipd-win -e # Install usbipd if not already installed
   usbipd list
   usbipd bind --busid <bus_id> --force # Bind the device to WSL
   usbipd attach --wsl --busid <bus_id> # Attach the device to WSL

.. code-block:: console

   $ lsusb | grep CMSIS-DAP
   Bus 001 Device 058: ID cafe:4006 Synaptics, Inc SR100 CMSIS-DAP

Next, run the ``west debugserver`` command.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: sr100_rdk/sr100/m55
   :goals: debugserver

.. code-block:: console

   cd ~/syna_zephyr/zephyr_srsdk
   west debugserver

Flashing image using WSL/Linux
------------------------------

To flash the image, need to terminate the debug server if it's running, and then run the following command:

.. code-block:: console

   cd ~/syna_zephyr/srsdk_tools
   python openocd_flash.py --openocd <path/openocd> --flash-offset 0x0 --file-offset 0x0 --cfg_path ~/syna_zephyr/srsdk_tools/Input_Config/sr100_m55.cfg --image <path_to_sdk>/build/zephyr/zephyr_flash.bin

Flashing image using Windows
----------------------------

To flash the image, need to terminate the debug server if it's running, unbind the CMSIS-DAP device from WSL using below command in Windows PowerShell with admin permission:

.. code-block:: bash
   usbipd list
   usbipd unbind --busid <bus_id> --force # Unbind the device from WSL

Then run the following command in Windows PowerShell / Command Prompt:

.. code-block:: console

   cd ~/syna_zephyr/srsdk_tools
   python openocd_flash.py --openocd <path/openocd.exe> --flash-offset 0x0 --file-offset 0x0 --cfg_path <path\srsdk_tools\Input_Config\sr100_m55.cfg> --image <path_to_sdk>\build\zephyr\zephyr_flash.bin>



Verify shell module application is running
------------------------------------------

After flashing the image, disconnect USB-C from the SR110 board's J14 port. Then connect power to the SR110 board through the J13 UART port by connecting it to the host machine.

For logs and shell interaction, connect GPIO 23 and GPIO 24 from J25 to the RX and TX pins of a USB-to-UART converter, respectively. Then connect the USB-to-UART converter to your host machine. refer to the `Synaptics Astra MCU website`_ and the `Synaptics Platform Guide`_ for more details on the pin connections.

On the host, open a serial terminal (for example, Tera Term) on the corresponding COM port at 230400 baud. You should see the shell prompt:

.. code-block:: console

   uart:~$

References
**********

.. target-notes::

.. _Synaptics website: https://www.synaptics.com/assets/product-brief/sr-series
.. _Synaptics Astra MCU website: https://synaptics-astra-mcu.github.io/doc/v/latest/platform/Astra_Machina_Micro_SR100_Series_Evaluation_Platform_Kit_RevC_UG_511-001445-02_RevA.html
.. _Synaptics Platform Guide: https://synaptics-astra-mcu.github.io/doc/v/latest/srsdk/docs/SR110/SR110_platform_Guide.html
.. _top-view-of-astra-machina-micro-sr110: https://synaptics-astra-mcu.github.io/doc/v/latest/platform/Astra_Machina_Micro_SR100_Series_Evaluation_Platform_Kit_RevC_UG_511-001445-02_RevA.html#top-view-of-astra-machina-micro-sr110
