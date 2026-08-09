.. zephyr:code-sample:: usb-composite
   :name: USB CDC ACM UART sample & HID keyboard
   :relevant-api: usbd_api usbd_hid_device input_interface uart_interface

   Use USB CDC ACM UART driver to implement a serial port echo.
   Implement a basic HID keyboard device.

Overview
********

This sample app demonstrates use of a USB Communication Device Class (CDC) Abstract Control Model
(ACM) driver provided by the Zephyr project. Received data from the serial port is echoed back to
the same port provided by this driver. Two CDC-ACM devices and a HID keyboard are registered.

See ``zephyr/samples/subsys/usb/hid-keyboard/README.rst`` and
``zephyr/samples/subsys/usb/cdc_acm/README.rst``. This sample is a combination of the existing two
usb samples of Zephyr: ``hid-keyboard`` and ``cdc_acm``.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: <path-to-zephyr_srsdk>/samples/subsys/usb_composite_hid_dual_cdc
   :board: sr100_rdk/sr100/m55
   :goals: build
   :compact:

On the host, start at least one serial port emulator, e.g., minicom and attach it to one of the
detected CDC ACM devices. The sample will wait for a connection before the HID processing is
started.

On a Linux host, you can dump the HID events with, e.g., ``usbhid-dump -e a``. If you press the
user button on the RDK board (``SW8``), you should see prints like the following:

.. code-block:: console

   usbhid-dump -e a

   001:012:004:STREAM             1779274716.022461
    00 00 00 00 00 00 00 00

   001:012:004:STREAM             1779274716.236394
    00 00 53 00 00 00 00 00
