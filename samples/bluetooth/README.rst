Bluetooth Samples
=================
::

    .. zephyr:code-sample:: bluetooth_samples
        :name: Bluetooth samples
        :relevant-api: bluetooth

        These sample applications demonstrate Bluetooth functionality on the
        Synaptics SR110 RDK using Zephyr Bluetooth APIs.

Description
***********

This directory contains Bluetooth sample applications for the Synaptics
SR110 RDK.

The following Bluetooth sample applications are currently supported:

* ``bt_shell``
* ``peripheral``
* ``peripheral_nus``
* ``classic_a2dp_source``
* ``central_syna_otc``
* ``peripheral_syna_ots``

Requirements
************

These samples require a Synaptics SR110 RDK with a supported SYNA Bluetooth
controller connected to the host over H:4 UART.

Supported board targets:

* RDK platform: ``sr100_rdk/sr100/m55``

Wiring
******

The BT controller uses the SR110 RDK UART and BT_REG_ON GPIO connections.
Refer to the SR110 RDK hardware documentation for connector and pin details.

Building and Running
********************

For :zephyr:sample:`Bluetooth Shell <bt_shell>`, build the bt shell Application
using the following command::

    west build -p -b sr100_rdk/sr100/m55 \
        samples/bluetooth/bt_shell \
        -d build_samples/bt_shell

The ``bt_shell`` application can be used to initialize Bluetooth, advertise, scan
for Bluetooth devices, and establish Bluetooth connections.

For :zephyr:sample:`Bluetooth LE Peripheral <peripheral>`, build the peripheral
Application using the following command::

    west build -p -b sr100_rdk/sr100/m55 \
        samples/bluetooth/peripheral \
        -d build_samples/peripheral

The ``peripheral`` application runs the SR110 platform as a Bluetooth LE Peripheral
and GATT Server. Use a mobile Bluetooth application to discover and verify GATT
services and characteristics.

For :zephyr:sample:`Bluetooth LE UART Service <peripheral_nus>`, build the
peripheral_nus Application using the following command::

    west build -p -b sr100_rdk/sr100/m55 \
        samples/bluetooth/peripheral_nus \
        -d build_samples/peripheral_nus

The ``peripheral_nus`` application runs the SR110 platform as a Bluetooth LE Peripheral
using the UART Service to exchange data between a mobile Bluetooth client and
the target.

For :zephyr:sample:`Bluetooth Classic A2DP Source <classic_a2dp_source>`, build the
classic_a2dp_source Application using the following command::

    west build -p -b sr100_rdk/sr100/m55 \
        samples/bluetooth/classic_a2dp_source \
        -d build_samples/classic_a2dp_source

The ``classic_a2dp_source`` application runs the SR110 RDK as a Bluetooth
Classic A2DP Source to stream audio to a connected A2DP Sink device.

A test sine-wave signal can be used to verify that audio is successfully
streamed from the SR110 platform to the connected headset.

For :zephyr:sample:`Bluetooth LE Central OTC <central_syna_otc>`, build the
central_syna_otc Application using the following command::

    west build -p -b sr100_rdk/sr100/m55 \
        samples/bluetooth/central_syna_otc \
        -d build_samples/central_syna_otc

The ``central_syna_otc`` application runs the SR110 platform as a Bluetooth LE
Central OTS Client to discover, read, write, create, and delete objects on a
connected OTS Server device.

For :zephyr:sample:`Bluetooth LE Peripheral OTS <peripheral_syna_ots>`, build the
peripheral_syna_ots Application using the following command::

    west build -p -b sr100_rdk/sr100/m55 \
        samples/bluetooth/peripheral_syna_ots \
        -d build_samples/peripheral_syna_ots

The ``peripheral_syna_ots`` application runs the SR110 platform as a Bluetooth LE
Peripheral OTS Server to provide object storage for a connected OTS Client to
discover, read, and write objects.

Sample output
*************

bt_shell
--------

The following output shows Bluetooth initialization, BLE advertising,
connection, disconnection, and device scanning::

    [00:00:00.009,000] <inf> syna_bt_hci: APBL version: 0.1.0 (0x00000100)
    [00:00:00.016,000] <inf> syna_bt_hci: HW Boot source: 0x5
    [00:00:00.022,000] <inf> syna_bt_hci: SPI slave boot
    [00:00:07.157,000] <inf> downloader: cmd start: opcode=0x12
    [00:00:09.367,000] <inf> downloader: cmd done: opcode=0x12 rc=0
    [00:00:09.379,000] <inf> downloader: cmd start: opcode=0x0B
    [00:00:09.385,000] <inf> loader: Jump fw-003 @ 12000800

    uart:~$ bt init
    [00:00:05.199,000] <err> bt_driver: Unknown H:4 type 0x00
    [00:00:22.138,000] <wrn> bt_hci_core: Num of Controller's ACL packets != ACL bt_conn_tx contexts (15 != 3)
    Bluetooth initialized
    uart:~$ bt advertise on
    Advertising started
    Connected: 51:D5:E2:1B:B8:E7 (random)
    LE conn param updated: int 0x0027 lat 0 to 42
    Disconnected: 51:D5:E2:1B:B8:E7 (random) (reason 0x08)
    uart:~$
    uart:~$ bt scan on
    Bluetooth active scan enabled
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 0, RSSI -97  C:1 S:1 D:0 SR:0 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 4, RSSI -97 TY C:0 S:1 D:0 SR:1 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 0, RSSI -97  C:1 S:1 D:0 SR:0 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 4, RSSI -99 TY C:0 S:1 D:0 SR:1 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 0, RSSI -98  C:1 S:1 D:0 SR:0 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 4, RSSI -97 TY C:0 S:1 D:0 SR:1 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 0, RSSI -98  C:1 S:1 D:0 SR:0 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 4, RSSI -99 TY C:0 S:1 D:0 SR:1 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 0, RSSI -97  C:1 S:1 D:0 SR:0 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 4, RSSI -97 TY C:0 S:1 D:0 SR:1 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 0, RSSI -97  C:1 S:1 D:0 SR:0 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    [DEVICE]: C0:F8:53:5C:3A:E8 (public), AD evt type 4, RSSI -97 TY C:0 S:1 D:0 SR:1 E:0 Prim: LE 1M, Secn: No packets, Interval: 0x0000 (0 us), SID: 0xff
    uart:~$ bt scan off
    Scan successfully stopped
    uart:~$

peripheral
----------

The following output shows Bluetooth initialization, BLE advertising,
GATT connection, MTU negotiation, GATT notifications, and security
pairing::

    High alert started
    *** Booting Zephyr OS build v4.4.0 ***
    [00:00:00.009,000] <inf> syna_bt_hci: APBL version: 0.1.0 (0x00000100)
    [00:00:00.016,000] <inf> syna_bt_hci: HW Boot source: 0x5
    [00:00:00.022,000] <inf> syna_bt_hci: SPI slave boot
    [00:00:15.202,000] <inf> downloader: cmd start: opcode=0x12
    [00:00:17.150,000] <inf> downloader: cmd done: opcode=0x12 rc=0
    [00:00:17.162,000] <inf> downloader: cmd start: opcode=0x0B
    [00:00:17.168,000] <inf> loader: Jump fw-003 @ 12000800
    [00:00:00.002,000] <err> syna_dmic: DMIC clock controller is not ready
    [00:00:00.010,000] <inf> pmic_yukon: Yukon APSRAM power: vlogic=800 mV, vdd2_1v1=1200 mV
    *** Booting Zephyr OS build v4.4.0 ***
    [00:00:00.154,000] <err> blinfo_mcuboot: MCUboot data load failed, expected magic value: 0x2016, got: 0xb83d
    [00:00:00.191,000] <err> bt_driver: Unknown H:4 type 0x00
    [00:00:00.749,000] <inf> syna_bt_hci: Downloading SYNA patchram (164068 bytes) via baudrate switch to 1500000
    [00:00:00.762,000] <inf> syna_bt_hci: Host UART baudrate -> 1500000 bps
    [00:00:02.316,000] <inf> syna_bt_hci: Patchram download complete (LAUNCH_RAM sent at 1500000 bps)
    [00:00:02.325,000] <inf> syna_bt_hci: Host UART baudrate -> 115200 bps
    [00:00:02.598,000] <inf> syna_bt_hci: Host UART baudrate -> 1500000 bps
    [00:00:02.605,000] <inf> syna_bt_hci: SYNA initialized, HCI running at 1500000 bps
    [00:00:02.626,000] <wrn> bt_hci_core: Num of Controller's ACL packets != ACL bt_conn_tx contexts (15 != 3)
    [00:00:02.641,000] <inf> bt_hci_core: HCI transport: H:4
    [00:00:02.646,000] <inf> bt_hci_core: Identity: C0:F5:35:DC:DC:5C (public)
    [00:00:02.654,000] <inf> bt_hci_core: HCI: version 6.0 (0x0e) revision 0x04ce, manufacturer 0x000f
    [00:00:02.663,000] <inf> bt_hci_core: LMP: version 6.0 (0x0e) subver 0x220a
    Bluetooth initialized
    Advertising successfully started
    Indicate VND attr 0x1203b430 (UUID 12345678-1234-5678-1234-56789abcdef1)
    Updated MTU: TX: 23 RX: 23 bytes
    Connected
    [00:00:24.369,000] <inf> bas: BAS Notifications enabled
    [00:00:31.730,000] <inf> cts: CTS Notifications enabled
    [00:00:37.872,000] <inf> hrs: HRS notifications enabled
    Mild alert started
    High alert started
    Passkey for 4D:95:FE:FA:A5:DB (random): 527924

peripheral_nus
--------------

The following output shows Bluetooth initialization, BLE advertising,
GATT connection, UART Service communication, and data exchange
between the mobile device and the target::

    *** Booting Zephyr OS build v4.4.0 ***
    [00:00:00.009,000] <inf> syna_bt_hci: APBL version: 0.1.0 (0x00000100)
    [00:00:00.016,000] <inf> syna_bt_hci: HW Boot source: 0x5
    [00:00:00.022,000] <inf> syna_bt_hci: SPI slave boot
    [00:10:03.985,000] <inf> downloader: cmd start: opcode=0x12
    [00:10:05.790,000] <inf> downloader: cmd done: opcode=0x12 rc=0
    [00:10:05.803,000] <inf> downloader: cmd start: opcode=0x0B
    [00:10:05.809,000] <inf> loader: Jump fw-003 @ 12000800
    [00:00:00.002,000] <err> syna_dmic: DMIC clock controller is not ready
    [00:00:00.010,000] <inf> pmic_yukon: Yukon APSRAM power: vlogic=800 mV, vdd2_1v1=1200 mV
    *** Booting Zephyr OS build v4.4.0 ***
    [00:00:00.156,000] <err> blinfo_mcuboot: MCUboot data load failed, expected magic value: 0x2016, got: 0xb83d
    Sample - Bluetooth Peripheral NUS
    [00:00:00.196,000] <err> bt_driver: Unknown H:4 type 0x00
    [00:00:00.754,000] <inf> syna_bt_hci: Downloading SYNA patchram (164068 bytes) via baudrate switch to 1500000
    [00:00:00.767,000] <inf> syna_bt_hci: Host UART baudrate -> 1500000 bps
    [00:00:02.321,000] <inf> syna_bt_hci: Patchram download complete (LAUNCH_RAM sent at 1500000 bps)
    [00:00:02.331,000] <inf> syna_bt_hci: Host UART baudrate -> 115200 bps
    [00:00:02.604,000] <inf> syna_bt_hci: Host UART baudrate -> 1500000 bps
    [00:00:02.611,000] <inf> syna_bt_hci: SYNA initialized, HCI running at 1500000 bps
    [00:00:02.632,000] <wrn> bt_hci_core: Num of Controller's ACL packets != ACL bt_conn_tx contexts (15 != 3)
    [00:00:02.645,000] <inf> bt_hci_core: HCI transport: H:4
    [00:00:02.650,000] <inf> bt_hci_core: Identity: C0:F5:35:DC:DC:5C (public)
    [00:00:02.658,000] <inf> bt_hci_core: HCI: version 6.0 (0x0e) revision 0x04ce, manufacturer 0x000f
    [00:00:02.667,000] <inf> bt_hci_core: LMP: version 6.0 (0x0e) subver 0x220a
    Initialization complete
    Data send - Result: -128
    Data send - Result: -128
    Data send - Result: -128
    Data send - Result: -128
    Data send - Result: -128
    received() - Len: 11, Message: test start
    Data send - Result: -128
    Data send - Result: -128
    Data send - Result: -128
    Data send - Result: -128
    Data send - Result: -128
    Data send - Result: -128
    Data send - Result: -128
    received() - Len: 10, Message: check test
    Data send - Result: -128
    Data send - Result: -128
    received() - Len: 13, Message: test success
    Data send - Result: -128

classic_a2dp_source
-------------------

The following output shows Bluetooth Classic initialization, device
discovery, A2DP connection establishment, SBC encoding, and audio
streaming to the connected A2DP Sink::

    Choice: Choice: *** Booting Zephyr OS build v4.4.0 ***
    [00:00:00.009,000] <inf> syna_bt_hci: APBL version: 0.1.0 (0x00000100)
    [00:00:00.016,000] <inf> syna_bt_hci: HW Boot source: 0x5
    [00:00:00.022,000] <inf> syna_bt_hci: SPI slave boot
    [00:00:05.495,000] <inf> downloader: cmd start: opcode=0x12
    [00:00:07.701,000] <inf> downloader: cmd done: opcode=0x12 rc=0
    [00:00:07.713,000] <inf> downloader: cmd start: opcode=0x0B
    [00:00:07.719,000] <inf> loader: Jump fw-003 @ 12000800
    [00:00:00.002,000] <err> syna_dmic: DMIC clock controller is not ready
    [00:00:00.009,000] <inf> pmic_yukon: Yukon APSRAM power: vlogic=800 mV, vdd2_1v1=1200 mV
    *** Booting Zephyr OS build v4.4.0 ***
    [00:00:00.154,000] <err> blinfo_mcuboot: MCUboot data load failed, expected magic value: 0x2016, got: 0xb83d
    Starting A2DP Source - Synaptics SR110
    [00:00:00.195,000] <err> bt_driver: Unknown H:4 type 0x00
    [00:00:00.752,000] <inf> syna_bt_hci: Downloading SYNA patchram (164068 bytes) via baudrate switch to 1500000
    [00:00:00.765,000] <inf> syna_bt_hci: Host UART baudrate -> 1500000 bps
    [00:00:02.319,000] <inf> syna_bt_hci: Patchram download complete (LAUNCH_RAM sent at 1500000 bps)
    [00:00:02.329,000] <inf> syna_bt_hci: Host UART baudrate -> 115200 bps
    [00:00:02.602,000] <inf> syna_bt_hci: Host UART baudrate -> 1500000 bps
    [00:00:02.609,000] <inf> syna_bt_hci: SYNA initialized, HCI running at 1500000 bps
    [00:00:02.629,000] <wrn> bt_hci_core: Num of Controller's ACL packets != ACL bt_conn_tx contexts (15 != 4)
    [00:00:02.659,000] <inf> bt_hci_core: HCI transport: H:4
    [00:00:02.665,000] <inf> bt_hci_core: Identity: C0:F5:35:DC:DC:5C (public)
    [00:00:02.672,000] <inf> bt_hci_core: HCI: version 6.0 (0x0e) revision 0x04ce, manufacturer 0x000f
    [00:00:02.682,000] <inf> bt_hci_core: LMP: version 6.0 (0x0e) subver 0x220a

    --- Scanning for BT devices (10 s) ---
    Found: 1C:D1:07:57:6C:EE  rssi=-87  (no name)
    Found: 1C:D1:07:57:6C:EE  rssi=-87  (no name)
    Found: 1C:D1:07:57:6C:EE  rssi=-91  (no name)
    Found: B0:DC:EF:84:95:5D  rssi=-80  6G72QV3
    Found: 1C:D1:07:57:6C:EE  rssi=-89  (no name)
    Found: 1C:D1:07:57:6C:EE  rssi=-85  realme X7 5G
    Scan complete: 2 device(s)

    --- Device list (2 found) ---
    [0] 1C:D1:07:57:6C:EE  rssi=-85  realme X7 5G
    [1] B0:DC:EF:84:95:5D  rssi=-80  6G72QV3
    [r] Rescan
    Choice: 0
    Connecting to 1C:D1:07:57:6C:EE...
    [00:00:22.409,000] <inf> a2dp_source: ACL connected: 1C:D1:07:57:6C:EE
    [00:00:22.416,000] <inf> a2dp_source: Opening A2DP signaling...
    Connected. Waiting for disconnect...
    [00:00:26.243,000] <inf> a2dp_source: A2DP disconnected
    [00:00:26.249,000] <wrn> bt_l2cap_br: Invalid ACL conn for chan 0x12078b38
    [00:00:26.322,000] <inf> a2dp_source: ACL disconnected: 1C:D1:07:57:6C:EE (reason 0x16)
    Disconnected.

    --- Device list (2 found) ---
    [0] 1C:D1:07:57:6C:EE  rssi=-85  realme X7 5G
    [1] B0:DC:EF:84:95:5D  rssi=-80  6G72QV3
    [r] Rescan
    Choice: r

    --- Scanning for BT devices (10 s) ---

    --- Device list (0 found) ---
    [r] Rescan
    Choice:   Found: 1C:D1:07:57:6C:EE  rssi=-87  (no name)
    Found: 1C:D1:07:57:6C:EE  rssi=-91  realme X7 5G
    Found: B0:DC:EF:84:95:5D  rssi=-85  6G72QV3
    Found: C4:CB:BE:04:D1:C7  rssi=-57  (no name)
    Found: C4:CB:BE:04:D1:C7  rssi=-58  realme Buds T01
    Scan complete: 3 device(s)
    Choice: 3
    Out of range (0-2).
    Choice: 2
    Connecting to C4:CB:BE:04:D1:C7...
    [00:01:02.901,000] <inf> a2dp_source: ACL connected: C4:CB:BE:04:D1:C7
    [00:01:02.908,000] <inf> a2dp_source: Opening A2DP signaling...
    Connected. Waiting for disconnect...
    [00:01:03.373,000] <wrn> bt_l2cap_br: Proposed MTU is the same as given value (1020 == 1020)
    [00:01:03.382,000] <inf> a2dp_source: A2DP signaling connected � discovering remote endpoints...
    [00:01:03.411,000] <inf> a2dp_source: Discovered EP: codec=0x00 tsep=1
    [00:01:03.418,000] <inf> a2dp_source: Found remote SBC SINK EP � will configure
    [00:01:03.443,000] <inf> a2dp_source: Configuring: local tsep=0  remote tsep=1
    [00:01:03.477,000] <inf> a2dp_source: Stream configured (sf=44100 nc=2), opening...
    [00:01:03.521,000] <wrn> bt_l2cap_br: Proposed MTU is the same as given value (1020 == 1020)
    [00:01:03.530,000] <inf> a2dp_source: Stream established, starting...
    [00:01:03.579,000] <inf> a2dp_source: Stream STARTED � playing 441 Hz tone for 100 s
    [00:01:03.598,000] <inf> a2dp_source: audio[1]: elapsed=10 fps=128 fenc=83 mtu=672
    [00:01:03.605,000] <inf> a2dp_source: audio[1]: total=384 frame_num=3
    [00:01:03.612,000] <inf> a2dp_source: audio[1]: sending 3 frames pdu=250
    [00:01:03.622,000] <inf> a2dp_source: audio[1]: sent seq=0 ts=0
    [00:01:03.718,000] <inf> a2dp_source: audio[11]: elapsed=10 fps=128 fenc=83 mtu=672
    [00:01:03.726,000] <inf> a2dp_source: audio[11]: total=384 frame_num=3
    [00:01:03.733,000] <inf> a2dp_source: audio[11]: sending 3 frames pdu=250
    [00:01:03.742,000] <inf> a2dp_source: audio[11]: sent seq=10 ts=5248
    [00:01:03.838,000] <inf> a2dp_source: audio[21]: elapsed=10 fps=128 fenc=83 mtu=672
    [00:01:03.846,000] <inf> a2dp_source: audio[21]: total[00:01:03.851,000] <wrn> bt_driver: Failed to allocate, deferring to rx_thread
    =512 frame_num=4
    [00:01:03.861,000] <inf> a2dp_source: audio[21]: sending 4 frames pdu=333
    [00:01:03.871,000] <inf> a2dp_source: audio[21]: sent seq=20 ts=10496
    [00:01:03.887,000] <wrn> bt_driver: Failed to allocate, deferring to rx_thread
    [00:01:03.958,000] <inf> a2dp_so[00:01:03.961,000] <wrn> bt_driver: Failed to allocate, deferring to rx_thread
    urce: audio[31]: elapsed=10 fps=128 fenc=83 mtu=672
    [00:01:03.974,000] <inf> a2dp_source: audio[31]: total=384 frame_num=3
    [00:01:03.981,000] <inf> a2dp_source: audio[31]: sending 3 frames pdu=250
    [00:01:03.990,000] <inf> a2dp_source: audio[31]: sent seq=30 ts=15872
    [00:01:04.006,000] <wrn> bt_driver: Failed to allocate, deferring to rx_thread
    [00:01:04.078,000] <inf> a2dp_source: audio[41]: elapsed=10 fps=128 fenc=83 mtu=672
    [00:01:04.086,000] <inf> a2dp_source: audio[41]: total=384 frame_num=3
    [00:01:04.093,000] <inf> a2dp_source: audio[41]: sending 3 frames pdu=250
    [00:01:04.102,000] <inf> a2dp_source: audio[41]: sent seq=40 ts=21120
    [00:01:04.198,000] <inf> a2dp_source: audio[51]: elapsed=10 fps=128 fenc=83 mtu=672
    [00:01:04.206,000] <inf> a2dp_source: audio[51]: total[00:01:04.211,000] <wrn> bt_driver: Failed to allocate, deferring to rx_thread
    =512 frame_num=4
    [00:01:04.221,000] <inf> a2dp_source: audio[51]: sending 4 frames pdu=333
    [00:01:04.231,000] <inf> a2dp_source: audio[51]: sent seq=50 ts=26368
    [00:01:04.247,000] <wrn> bt_driver: Failed to allocate, deferring to rx_thread
    [00:01:04.318,000] <inf> a2dp_source: audio[61]: elapsed=10 fps=128 fenc=83 mtu=672
    [00:01:04.326,000] <inf> a2dp_source: a[00:01:04.329,000] <wrn> bt_driver: Failed to allocate, deferring to rx_thread
    udio[61]: total=384 frame_num=3
    [00:01:04.341,000] <inf> a2dp_source: audio[61]: sending 3 frames pdu=250
    [00:01:04.350,000] <inf> a2dp_source: audio[61]: sent seq=60 ts=31744
    [00:01:04.366,000] <wrn> bt_driver: Failed to allocate, deferring to rx_thread
    [00:01:04.438,000] <inf> a2dp_source: audio[71]: elapsed=10 fps=128 fenc=83 mtu=672
    [00:01:04.446,000] <inf> a2dp_source: audio[71]: total=384 frame_num=3
    [00:01:04.453,000] <inf> a2dp_source: audio[71]: sending 3 frames pdu=250
    [00:01:04.462,000] <inf> a2dp_source: audio[71]: sent seq=70 ts=36992
    [00:01:04.558,000] <inf> a2dp_source: audio[81]: elapsed=10 fps=128 fenc=83 mtu=672
    [00:01:04.566,000] <inf> a2dp_source: audio[81]: total=512 frame_num=4
    [00:01:04.573,000] <inf> a2dp_source: audio[81]: sending 4 frames pdu=333
    [00:01:04.583,000] <inf> a2dp_source: audio[81]: sent seq=80 ts=42240
    [00:01:04.678,000] <inf> a2dp_source: audio[91]: elapsed=10 fps=128 fenc=83 mtu=672
    [00:01:04.686,000] <inf> a2dp_source: audio[91]: total=384 frame_num=3
    [00:01:04.693,000] <inf> a2dp_source: audio[91]: sending 3 frames pdu=250
    [00:01:04.702,000] <inf> a2dp_source: audio[91]: sent seq=90 ts=47616
    [00:01:04.798,000] <inf> a2dp_source: audio[101]: elapsed=10 fps=128 fenc=83 mtu=672
    ...
    .
    .
    .
    [00:02:22.282,000] <wrn> bt_driver: Failed to allocate, deferring to rx_thread
    [00:02:22.293,000] <wrn> bt_driver: Failed to allocate, deferring to rx_thread
    [00:02:22.351,000] <inf> a2dp_source: Stream released
    [00:02:22.356,000] <inf> a2dp_source: A2DP disconnected
    [00:02:22.563,000] <inf> a2dp_source: ACL disconnected: C4:CB:BE:04:D1:C7 (reason 0x13)
    Disconnected.

    --- Device list (3 found) ---
    [0] 1C:D1:07:57:6C:EE  rssi=-91  realme X7 5G
    [1] B0:DC:EF:84:95:5D  rssi=-85  6G72QV3
    [2] C4:CB:BE:04:D1:C7  rssi=-58  realme Buds T01
    [r] Rescan
    Choice:
    Invalid � enter 0-2 or r.
    Choice: s
    Invalid � enter 0-2 or r.
    Choice: Choice:

central_syna_otc
----------------

The following output shows Bluetooth initialization, BLE scanning, connection
to an OTS Server, GATT discovery, object selection, metadata read, object
data read, object creation, name setting, object write, object deletion,
and disconnection::

    *** Booting Zephyr OS build v4.4.0 ***
    [00:00:00.200,000] <inf> syna_bt_hci: APBL version: 0.1.0 (0x00000100)
    [00:00:00.207,000] <inf> syna_bt_hci: HW Boot source: 0x5
    [00:00:00.213,000] <inf> syna_bt_hci: SPI slave boot
    [00:00:07.922,000] <inf> downloader: cmd start: opcode=0x12
    [00:00:10.417,000] <inf> downloader: cmd done: opcode=0x12 rc=0
    [00:00:10.427,000] <inf> downloader: cmd start: opcode=0x0B
    [00:00:10.433,000] <inf> loader: Jump fw-003 @ 12001000
    [00:00:00.003,000] <err> syna_dmic: DMIC clock controller is not ready
    [00:00:00.010,000] <inf> pmic_yukon: Yukon APSRAM power: vlogic=800 mV, vdd2_1v1=1275 mV
    *** Booting Zephyr OS build v4.4.0 ***
    Build: Aug 19 2026 14:43:25
    Board: sr100_rdk
    Configured BT HCI baudrate: 1500000

    === BLE OTS Central Client - sr100_rdk ===
    [00:00:00.224,000] <err> bt_driver: Unknown H:4 type 0x00
    [00:00:00.782,000] <inf> syna_bt_hci: Downloading SYNA patchram (164068 bytes) via baudrate switch to 1500000
    [00:00:00.795,000] <inf> syna_bt_hci: Host UART baudrate -> 1500000 bps
    [00:00:02.487,000] <inf> syna_bt_hci: Patchram download complete (LAUNCH_RAM sent at 1500000 bps)
    [00:00:02.497,000] <inf> syna_bt_hci: Host UART baudrate -> 115200 bps
    [00:00:02.769,000] <inf> syna_bt_hci: Host UART baudrate -> 1500000 bps
    [00:00:02.776,000] <inf> syna_bt_hci: SYNA initialized, HCI running at 1500000 bps
    [00:00:02.798,000] <wrn> bt_hci_core: Num of Controller's ACL packets != ACL bt_conn_tx contexts (15 != 3)
    [00:00:02.813,000] <inf> bt_hci_core: HCI transport: H:4
    [00:00:02.818,000] <inf> bt_hci_core: Identity: C0:F5:35:DC:CA:AE (public)
    [00:00:02.826,000] <inf> bt_hci_core: HCI: version 6.0 (0x0e) revision 0x04ce, manufacturer 0x000f
    [00:00:02.835,000] <inf> bt_hci_core: LMP: version 6.0 (0x0e) subver 0x220a

    --- Scanning for BLE devices (5 sec) ---
    Scan complete: 20 device(s) found

    --- Device list (20 found) ---
      [0] 0E:B7:93:83:81:E6 (random)  rssi=-59  (no name)
      [1] 37:AD:00:E6:C2:90 (random)  rssi=-57  (no name)
      ...
      [13] C0:F5:35:AA:7A:1D (public)  rssi=-81  Zephyr OTS SR110
      ...
      [19] 12:6E:93:6A:F5:54 (random)  rssi=-77  (no name)
      [r] Rescan
    Choice: 13
    Connecting to C0:F5:35:AA:7A:1D (public)...
    [00:00:30.527,000] <inf> central_otc: Connected: C0:F5:35:AA:7A:1D (public)
    [00:00:30.935,000] <inf> central_otc: MTU exchange successful, negotiated MTU: 65 bytes
    [00:00:30.943,000] <inf> central_otc: Starting GATT discovery for OTS service...
    [00:00:31.002,000] <inf> central_otc: Found OTS service
    ...
    [00:00:32.112,000] <inf> central_otc: OTS Discovery Complete!
    [00:00:32.118,000] <inf> central_otc:   feature_handle: 0x0012
    [00:00:32.124,000] <inf> central_otc:   obj_name_handle: 0x0014
    [00:00:32.131,000] <inf> central_otc:   obj_type_handle: 0x0016
    [00:00:32.137,000] <inf> central_otc:   obj_size_handle: 0x0018
    [00:00:32.143,000] <inf> central_otc:   obj_id_handle: 0x001a
    [00:00:32.150,000] <inf> central_otc:   obj_properties_handle: 0x001c
    [00:00:32.157,000] <inf> central_otc:   oacp_handle (decl): 0x001e, value: 0x001f
    [00:00:32.165,000] <inf> central_otc:   olcp_handle (decl): 0x0021, value: 0x0022
    [00:00:32.173,000] <inf> central_otc: OTS discovery complete, subscribing...
    [00:00:32.180,000] <inf> central_otc: Subscribing to OACP...
    [00:00:32.186,000] <inf> central_otc: OACP subscribed OK
    [00:00:32.192,000] <inf> central_otc: Subscribing to OLCP...
    [00:00:32.198,000] <inf> central_otc: OLCP subscribed OK
    [00:00:32.204,000] <inf> central_otc: Reading OTS feature...

    === Connected to OTS server ===

    [QUICK TEST GUIDE]
    Read File:  Press [s] then [m] then [r]
    Write File: Press [c] then [t] then [w]

    [s] Select first object
    [n] Select next object
    [m] Read metadata
    [r] Read object data
    [w] Write object data
    [t] Set object name
    [c] Create new object
    [d] Delete current object
    [h] Help menu
    [q] Quit/Disconnect

    Choice: s
    Selecting first object...

    Choice: [00:00:33.755,000] <inf> central_otc: Object selected successfully (ID: 0x1000000000000)
    [00:00:33.995,000] <inf> bt_otc: Read Obj Id confirmed correct Obj Id
    [00:00:34.084,000] <inf> bt_otc: handle 28, length 4
    [00:00:34.090,000] <inf> bt_otc: Object properties (raw) : 0x4c

    === Object Metadata ===
    Object ID: 0x000000000100
    Object Name: first_object.txt
    Current size: 512 bytes
    Allocated size: 1024 bytes
    Properties: 0x004c (READ WRITE PATCH )
    r

    --- Reading Object Data ---
    Object ID: 0x000000000100
    Object Name: first_object.txt
    Object Size: 512 / 1024 bytes

    Data:
    Reading object data...

    Choice: [00:00:34.295,000] <inf> central_otc: Object data read: 20 bytes at offset 0 (partial)
    Data (offset 0): [01][02][03][04][05][06][07][08][09][0a][0b][0c][0d][0e][0f][10][11][12][13][14]
    [00:00:34.715,000] <inf> central_otc: Object data read: 20 bytes at offset 20 (partial)
    Data (offset 20): [15][16][17][18][19][1a][1b][1c][1d][1e][1f] !"#$%&'(
    ...
    [00:00:41.960,000] <inf> central_otc: Object data read: 12 bytes at offset 500 (complete)
    Data (offset 500): [f5][f6][f7][f8][f9][fa][fb][fc][fd][fe][ff][00]
    ==> Object data transfer complete!
    n
    Selecting next object...

    Choice: [00:00:43.085,000] <inf> central_otc: Object selected successfully (ID: 0x1000000000000)
    [00:00:43.579,000] <inf> bt_otc: Read Obj Id confirmed correct Obj Id
    [00:00:43.759,000] <inf> bt_otc: handle 28, length 4
    [00:00:43.765,000] <inf> bt_otc: Object properties (raw) : 0x4

    === Object Metadata ===
    Object ID: 0x000000000101
    Object Name: second_object.gif
    Current size: 1024 bytes
    Allocated size: 1024 bytes
    Properties: 0x0004 (READ )
    r

    --- Reading Object Data ---
    Object ID: 0x000000000101
    Object Name: second_object.gif
    Object Size: 1024 / 1024 bytes

    Data:
    Reading object data...

    Choice: [00:00:45.694,000] <inf> central_otc: Object data read: 255 bytes at offset 0 (partial)
    Data (offset 0): [00][02][04][06][08][0a][0c][0e][10][12][14][16][18][1a][1c][1e] "$&(*,.02468:<>@BDFHJLNPRTVXZ\^`bdfhjlnprtvxz|~[80][82][84][86][88][8a][8c][8e][90][92][94][96][98][9a][9c][9e][a0][a2][a4][a6][a8][aa][ac][ae][b0][b2][b4][b6][b8][ba]
    ...
    [00:00:47.585,000] <inf> central_otc: Object data read: 4 bytes at offset 1020 (complete)
    Data (offset 1020): [f8][fa][fc][fe]
    ==> Object data transfer complete!

    Choice: c

    --- Create New Object ---
    Enter object size in bytes (default 1024, press Enter to confirm):
    > 20
    [00:00:56.023,000] <inf> central_otc: Writing CREATE to handle 0x001e (21 bytes) using Write Command
    CREATE object request sent (size=20 bytes, handle=0x001e)
    Waiting for server to generate object ID...

    Choice: [00:00:56.404,000] <inf> central_otc: Object selected successfully (ID: 0x1000000000000)

    New object created successfully!
    Object ID: 0x1000000000000
    [00:00:56.419,000] <inf> central_otc: Reading metadata for newly created object...
    [00:00:56.765,000] <wrn> bt_otc: Obj size read returned a current size of 0
    [00:00:56.899,000] <inf> bt_otc: Read Obj Id confirmed correct Obj Id
    [00:00:56.989,000] <inf> bt_otc: handle 28, length 4
    [00:00:56.995,000] <inf> bt_otc: Object properties (raw) : 0x4d

    === Object Metadata ===
    Object ID: 0x000000000102
    Object Name:
    Current size: 0 bytes
    Allocated size: 1024 bytes
    Properties: 0x004d (READ WRITE PATCH )
    [00:00:57.015,000] <wrn> central_otc:   WARNING: Object name is empty (server may not have initialized object)

    Choice: t

    --- Set Object Name ---
    Enter object name (max 63 chars, press Enter to confirm):
    > aabbccddee
    [00:01:04.185,000] <inf> central_otc: Writing Object Name 'aabbccddee' to handle 0x0014 (10 bytes)
    Object name set to: aabbccddee

    Choice: w

    === Write Mode ===
    Use [s]/[n] to select objects, [r] to read, [w] to write, [d] to disconnect
    Press [w] again to write data to current object
    Enter data to write (max 255 chars, press Enter to finish):
    > aaaaaaaaaa

    --- Writing Object Data ---
    Object ID: 0x000000000102
    Object Name:
    Object Size: 0 / 1024 bytes
    Writing 10 bytes: aaaaaaaaaa
    Write operation initiated...

    Choice: [00:01:12.830,000] <inf> central_otc: Object data written: 10 bytes
    Write successful! Press [m] to refresh metadata.
    m
    Reading object metadata...

    Choice: [00:01:14.449,000] <inf> bt_otc: Read Obj Id confirmed correct Obj Id
    [00:01:14.584,000] <inf> bt_otc: handle 28, length 4
    [00:01:14.590,000] <inf> bt_otc: Object properties (raw) : 0x4d

    === Object Metadata ===
    Object ID: 0x000000000102
    Object Name: aabbccddee
    Current size: 10 bytes
    Allocated size: 1024 bytes
    Properties: 0x004d (READ WRITE PATCH )
    r

    --- Reading Object Data ---
    Object ID: 0x000000000102
    Object Name: aabbccddee
    Object Size: 10 / 1024 bytes

    Data:
    Reading object data...

    Choice: [00:01:15.394,000] <inf> central_otc: Object data read: 10 bytes at offset 0 (complete)
    Data (offset 0): aaaaaaaaaa
    ==> Object data transfer complete!

    Choice: d

    --- Delete Object ---
    Delete object 0x000000000102? (y/n): y
    [00:01:26.792,000] <inf> central_otc: Writing DELETE to handle 0x001e for object 0x000000000102 (1 byte)
    DELETE object request sent for 0x000000000102

    Choice: q
    [00:01:35.543,000] <inf> central_otc: Disconnecting...
    [00:01:35.555,000] <inf> central_otc: Disconnected: C0:F5:35:AA:7A:1D (public) (reason 0x16)
    Disconnected. Back to scan.

    --- Scanning for BLE devices (5 sec) ---
    Scan complete: 20 device(s) found

    --- Device list (20 found) ---
      [0] 15:7C:84:86:65:A6 (random)  rssi=-66  (no name)
      [1] 6D:0F:70:79:36:0C (random)  rssi=-79  (no name)
      ...
      [r] Rescan
    Choice:

peripheral_syna_ots
-------------------

The following output shows Bluetooth initialization, OTS object creation,
BLE advertising, connection from an OTC Client, object selection, object
data read, object creation by client, object name change, object write,
and disconnection::

    [00:00:04.932,000] <inf> downloader: cmd start: opcode=0x12
    [00:00:07.300,000] <inf> downloader: cmd done: opcode=0x12 rc=0
    [00:00:07.310,000] <inf> downloader: cmd start: opcode=0x0B
    [00:00:07.316,000] <inf> loader: Jump fw-003 @ 12001000
    [00:00:00.002,000] <err> syna_dmic: DMIC clock controller is not ready
    [00:00:00.010,000] <inf> pmic_yukon: Yukon APSRAM power: vlogic=800 mV, vdd2_1v1=1275 mV
    *** Booting Zephyr OS build v4.4.0 ***
    Starting Bluetooth Peripheral OTS example
    [00:00:00.216,000] <err> bt_driver: Unknown H:4 type 0x00
    [00:00:00.774,000] <inf> syna_bt_hci: Downloading SYNA patchram (164068 bytes) via baudrate switch to 1500000
    [00:00:00.787,000] <inf> syna_bt_hci: Host UART baudrate -> 1500000 bps
    [00:00:02.480,000] <inf> syna_bt_hci: Patchram download complete (LAUNCH_RAM sent at 1500000 bps)
    [00:00:02.489,000] <inf> syna_bt_hci: Host UART baudrate -> 115200 bps
    [00:00:02.762,000] <inf> syna_bt_hci: Host UART baudrate -> 1500000 bps
    [00:00:02.769,000] <inf> syna_bt_hci: SYNA initialized, HCI running at 1500000 bps
    [00:00:02.791,000] <wrn> bt_hci_core: Num of Controller's ACL packets != ACL bt_conn_tx contexts (15 != 3)
    [00:00:02.806,000] <inf> bt_hci_core: HCI transport: H:4
    [00:00:02.811,000] <inf> bt_hci_core: Identity: C0:F5:35:DC:CA:AE (public)
    [00:00:02.819,000] <inf> bt_hci_core: HCI: version 6.0 (0x0e) revision 0x04ce, manufacturer 0x000f
    [00:00:02.828,000] <inf> bt_hci_core: LMP: version 6.0 (0x0e) subver 0x220a
    Bluetooth initialized
    Object with 0x000000000100 ID has been created
    Object with 0x000000000101 ID has been created

    ========== OTS Object List ==========
      [0] ID=0x000000000100        alloc=1024  cur= 512  name="first_object.txt"
      [1] ID=0x000000000101        alloc=1024  cur=1024  name="second_object.gif"
    =====================================

    Advertising successfully started
    Connected
    Object with 0x000000000100 ID has been selected
    Object with 0x000000000100 ID is being read
    Offset = 0, Length = 20
    Object with 0x000000000100 ID is being read
    Offset = 20, Length = 20
    ...
    Object with 0x000000000100 ID is being read
    Offset = 500, Length = 12
    Object with 0x000000000101 ID has been selected
    Object with 0x000000000101 ID is being read
    Offset = 0, Length = 1024
    Object with 0x000000000102 ID has been created
    Object 0x000000000102 name changed: '' -> 'aabbccddee'
    Object 0x000000000102 name changed: 'aabbccddee' -> 'hjijh'
    Object with 0x000000000102 ID is being written
    Offset = 0, Length = 8, Remaining= 0
    Object with 0x000000000102 ID write complete, total 8 bytes
    Object with 0x000000000102 ID is being read
    Offset = 0, Length = 8
    Disconnected, reason 19

OTS / OTC object sizing
------------------------

For ``peripheral_syna_ots`` (OTS server) and ``central_syna_otc`` (OTC
client), the size of a single object and the number of objects the server
can hold are controlled by two **separate** configs:

* ``OBJ_MAX_SIZE`` — defined in each sample's ``src/main.c`` (currently
  1024 bytes on both sides). This is a sample-only define (not part of the
  Zephyr OTS stack) that sets the max size of a single object's data
  buffer. The OTC side's ``OBJ_MAX_SIZE`` must be large enough to hold the
  largest object the OTS server can serve.
* ``CONFIG_BT_OTS_MAX_OBJ_CNT`` — Zephyr OTS Kconfig (default ``0x05``),
  set in ``peripheral_syna_ots/prj.conf``. Controls how many objects the
  OTS server instance can manage at once.

**Important:** ``CONFIG_BT_OTS_MAX_OBJ_CNT`` counts ALL objects in the
shared pool, including the Directory Listing Object, which is
auto-reserved when ``CONFIG_BT_OTS_DIR_LIST_OBJ=y`` (enabled in this
sample). So with ``N`` configured, only ``N-1`` slots are actually
available for user-created objects via ``bt_ots_obj_add()`` (e.g. the
sample's ``add_photo_object()``). This was confirmed on real hardware:
setting ``CONFIG_BT_OTS_MAX_OBJ_CNT=5`` only allows 4 objects to be
created.

Static RAM used by the server's demo object pool is roughly
``OBJ_MAX_SIZE * CONFIG_BT_OTS_MAX_OBJ_CNT`` (plus per-object metadata).
Both values can be increased if larger or more objects are needed, but
check the available SRAM budget first — raising either one directly
increases the static memory footprint. See the comments in
``peripheral_syna_ots/prj.conf`` and ``central_syna_otc/prj.conf`` for
details.

Notes
-----

* Use a pristine build when switching between different source trees or
  workspaces.
* 4381(A1)(CONFIG_BT_4381) is selected by default, please add CONFIG_BT_4612
  if you want to use the 4612(A1) module instead.