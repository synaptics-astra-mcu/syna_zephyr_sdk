.. zephyr:code-sample:: host_api_system
   :name: Host API System Sample

   Enable the Host API core with the system, firmware-update, and flash
   auxiliary services on ``sr100_rdk/sr100/m55`` over USB.

Description
***********

This sample enables the Host API core, system service, firmware update
service, and flash auxiliary service on ``sr100_rdk/sr100/m55``. The active
Host API transport in this sample is USB, exposed to the host as a CDC ACM
serial port.

The sample is intended for end-to-end validation of:

* Host API command exchange between the host PC and SR100 M55
* System-service version, register, interface, reset, and onboard I2C access
* Managed firmware SDK update flow
* Flash auxiliary service support for extension-based raw flash programming

Requirements
************

Hardware:

* Synaptics SR100 RDK (``sr100_rdk/sr100/m55``)
* USB connection between the host PC and the SR100 board

Host tools:

* Python with ``pyserial`` installed

Build and Run
*************

Build
=====

.. code-block:: console

   west build -p always -d build/host_api_system -b sr100_rdk/sr100/m55 samples/subsys/host_api_system


Runtime
=======

1. Connect the board over USB and wait for the Host API serial port to enumerate.
2. Open the device log console and wait for:

   .. code-block:: text

      Host API is ready; open the Host API port on host and send Host API commands
      Validation register @ 0xXXXXXXXX = 0x13572468

3. Note the validation register address printed by the sample.
4. Install the host dependency if needed:

   .. code-block:: console

      python -m pip install pyserial

Basic Validation
****************

The preferred system-service entry point is the consolidated host tool below.
It auto-detects the Host API port using ``VID=0xCAFE`` and ``PID=0x4002``.
Use ``--port`` only when you want to override auto-detect.

Basic command sweep
===================

Usage:

.. code-block:: console

   python tools/scripts/host_api/host_api_system_tool.py basic --verbose

This command validates:

* Host API version response
* Loaded applications query
* Toggle CRC command path
* Read pending message with no queued event
* Configure active interface to USB

Sample log:

.. code-block:: text

   PS C:\Users\bsanjays\srsdk> python host_api_system_tool.py basic --verbose
   Auto-detected Host API port by VID/PID: COM50, VID:PID=cafe:4002, description=USB Serial Device (COM50)
   VERSION:
     TX      : 5b 5a 01 01 00 00 00 00
     Header  : 5b 5a 01 01 04 00 00 00
     Payload : 00 00 09 07
   Version : 0.9.7
   PASS: version response received
   GET_LOADED_APPS:
     TX      : 5b 5a 01 06 00 00 00 00
     Header  : 5b 5a 01 06 00 00 00 00
     Payload :
   PASS: get_loaded_apps returned zero-length response
   TOGGLE_CRC:
     TX      : 5b 5a 01 00 00 00 00 00
     Header  : 5b 5a 01 00 00 00 00 00
     Payload :
   PASS: toggle_crc command path responded successfully
   READ_PENDING:
     TX      : 5b 5a 01 04 00 00 00 00
     Header  : 5b 5a 04 00 00 00 00 00
     Payload :
   PASS: read_pending_message reported no pending event as expected
   CONFIG_USB:
     TX      : 5b 5a 01 05 01 00 00 00 01
     Header  : 5b 5a 01 05 00 00 00 00
     Payload :
   PASS: config_active_interface(USB) returned success

Register read/write validation
==============================

Usage:

.. code-block:: console

   python tools/scripts/host_api/host_api_system_tool.py register-rw --address 0xXXXXXXXX --initial 0x13572468 --write-value 0xA5A5F00D

Use the validation register address printed in the device log by the sample in the option --address 0xXXXXXXXX.

Sample log:

.. code-block:: text

   PS C:\Users\bsanjays\srsdk> python host_api_system_tool.py register-rw --address 0x33EA05D0 --initial 0x13572468 --write-value 0xA5A5F00D
   Auto-detected Host API port by VID/PID: COM50, VID:PID=cafe:4002, description=USB Serial Device (COM50)
   Value   : 0x13572468
   PASS: read_register returned expected value
   PASS: write_register returned success
   Value   : 0xa5a5f00d
   PASS: read_register returned expected value

Software reset validation
=========================

Usage:

.. code-block:: console

   python tools/scripts/host_api/host_api_system_tool.py reset

Sample log:

.. code-block:: text

   PS C:\Users\bsanjays\srsdk> python host_api_system_tool.py reset
   Auto-detected Host API port by VID/PID: COM50, VID:PID=cafe:4002, description=USB Serial Device (COM50)
   PASS: reset command acknowledged; device should reboot immediately

I2C validation
==============

The sample validates onboard I2C access through the board expander at slave
address ``0x20``.

Read current register value:

.. code-block:: console

   python tools/scripts/host_api/host_api_system_tool.py i2c-read --slave 0x20 --register 0x02 --expected-rc 0

Write a new value:

.. code-block:: console

   python tools/scripts/host_api/host_api_system_tool.py i2c-write --slave 0x20 --register 0x02 --value 0x01 --expected-rc 0

Read back and verify:

.. code-block:: console

   python tools/scripts/host_api/host_api_system_tool.py i2c-read --slave 0x20 --register 0x02 --expected-rc 0 --expected-value 0x01

Sample log:

.. code-block:: text

   PS C:\Users\bsanjays\srsdk> python host_api_system_tool.py i2c-read --slave 0x20 --register 0x02 --expected-rc 0
   Auto-detected Host API port by VID/PID: COM50, VID:PID=cafe:4002, description=USB Serial Device (COM50)
   RC      : 0
   Value   : 0x00
   PASS: read_i2c_register returned expected response
   PS C:\Users\bsanjays\srsdk> python host_api_system_tool.py i2c-write --slave 0x20 --register 0x02 --value 0x01 --expected-rc 0
   Auto-detected Host API port by VID/PID: COM50, VID:PID=cafe:4002, description=USB Serial Device (COM50)
   RC      : 0
   PASS: write_i2c_register returned expected response
   PS C:\Users\bsanjays\srsdk> python host_api_system_tool.py i2c-read --slave 0x20 --register 0x02 --expected-rc 0 --expected-value 0x01
   Auto-detected Host API port by VID/PID: COM50, VID:PID=cafe:4002, description=USB Serial Device (COM50)
   RC      : 0
   Value   : 0x01
   PASS: read_i2c_register returned expected response

Firmware Update Validation
**************************

The preferred managed firmware-update entry point is the consolidated tool
below. It auto-detects the Host API port using ``VID=0xCAFE`` and
``PID=0x4002``. Use ``--port`` only when you want to override auto-detect.

Managed SDK image flashing
==========================

Usage:

.. code-block:: console

   python tools/scripts/host_api/host_api_fw_update_tool.py --bin-file "C:\path\to\image.bin"

The tool accepts either:

* a real SDK payload image, or
* a full flash image containing embedded NVM metadata, from which the SDK
  payload is extracted automatically

Sample log:

.. code-block:: text

   PS C:\Users\bsanjays\srsdk> python host_api_fw_update_tool.py --bin-file "C:\Users\bsanjays\Downloads\B0_flash_full_image_GD25LE128_67Mhz_secured.bin"
   Auto-detected Host API port by VID/PID: COM50, VID:PID=cafe:4002, description=USB Serial Device (COM50)
     FW Ver  : 0x07090000
     MaxWr   : 256
   Image file : C:\Users\bsanjays\Downloads\B0_flash_full_image_GD25LE128_67Mhz_secured.bin
   Image kind : full-flash-image: extracted SDK payload 0x00050000..0x00340000
   Input size : 3515936 bytes
   Write size : 3080192 bytes
   Image ID   : 2
   Chunk size : 256 bytes
   State check: READY
   Starting managed FW update...
   [  0%] @0x00000000 Flash Programming sector# 0
   [  0%] @0x00001000 Flash Programming sector# 1
   [  0%] @0x00002000 Flash Programming sector# 2
   [  0%] @0x00003000 Flash Programming sector# 3
   [  1%] @0x00004000 Flash Programming sector# 4
   [  1%] @0x00005000 Flash Programming sector# 5
   [  1%] @0x00006000 Flash Programming sector# 6
   [  1%] @0x00007000 Flash Programming sector# 7
   .
   .
   .
   .
   .
   [ 99%] @0x002eb000 Flash Programming sector# 747
   [ 99%] @0x002ec000 Flash Programming sector# 748
   [100%] @0x002ed000 Flash Programming sector# 749
   [100%] @0x002ee000 Flash Programming sector# 750
   [100%] @0x002ef000 Flash Programming sector# 751
   [100%] @0x002ef000 Flash Programming sector# 751
   Installing staged image...
   Flash burn completed successfully, Programming time: 51 sec
   Rebooting into trial image...
   Trial boot check: OK
   PASS: managed FW update image flashing completed

Post-reboot recovery to READY
=============================

If the device is left in the ``TRIAL`` state after reboot and you want to
advance it back to ``READY``, use the post-reboot recovery command below.

Usage:

.. code-block:: console

   python tools/scripts/host_api/host_api_fw_update_tool.py --accept --clean

Flash Auxiliary Usage
*********************

The flash auxiliary service is enabled in this sample to support extension-
based raw flash programming. It is not documented here as a standalone
command-line validation flow.
