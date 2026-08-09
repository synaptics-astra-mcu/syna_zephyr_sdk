.. zephyr:code-sample:: ipc_data_pipeline
   :name: IPC Data Pipeline
   :relevant-api: mbox_interface

   Forward I2C input-device data across cores with the MBOX API and emit USB HID reports.

Overview
********

This sample demonstrates a dual-core data pipeline on the Synaptics SR100
platform (Cortex-M4 and Cortex-M55 cores) using the :ref:`MBOX API <mbox_api>`.

Two applications are built:

- **HOST** (``samples/ipc_data_pipeline``, built for the **M55** core):
  reads two I2C input devices on GPIO data-ready interrupts, forwards each
  sample to the CLIENT over the mailbox, and turns the CLIENT replies into
  USB HID reports. It also brings up USB CDC ACM.
- **CLIENT** (``samples/ipc_data_pipeline/remote``, built for the **M4** core):
  receives mailbox frames, increments the counter, refreshes the timestamp,
  and sends the header back to the HOST.

Architecture and Data Flow
**************************

System block view:

.. code-block:: text

    M55 (HOST app, src/main.c + src/input_device.c)
    +----------------------------+         MBOX HW         +---------------------------+
    | InputDev1_Task /           | --tx--> [channel pair] --> mbox_rx_callback (ISR)    |
    | InputDev2_Task             |                                    |                 |
    | (GPIO IRQ -> I2C read)     |                                    v                 |
    +----------------------------+                           mbox_to_process_msgq      |
                  ^                                                  |                 |
                  |                                                  v                 |
                  |                                         CLIENT_Process_Task        |
    +----------------------------+         MBOX HW                   |                 |
    | HID_Task                   | <-rx-- [channel pair] <--tx-------+                 |
    | (USB HID reports)          |                          M4 (CLIENT, remote/src)    |
    +----------------------------+                          +---------------------------+

Shared modules:

- ``include/mbox_common.h`` defines ``struct mbox_message`` with:

  - ``source`` (input-device interface id)
  - ``counter``
  - ``timestamp``

- ``include/input_device.h`` and ``src/input_device.c`` provide the HOST-side
  input-device abstraction (GPIO reset sequence, data-ready interrupt, I2C
  read-only / write-read transfer paths, mailbox TX).

Runtime process:

1. A data-ready GPIO falling edge queues an ``INPUTDEV_EVENT_DATA_READY``
   event for the matching HOST input-device task.
2. The task reads the device over I2C while data-ready stays asserted and
   sends ``struct mbox_message`` followed by the raw I2C payload to the M4.
3. The CLIENT RX callback copies header plus payload into
   ``mbox_to_process_msgq``.
4. ``CLIENT_Process_Task`` dequeues, increments ``counter``, refreshes
   ``timestamp``, and sends the header back to the HOST.
5. The HOST RX callback pushes the header into ``mbox_to_hid_msgq``.
6. ``HID_Task`` dequeues and emits USB HID reports (keyboard key selected by
   ``source``; a short mouse report burst runs first for bring-up).

Initialization on both cores:

1. Resolve DT mailbox channels (tx/rx) from ``DT_PATH(mbox_consumer)``.
2. Validate MTU against ``sizeof(struct mbox_message)`` (the HOST also needs
   room for the appended I2C payload).
3. Register the RX callback and enable the RX channel.
4. HOST only: initialize input devices, run the GPIO reset sequence, and start
   the USB HID and CDC stack services.
5. Start worker threads.

Requirements
************

- A dual-core M4/M55 board that supports MBOX communication, with a board
  target per core. Validated on ``sr100_rdk/sr100/m4`` and
  ``sr100_rdk/sr100/m55``.
- Two I2C input devices connected to the HOST core (addresses ``0x0A`` and
  ``0x42``) with their data-ready and reset GPIOs wired. Without them the HOST
  logs I2C errors and no mailbox traffic is generated.
- A USB connection to a host PC to observe the HID reports.

Configuration
*************

- ``CONFIG_IPC_CLIENT_LOG_EVERY_N`` (CLIENT): log every N processed messages.
- ``CONFIG_IPC_CLIENT_LOG_HEXDUMP`` (CLIENT): hexdump the forwarded I2C payload.
- ``CONFIG_SAMPLE_HID_EVENT_DIVIDER`` (HOST): decimate HID keyboard events.
- ``DEBUG_RUNTIME_LOGS`` (HOST, set in ``CMakeLists.txt``): enable additional
  mailbox/HID diagnostic counters and logs.

Building and Running
********************

Build both firmware images (see ``build_app.sh``). The CLIENT
image is built first because the HOST build references it:

.. zephyr-app-commands::
   :zephyr-app: samples/ipc_data_pipeline/remote
   :board: sr100_rdk/sr100/m4
   :goals: build
   :west-args: -p always -d build/m4

.. zephyr-app-commands::
   :zephyr-app: samples/ipc_data_pipeline/
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always -d build/m55 -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD=../m4

``CONFIG_SR100_RELEASE_M4_RESET=y`` lets the M55 image release the M4 core
from reset; on other SoCs use the equivalent option.

Open a serial terminal (minicom, putty, etc.) for each core and connect with
the following settings:

- Speed: 230400
- Data: 8 bits
- Parity: None
- Stop bits: 1

Reset the board. The HOST (M55) console shows pipeline start-up and HID
activity:

.. code-block:: console

   *** Booting Zephyr OS ***
   <inf> ipc_host: Dualcore mbox data HOST SERVER - sr100_rdk/sr100/m55
   <inf> ipc_host: [InputDev1_Task] Started on HOST
   <inf> ipc_host: [InputDev2_Task] Started on HOST
   <inf> ipc_host: [HID_Task] Started on HOST
   <inf> ipc_host: HOST threads started (2 input devices). Main thread sleeping.
   <inf> ipc_host: [HID_Task] Mouse test send 1/10 (source=0 counter=1)
   ...
   <inf> ipc_host: [HID_Task] Mouse test complete (10 reports), resuming keyboard A/B
   <inf> ipc_host: [HID_Task] RX ch 1 source 0 counter 15 time: 00:00:02.100

The CLIENT (M4) console shows the processing loop:

.. code-block:: console

   *** Booting Zephyr OS ***
   <inf> ipc_client: Dualcore mbox data CLIENT CLIENT - sr100_rdk/sr100/m4
   <inf> ipc_client: [CLIENT_Process_Task] Started on CLIENT
   <inf> ipc_client: CLIENT thread started. Main thread sleeping.
   <inf> ipc_client: [CLIENT_Process_Task] Received from HOST (rx ch 1) counter: 0, time: 00:00:01.000
   <inf> ipc_client: [CLIENT_Process_Task] Source interface: 0
   <inf> ipc_client: [CLIENT_Process_Task] Sending to HOST (tx ch 0) counter: 1, time: 00:00:01.000
   ...
