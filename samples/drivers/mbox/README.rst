.. zephyr:code-sample:: mbox
   :name: MBOX
   :relevant-api: mbox_interface

   Perform inter-processor mailbox communication using the MBOX API (with & without data).

Overview
********

The ``mbox`` and ``mbox_data`` samples demonstrate how to use the :ref:`MBOX API <mbox_api>`.

See ``zephyr/samples/drivers/mbox/README.rst`` and ``zephyr/samples/drivers/mbox_data/README.rst``.


Requirements
************

To retrieve messages from both cores M4 & M55 two UART connections are required on the RDK board
and two terminal sessions on the host (minicom, putty, etc.).


Wiring
******

Connect two UART-to-USB converters to the pins 13 + 14 on J24 (+ GND) for UART0 (M4) and 13 + 14 on
J25 (+ GND) for UART1 (M55), so there is a dedicated UART connection for each core.


Building and Running mbox
*************************

Build both firmware images for the M4 & M55:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/mbox/
   :board: sr100_rdk/sr100/m4
   :goals: build
   :west-args: -d build/m4

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/mbox/
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -d build/m55 -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD=../../build/m4


Sample Output for mbox
**********************

Reset the board and the following messages will appear on the corresponding serial port, one is the
M55 core and the other the M4:


.. code-block:: console

   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   Hello from REMOTE - sr100_rdk/sr100/m55
   Maximum RX channels: 8
   Maximum bytes of data in the TX message: 4
   Maximum TX channels: 8
   Pong (on channel 1)
   Ping (on channel 1)
   Pong (on channel 1)
   Ping (on channel 1)
   Pong (on channel 1)
   Ping (on channel 1)


.. code-block:: console

   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   Hello from HOST - sr100_rdk/sr100/m4
   Maximum RX channels: 8
   Maximum bytes of data in the TX message: 4
   Maximum TX channels: 8
   Ping (on channel 0)
   Pong (on channel 0)
   Ping (on channel 0)
   Pong (on channel 0)
   Ping (on channel 0)
   Pong (on channel 0)

Building and Running mbox_data
******************************

For this sample, the board devicetrees for the M4 and M55 define a shared memory area at the
end of the LP-MEMB area (see ``ipc_memory``, address ``0x3416f000``, size 4kB).

The size specified in the ``ipc0`` nodes (``shared-memory-size``) has to be twice as large as the
size used for messages by each core.

Please note that the size of the shared memory area in the board devicetree files is currently
set to 8, i.e., 4 bytes for each core (see ``ipc0`` node in ``sr100_rdk_m4.dts`` and
``sr100_rdk_m55.dts``). The sizes can be increased, but the ``mbox_data`` sample will fail if the
size is not 4 bytes.

Build both firmware images for the M4 & M55:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/mbox_data/
   :board: sr100_rdk/sr100/m4
   :goals: debug
   :west-args: -d build/m4

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/mbox_data/remote
   :board: sr100_rdk/sr100/m55
   :goals: debug
   :west-args: -d build/m55 -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD=../../build/m4

Please note that in this example, the server starts and waits for the first message from the
client. If the client is started before the server, this message will get lost and the test
will not proceed. Therefore, the M55 should be built as server (remote).

Sample Output for mbox_data
***************************

Reset the board and the following messages will appear on the corresponding serial port, one is the
M55 core and the other the M4:

.. code-block:: console

   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   mbox_data Client demo started
   Client send (on channel 3) value: 0
   Client received (on channel 2) value: 1
   Client send (on channel 3) value: 2
   Client received (on channel 2) value: 3
   Client send (on channel 3) value: 4
   ...
   Client received (on channel 2) value: 95
   Client send (on channel 3) value: 96
   Client received (on channel 2) value: 97
   Client send (on channel 3) value: 98
   Client received (on channel 2) value: 99
   mbox_data Client demo ended


.. code-block:: console

   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   mbox_data Server demo started
   Server receive (on channel 3) value: 0
   Server send (on channel 2) value: 1
   Server receive (on channel 3) value: 2
   Server send (on channel 2) value: 3
   Server receive (on channel 3) value: 4
   ...
   Server send (on channel 2) value: 95
   Server receive (on channel 3) value: 96
   Server send (on channel 2) value: 97
   Server receive (on channel 3) value: 98
   Server send (on channel 2) value: 99
   mbox_data Server demo ended.
