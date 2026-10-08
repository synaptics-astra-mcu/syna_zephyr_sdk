.. zephyr:code-sample:: synaptics_wifi_app_srw1500
   :name: Synaptics Wi-Fi Application (SRW1500)

   Wi-Fi app for SRW1500 using Zephyr Wi-Fi shell commands.

Description
***********

This sample enables the integrated Wi-Fi solution on SRW1500 and exposes
Zephyr Wi-Fi shell commands for scan, connect, disconnect, and status checks.

This application is intended for:

* validating integrated Wi-Fi,
* verifying AP association and DHCP, and running network sanity tests from
  the shell.

Requirements
************

* SRW1500 RDK board with integrated Wi-Fi.

Building and Running
********************

.. code-block:: console

   west build --pristine -b srw1500_rdk zephyr/samples/net/wifi/shell --sysbuild -- \
  -DEXTRA_CONF_FILE="$PWD/zephyr_srsdk/samples/net/wifi/shell/boards/srw1500_rdk.conf;\
$PWD/zephyr_srsdk/samples/net/wifi/shell/synaptics/overlay_srw1500.conf;\
$PWD/zephyr_srsdk/samples/net/wifi/shell/synaptics/overlay_srw1500_release.conf" \
  -DEXTRA_DTC_OVERLAY_FILE="$PWD/zephyr_srsdk/boards/syna/astra_sr/srw1500/srw1500_rdk_xip.overlay;\
$PWD/zephyr_srsdk/boards/syna/astra_sr/srw1500/uart0_ftdi_gpadc.overlay"

.. zephyr-app-commands::
   :zephyr-app: samples/net/wifi/shell
   :board: srw1500_rdk
   :goals: build
   :compact:

Sample Output
*************
Flash following the steps provided in index.rst.
After boot, open the UART console and use the following commands for Wi-Fi operations.

.. code-block:: console

  wifi ver
  net iface
  wifi scan
  wifi connect -s "<SSID>"
  wifi status
  net dhcpv4 client
  net dhcpv4 client start 1
  net iface
  wifi disconnect

.. code-block:: console

  uart:~$ wifi ver
  CHIP: 5312/1
  MHD: Version 5.0.24 (7e8525f-dirty) 90:CC:24:00:10:01 Sep 25 2026 04:13:21
  FW: wl0: Sep 21 2026 03:39:52 version 18.46.107 (1-g1f11639b+)

  uart:~$ net iface
  Hostname: zephyr
  Default interface: 1
  Interface wlan0 (WiFi) [1]
  ===============================
  Link addr : 90:CC:24:00:10:01
  MTU       : 1500
  Flags     : AUTO_START,IPv4,IPv6
  Device    : wlan0 (0x381224d8)
  Status    : oper=UP, admin=UP, carrier=ON
  Ethernet capabilities supported:
  Ethernet PHY device: <none> (0)
  IPv4 gateway : 0.0.0.0
  DHCPv4 state      : disabled

  uart:~$ wifi scan
  Scan requested
  Num  | SSID                             (len) | Chan (Band)   | RSSI | Security             | BSSID             | MFP
  1    | 2G                               2     | 6    (2.4GHz) | -58  | OPEN                 | 00:90:4C:4D:4A:A4 | Disable
  2    | 5G                               2     | 36   (5GHz  ) | -45  | OPEN                 | 00:90:4C:4B:F2:5B | Disable
  3    | 6G                               2     | 5    (6GHz  ) | -54  | UNKNOWN              | 00:90:4C:4C:86:16 | Disable
  Scan request done

  uart:~$ wifi connect -s 5G
  [... omitted: repetitive connection setup output ...]
  [00:00:56.481,000] <inf> wpa_supp: wlan0: CTRL-EVENT-CONNECTED - Connection to 00:90:4c:4b:f2:5b completed [id=0 id_str=]
  [00:00:56.514,000] <dbg> wpa_supp: wpa_printf_impl: Adaptor: mhd_wifi_api_set_supp_port dispatched
  Connected

  uart:~$ wifi status
  [00:01:08.723,000] <err> wpa_supp: wpa_drv_zep_signal_poll: signal_poll op not supported
  [00:01:08.742,000] <err> wpa_supp: wpa_drv_zep_get_conn_info: get_conn_info op not supported
  Status: successful
  State: COMPLETED
  Interface Mode: STATION
  Link Mode: WIFI 6 (802.11ax/HE)
  SSID: 5G
  BSSID: 00:90:4C:4B:F2:5B
  Band: 5GHz
  Channel: 36
  Security:  OPEN
  MFP: Disable
  RSSI: -9999
  Beacon Interval: 0
  DTIM: 0
  TWT: Not supported
  Current PHY TX rate (Mbps) : 0.0

  uart:~$ net dhcpv4 client
  client - DHCPv4 client management
           Usage: client
  Subcommands:
    start  : Start the DHCPv4 client operation on the interface
             Usage: start <index>
    stop   : Stop the DHCPv4 client operation on the interface
             Usage: stop <index>

  uart:~$ net dhcpv4 client start 1
  [00:01:28.852,000] <inf> net_dhcpv4: Received: 192.168.1.2

  uart:~$ net iface
  Default interface: 1
  Interface wlan0 (WiFi) [1]
  ===============================
  Link addr : 90:CC:24:00:10:01
  MTU       : 1500
  Flags     : AUTO_START,IPv4,IPv6
  Device    : wlan0 (0x381224d8)
  Status    : oper=UP, admin=UP, carrier=ON
  Ethernet capabilities supported:
  Ethernet PHY device: <none> (0)
  IPv4 unicast addresses:
          192.168.1.2/255.255.255.0 DHCP preferred
  IPv4 gateway : 192.168.1.1
  DHCPv4 lease time : 86400
  DHCPv4 renew time : 43200
  DHCPv4 server     : 192.168.1.1
  DHCPv4 requested  : 192.168.1.2
  DHCPv4 state      : bound
  DHCPv4 attempts   : 2
  DHCPv4 state      : bound

  uart:~$ wifi disconnect
  [... omitted: deauthentication and cleanup output ...]
  [00:01:43.587,000] <inf> wpa_supp: wlan0: CTRL-EVENT-DISCONNECTED bssid=00:90:4c:4b:f2:5b reason=3 locally_generated=1
  Disconnection request done (0)
  Disconnect requested
