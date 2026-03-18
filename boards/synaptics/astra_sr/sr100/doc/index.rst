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

A detailed description on the hardware connections, IOs and peripherals can be found on the
`Synaptics Astra MCU website`_ and in the `Synaptics Platform Guide`_.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The Synaptics SR110 SoC needs to be eMMC flashed prior to running a Zephyr application. This can be
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

Install GNU Arm Embedded toolchain.
----------------------------------
.. code-block:: console

   wget https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz
   tar xJf arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz -C $HOME
   export GNUARMEMB_TOOLCHAIN_PATH="$HOME/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi"
   export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
   export PATH="$PATH:$HOME/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin"

Make sure the toolchain is available in your environment (e.g., added to PATH).

 SDK Tools Setup
---------------

Install the required SDK tools by following the
`Zephyr Getting Started Guide <https://docs.zephyrproject.org/latest/develop/getting_started/index.html>`_.

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

Install GNU Arm Embedded toolchain.
----------------------------------
.. code-block:: console

   wget https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz
   tar xJf arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz -C $HOME
   export GNUARMEMB_TOOLCHAIN_PATH="$HOME/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi"
   export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
   export PATH="$PATH:$HOME/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin"

Make sure the toolchain is available in your environment (e.g., added to PATH).

SDK Tools Setup
---------------

Install the required SDK tools by following the
`Zephyr Getting Started Guide <https://docs.zephyrproject.org/latest/develop/getting_started/index.html>`_.
follow the steps from section "Select and Update OS" till step 3 of "Get Zephyr and install Python dependencies" section.


> sudo apt install ninja-build
> sudo apt install binutils-arm-none-eabi
> pip install cmake

Initialization
==============

Next, obtain ``zephyr_srsdk`` either by ``west init`` or from a ``.zip`` file.

Option 1: west init

The first step is to initialize the workspace folder (``syna_zephyr``) where
the example application and all Zephyr modules will be cloned. Run the following
command:

.. code-block:: console

   west init -m https://github.com/synaptics-astra-mcu/syna_zephyr_sdk.git --mr main  syna_zephyr

Option 2: .zip file

Copy the ``zephyr_srsdk-main.zip`` file into ``~/syna_zephyr`` and run:

.. code-block:: console

   cd ~/syna_zephyr
   unzip zephyr_srsdk-main.zip
   mv zephyr_srsdk-main zephyr_srsdk
   west init -l .

 # update Zephyr modules
   cd syna_zephyr/zephyr
   west update

For the image generation scripts, clone the following repository:

.. code-block:: console

   git clone https://github.com/synaptics-astra-mcu/srsdk_tools.git

These tools require the python package ``pycrypto`` and the executable
``arm-none-eabi-objcopy`` from the ARM GNU toolchain in your PATH.

Finally, enter the ``zephyr_srsdk`` directory:

.. code-block:: console

   cd zephyr_srsdk


Building
========

To build an application, use the standard Zephyr command. Here is an example for the
:zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: sr100_rdk
   :goals: build

You can also try the shell and blinky examples:

.. code-block:: console

   west build -b sr100_rdk ../zephyr/samples/basic/blinky
   west build -b sr100_rdk ../zephyr/samples/subsys/shell/shell_module

For a clean rebuild:

.. code-block:: bash

   west build -p always -b sr100_rdk ../zephyr/samples/subsys/shell/shell_module

Flashing
========

For installation on the target, the Astra image generator scripts need to be
invoked to create a combined image (SPK, APBL/bootloader & M55-firmware).

The default path to the image generator is ``syna_zephyr/srsdk_tools`` but can
be modified with the parameter ``-DSRSDK_IMGGEN=path`` when calling ``west build``.
The resulting image file is stored in ``build/zephyr/zephyr_flash.bin``.

Connect the board using the USB-C connector to your host machine. Make sure you can see the
CMSIS-DAP endpoint using ``lsusb``:

Debugging on WSL
----------------

To use ``west debugserver``, the CMSIS-DAP device must be attached to WSL.

Use ``usbipd`` from Windows Powersehll with admin permission to list and attach the device:

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
   :board: sr100_rdk
   :goals: debugserver

.. code-block:: console

   cd syna_zephyr/zephyr_srsdk
   west debugserver

Now, in a separate terminal, run the flashing script provided by Synaptics:

.. code-block:: console

   cd syna_zephyr/srsdk_tools
   python openocd_flash.py ../zephyr_srsdk/build/zephyr/zephyr_flash.bin 0x0 0x0 1

References
**********

.. target-notes::

.. _Synaptics website: https://www.synaptics.com/assets/product-brief/sr-series
.. _Synaptics Astra MCU website: https://synaptics-astra-mcu.github.io/doc/v/latest/platform/Astra_Machina_Micro_SR100_Series_Evaluation_Platform_Kit_RevC_UG_511-001445-02_RevA.html
.. _Synaptics Platform Guide: https://synaptics-astra-mcu.github.io/doc/v/latest/srsdk/docs/SR110/SR110_platform_Guide.html
