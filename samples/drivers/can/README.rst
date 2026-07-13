.. zephyr:code-sample:: syna-can
   :name: Synaptics CAN
   :relevant-api: can_interface

   Exercise the Synaptics CAN/CAN FD controller with Zephyr's CAN API.

Description
***********

This sample validates one selected Synaptics CAN controller in internal loopback
mode. It uses the controller selected by the ``zephyr,canbus`` devicetree chosen
node and runs classic CAN, extended ID, CAN FD when supported, RX filter,
timing, state, stop/restart, statistics, and negative API checks.

The sample intentionally does not run dual-controller, external-bus, or
double-board tests.

Requirements
************

Hardware:

* Synaptics SL2619 RDK (``sl2619_rdk/sl2619/m52``)
* An enabled CAN controller selected by the ``zephyr,canbus`` chosen node
* No external CAN wiring is required for the default loopback coverage

Building and Running
********************

The code can be found in :zephyr_file:`zephyr_srsdk/samples/drivers/can`.

Build the default CAN0 loopback configuration from the workspace root:

.. zephyr-app-commands::
   :zephyr-app: zephyr_srsdk/samples/drivers/can
   :board: sl2619_rdk/sl2619/m52
   :goals: build
   :west-args: -p always
   :compact:

To build the same sample for CAN1, select the CAN1 overlay:

.. zephyr-app-commands::
   :zephyr-app: zephyr_srsdk/samples/drivers/can
   :board: sl2619_rdk/sl2619/m52
   :goals: build
   :west-args: -p always
   :gen-args: -DDTC_OVERLAY_FILE=boards/sl2619_rdk_sl2619_m52_can1.overlay
   :compact:

Flash and run using the normal board runner, then monitor the UART log. The
application exits the test sequence by printing ``CAN Sample App: PASS`` or a
failure reason.

RTR Coverage
************

Zephyr rejects incoming RTR frames by default because ``CONFIG_CAN_ACCEPT_RTR``
defaults to ``n``. The default build validates standard and extended RTR
rejection.

Build with ``CONFIG_CAN_ACCEPT_RTR=y`` to validate RTR receive behavior:

.. zephyr-app-commands::
   :zephyr-app: zephyr_srsdk/samples/drivers/can
   :board: sl2619_rdk/sl2619/m52
   :goals: build
   :west-args: -p always
   :gen-args: -DCONFIG_CAN_ACCEPT_RTR=y
   :compact:

Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.4.1 ***
   CAN Sample App
   can@48060000 capabilities: 0x2f loopback=1 fd=1 listen-only=1
   can@48060000 core clock: 200000000 Hz, bitrate range: 1..8000000 bps
   can@48060000 configuring: mode=0x5 loopback=1 fd=1
   can@48060000 state changed: error-active, rx_err=0, tx_err=0
   can@48060000 started state: error-active, rx_err=0, tx_err=0
   TX=can@48060000 RX=can@48060000 mode=0x5 loopback=1 fd=1
   can@48060000 initial state: error-active, rx_err=0, tx_err=0
   - RX filter: dev=can@48060000 handle=0 id=0x00000000 mask=0x00000000 flags=0x0 id_type=standard

   [TEST] classic standard loopback
   - TX device=can@48060000 timeout=1000 ms
   - TX request: id=0x00000123 (standard) type=classic dlc=8 bytes=8 flags=0x0 rtr=0 fdf=0 brs=0 esi=0
   - TX request payload[8]: 53 4c 32 36 31 78 43 41
   - RX queue purged
   - can_send accepted, waiting for TX callback
   - TX callback completed, error=0
   - RX frame: id=0x00000123 (standard) type=classic dlc=8 bytes=8 flags=0x0 rtr=0 fdf=0 brs=0 esi=0
   - RX frame payload[8]: 53 4c 32 36 31 78 43 41
   [PASS] classic standard loopback: id=0x123 dlc=8 flags=0x0

   CAN Sample App: PASS
