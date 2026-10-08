.. zephyr:code-sample:: synaptics_wifi_app
   :name: Synaptics Wi-Fi Application (SR100)

   Wi-Fi app for SR100 RDK using Zephyr Wi-Fi shell commands.

Description
***********

This sample enables Synaptics Wi-Fi on SR100 and exposes Zephyr Wi-Fi shell
commands for firmware loading, scan, connect, disconnect, and status checks.

This application is intended for:

* validating SDIO-based Wi-Fi,
* verifying AP association and DHCP, running network sanity tests from shell.

Requirements
************

* SR100 RDK board with supported Wi-Fi module inserted on M.2 slot.

Building and Running
********************

.. code-block:: console

   west build -p always -b sr100_rdk/sr100/m55 ./samples/wifi/synaptics/

.. zephyr-app-commands::
   :zephyr-app: samples/wifi/synaptics
   :board: sr100_rdk/sr100/m55
   :goals: build
   :compact:

Flash/run using your normal runner, then monitor the UART log.

Sample Output
*************

Commands
========

After boot, open the UART console and use:

.. code-block:: console

  net cm up 2
  net iface 2
  wifi scan -i 2
  wifi connect -i 2 -s "<SSID>" -p "<PASSWORD>" -k <KEY_MGMT>
  wifi status -i 2
  net dhcpv4 client start 2
  net iface 2
  wifi disconnect -i 2

The following log captures a complete STA workflow on interface 2:

.. code-block:: console

  uart:~$ net cm up 2
  Taking iface 2 (0x32284f18 - unnamed - WiFi) admin-up.
  This command duplicates 'net iface up' if [target] != all.
  00000002.263:syna_mgmt_iface_start, 415
  [00:00:02.368,000] <inf> sd: Card does not support CMD8, assuming legacy card
  00000002.402:mhd_bus_init, 256
  00000002.405:[WiFi] Chip Info: ID=4381 REV=1
  00000002.409:[WiFi] Chip Soc: TYPE=1 PKG=2
  00000002.413:SDIOD_CCCR_DRIVE 00000007
  00000002.417:current is cr4 chip
  00000002.420:mhd_get_core_address, 18030000, 00000000
  00000002.429:mhd_get_core_address, 18030000, 00000000
  00000002.438:mhd_bus_sdio_download_firmware, [WiFi] Firmware Download
  00000002.444:mhd_bus_write_wifi_firmware_image, 791
  00000002.448:download_resource, 00740000, 980048
  00000002.453:download_resource, 967
  00000002.456:download_resource, 1046
  00000004.231:mhd_bus_sdio_download_firmware, [WiFi] Nvram Download
  00000004.237:[WiFi] Wlan RAM size: calculated[180000]
  00000004.242:download_resource, 008bdefc, 8448
  00000004.246:download_resource, 1046
  00000004.265:mhd_get_core_address, 18030000, 00000000
  00000004.274:mhd_get_core_address, 18030000, 00000000
  00000004.321:[WiFi] HT CLK Actived [42]
  00000004.498:[WiFi] Function 2 Ready
  00000004.501:[WiFi] Chip Specific Init
  00000004.505:[WiFi] Wlan Bus Up
  00000004.507:mhd_thread_init, 136

  00000004.511:host_rtos_init_semaphore, 281, 0x324a94a0
  00000004.515:host_rtos_init_semaphore, 281, 0x324a94c0
  00000004.520:host_rtos_init_semaphore, 281, 0x324a94e0
  00000004.525:host_rtos_init_semaphore, 281, 0x324a9500
  00000004.530:host_rtos_init_semaphore, 281, 0x324a9520
  00000004.535:host_rtos_init_binary_semaphore, 266, 0x324a9540
  00000004.642:host_rtos_get_mhdtask_settings, 395
  00000004.646:[WiFi] mhdTask priority[3] stack[0x322d2620] stack_size[3072]
  00000004.653:host_rtos_create_thread, 334, 0x324a9560, 0x322d2620, 3072
  00000004.680:[MKD IOCTL] max_time=20
  00000004.837:event from idx 0, type 47, status 0, reason 0, flags 24, 0
  00000004.862:[MKD IOCTL] max_time=29
  00000004.901:[WiFi] Turn on AMPDU TX
  00000004.962:[MKD IOCTL] max_time=41
  00000005.020:[WiFi] MHD: Version V9.0.13 (889d120a) C0:F5:35:DC:DC:5B
  00000005.043:[WiFi] wl0: Mar 31 2026 15:07:31 version 20.27.162 (1-g0d5a09a+) FWID 01-5f95b8b3

  00000005.051:[WiFi] Disable roaming mode
  00000005.072:load wifi driver success
  00000005.075:wifi_up, 381
  00000005.078:syna_mgmt_send err state 0
  00000005.082:syna_mgmt_send err state 0
  00000005.085:syna_mgmt_send err state 0
  00000005.089:syna_mgmt_send err state 0
  00000006.093:syna_mgmt_send err state 0
  00000007.099:syna_mgmt_send err state 0
  uart:~$ net iface 2
  Hostname: zephyr
  Default interface: 2


  Interface wlan1 (0x32284f18) (WiFi) [2]
  ===============================
  Link addr : C0:F5:35:DC:DC:5B
  MTU       : 1500
  Flags     : NO_AUTO_START,IPv4,IPv6
  Device    : wifi_sta (0x1206b4a8)
  Status    : oper=UP, admin=UP, carrier=ON
  Ethernet capabilities supported:
  Ethernet PHY device: <none> (0)
  IPv6 unicast addresses (max 2):
        fe80::c2f5:35ff:fedc:dc5b autoconf preferred infinite
  IPv6 multicast addresses (max 3):
        ff02::1
        ff02::1:ffdc:dc5b
  IPv6 prefixes (max 2):
        <none>
  IPv6 hop limit           : 64
  IPv6 base reachable time : 30000
  IPv6 reachable time      : 19592
  IPv6 retransmit timer    : 0
  IPv4 unicast addresses (max 2):
        <none>
  IPv4 multicast addresses (max 2):
        224.0.0.1
  IPv4 gateway : 0.0.0.0
  DHCPv4 state      : disabled
  uart:~$ wifi scan -i 2
  00000015.363:syna_mgmt_scan
  00000015.365:host_rtos_init_semaphore, 281, 0x324a9638
  00000015.370:host_rtos_init_semaphore, 281, 0x324a9658
  00000017.563:chanspec 00001001, 00400004
  00000017.583:chanspec 00001001, 00400004
  00000017.614:chanspec 00001006, 00000000
  00000017.622:chanspec 00001006, 00000000
  00000017.630:chanspec 00001006, 02400006
  00000017.637:chanspec 00001006, 02400006
  00000017.644:chanspec 00001006, 00000000
  00000017.652:chanspec 00001006, 00000000
  00000017.660:chanspec 0000100b, 00400008
  00000017.667:chanspec 0000100b, 00400004
  00000017.674:chanspec 0000100b, 00400008
  00000017.681:chanspec 0000100b, 00400004
  00000017.688:chanspec 0000100b, 00400008
  00000017.743:chanspec 00001002, 00400004
  00000018.115:chanspec 0000100a, 00400004
  00000018.122:chanspec 0000100a, 00400004
  00000018.129:chanspec 0000100a, 00400004
  00000018.136:chanspec 0000100a, 00400004
  00000018.156:chanspec 0000e02a, 00400004
  00000018.176:chanspec 0000e02a, 00400004
  00000018.207:chanspec 0000d926, 02400006
  00000018.214:chanspec 0000d926, 02400006
  00000018.221:chanspec 0000d926, 00000000
  00000018.229:chanspec 0000d926, 02400006
  00000018.236:chanspec 0000d926, 00000000
  00000018.244:chanspec 0000d926, 00000000
  00000018.252:chanspec 0000d926, 02400006
  00000018.259:chanspec 0000d926, 02400006
  00000018.266:chanspec 0000d926, 02400006
  00000018.273:chanspec 0000d926, 00000000
  00000018.281:chanspec 0000d926, 00000000
  00000018.289:chanspec 0000e22a, 00000000
  00000018.297:chanspec 0000d82e, 02400006
  00000018.304:chanspec 0000e22a, 00000000
  00000018.312:chanspec 0000d82e, 00000000
  00000018.320:chanspec 0000d82e, 00000000
  00000018.328:chanspec 0000e22a, 00000000
  00000018.336:chanspec 0000e22a, 00000000
  00000018.344:chanspec 0000d82e, 02400006
  00000018.351:chanspec 0000d82e, 00000000
  00000018.359:chanspec 0000d82e, 00000000
  00000018.367:chanspec 0000d92e, 02400006
  00000018.375:chanspec 0000d92e, 00000000
  00000018.383:chanspec 0000d92e, 00000000
  00000018.391:chanspec 0000d92e, 02400006
  00000018.399:chanspec 0000d92e, 00000000
  00000018.407:chanspec 0000d92e, 00000000
  00000018.415:chanspec 0000d92e, 02400006
  00000018.423:chanspec 0000d92e, 00000000
  00000018.431:chanspec 0000d92e, 00000000
  00000018.439:chanspec 0000e09b, 00800004
  00000018.447:chanspec 0000d897, 02400006
  00000018.455:chanspec 0000d897, 00000000
  00000018.463:chanspec 0000d897, 00000000
  00000018.471:chanspec 0000e09b, 00800004
  00000018.479:chanspec 0000d897, 02400006
  00000018.487:chanspec 0000d897, 00000000
  00000018.495:chanspec 0000d897, 00000000
  00000018.503:chanspec 0000e09b, 00800004
  00000018.510:chanspec 0000d897, 02400006
  00000018.517:chanspec 0000d897, 00000000
  00000018.525:chanspec 0000d897, 00000000
  00000018.533:chanspec 0000e09b, 00400004
  00000018.540:chanspec 0000d99f, 02400006
  00000018.573:chanspec 0000e39b, 00400004
  00000020.320:syna_mgmt_scan, 97: 30

  Num  | SSID                             (len) | Chan (Band)   | RSSI | Security             | BSSID             | MFP
  1    | testAP_linksys                   14    | 1    (2.4GHz) | -48  | WPA2-PSK             | C8:B3:73:1E:38:22 | Disable
  2    | HP-Print-AB-LaserJet Pro MFP     28    | 6    (2.4GHz) | -85  | OPEN                 | F8:DA:0C:09:7E:AB | Disable
  3    | EdgeConnect-Guest                17    | 6    (2.4GHz) | -79  | OPEN                 | E4:D1:24:DF:B3:41 | Disable
  4    | Synaptics                        9     | 6    (2.4GHz) | -74  | WPA2 Enterprise      | E4:D1:24:ED:AC:20 | Disable
  5    | Synaptics                        9     | 6    (2.4GHz) | -57  | WPA2 Enterprise      | E4:D1:24:ED:AD:A0 | Disable
  6    | EdgeConnect-Guest                17    | 6    (2.4GHz) | -57  | OPEN                 | E4:D1:24:ED:AD:A1 | Disable
  7    | SVT                              3     | 11   (2.4GHz) | -19  | WPA2-PSK-SHA256      | 7C:10:C9:03:57:08 | Disable
  8    | vivo Y39 5G                      11    | 11   (2.4GHz) | -89  | WPA2-PSK             | 5A:E9:35:5F:5C:A6 | Disable
  9    | [LG_Refrigerator]d241            21    | 11   (2.4GHz) | -91  | WPA2-PSK             | 8E:83:94:02:D2:41 | Disable
  10   | Joe_Zus                          7     | 2    (2.4GHz) | -83  | WPA2-PSK             | 2C:4D:54:49:BD:D0 | Disable
  11   | NETGEAR82                        9     | 10   (2.4GHz) | -88  | WPA2-PSK             | 8C:3B:AD:D7:C8:0E | Disable
  12   | SVT_5G                           6     | 36   (5GHz  ) | -24  | WPA2-PSK             | 7C:10:C9:03:57:0C | Disable
  13   | Synaptics                        9     | 40   (5GHz  ) | -88  | WPA2 Enterprise      | E4:D1:24:ED:A1:F0 | Disable
  14   | Synaptics                        9     | 40   (5GHz  ) | -87  | WPA2 Enterprise      | E4:D1:24:ED:C8:70 | Disable
  15   | EdgeConnect                      11    | 40   (5GHz  ) | -87  | OPEN                 | E4:D1:24:ED:C8:71 | Disable
  16   | EdgeConnect-Guest                17    | 40   (5GHz  ) | -86  | OPEN                 | E4:D1:24:ED:C8:72 | Disable
  17   | DANDELI                          7     | 44   (5GHz  ) | -73  | OPEN                 | 5C:F3:70:B9:E9:AC | Disable
  18   | Synaptics                        9     | 44   (5GHz  ) | -60  | WPA2 Enterprise      | E4:D1:24:ED:B6:F0 | Disable
  19   | KUNDREMUKH                       10    | 44   (5GHz  ) | -72  | OPEN                 | 5C:F3:70:B9:E9:5A | Disable
  20   | EdgeConnect                      11    | 44   (5GHz  ) | -60  | OPEN                 | E4:D1:24:ED:B6:F1 | Disable
  21   | EdgeConnect-Guest                17    | 44   (5GHz  ) | -60  | OPEN                 | E4:D1:24:ED:B6:F2 | Disable
  22   | Synaptics                        9     | 48   (5GHz  ) | -67  | WPA2 Enterprise      | E4:D1:24:ED:AD:B0 | Disable
  23   | EdgeConnect                      11    | 48   (5GHz  ) | -67  | OPEN                 | E4:D1:24:ED:AD:B1 | Disable
  24   | EdgeConnect-Guest                17    | 48   (5GHz  ) | -67  | OPEN                 | E4:D1:24:ED:AD:B2 | Disable
  25   | cybersyna_5ghz                   14    | 149  (5GHz  ) | -88  | WPA3-SAE-HNP         | 8C:90:2D:C8:74:83 | Disable
  26   | Synaptics                        9     | 149  (5GHz  ) | -88  | WPA2 Enterprise      | E4:D1:24:ED:AC:30 | Disable
  Scan requested
  27   | EdgeConnect                      11    | 149  (5GHz  ) | -89  | OPEN                 | E4:D1:24:ED:AC:32 | Disable
  28   | EdgeConnect-Guest                17    | 149  (5GHz  ) | -88  | OPEN                 | E4:D1:24:ED:AC:33 | Disable
  29   |                                  0     | 149  (5GHz  ) | -89  | WPA2-PSK             | BE:90:2D:C8:74:83 | Disable
  30   | Synaptics                        9     | 161  (5GHz  ) | -91  | WPA2 Enterprise      | E4:D1:24:ED:B7:30 | Disable
  Scan request done
  uart:~$ wifi connect -i 2 -s "SVT_5G" -p "1234567890" -k 1
  00000031.746:syna_mgmt_connect
  00000031.748:[WiFi] Looking for AP -
  00000031.752:host_rtos_init_semaphore, 281, 0x324a9678
  00000034.461:chanspec 0000e02a, 00400004
  00000034.481:chanspec 0000e02a, 00400004
  00000034.501:chanspec 0000e02a, 00400004
  00000041.627:host_rtos_deinit_semaphore, 314
  00000041.631:[WiFi] AP Found -
  00000041.634:  SSID     : SVT_5G

  00000041.637:  BSSID    : 7C:10:C9:03:57:0C
  00000041.640:  Channel  : 36

  00000041.643:  band     : 5

  00000041.646:  Security : WPA2 PSK AES

  00000041.649:[WiFi] Joining AP
  00000041.652:host_rtos_init_semaphore, 281, 0x324a9678
  00000041.845:mhd_wifi_join_specific, 1149, 5, 0000d024
  00000041.867:[WiFi] Try IOCTL to join for 4384
  00000041.888:mhd_wifi_join_specific, 1177, 0 0
  00000044.028:Error - received a channel 1 packet which was not BRCM ethertype 0xFFFF
  00000044.547:Error - received a channel 1 packet which was not BRCM ethertype 0xC0F5
  00000044.571:Error - received a channel 1 packet which was not BRCM ethertype 0xC0F5
  00000052.020:Error - received a channel 1 packet which was not BRCM ethertype 0x7C10
  00000052.031:Error - received a channel 1 packet which was not BRCM ethertype 0xC0F5
  00000052.042:event from idx 0, type 3, status 0, reason 0, flags 0, 0
  00000052.048:-- JOIN Event (type 3 status 0 reason 0)
  00000052.057:Error - received a channel 1 packet which was not BRCM ethertype 0x7C10
  00000052.068:Error - received a channel 1 packet which was not BRCM ethertype 0xC0F5
  00000052.079:event from idx 0, type 16, status 0, reason 0, flags 1, 0
  00000052.085:-- JOIN Event (type 16 status 0 reason 0)
  00000052.094:event from idx 0, type 46, status 6, reason 0, flags 0, 0
  00000052.100:-- JOIN Event (type 46 status 262 reason 512)
  00000052.111:event from idx 0, type 0, status 0, reason 0, flags 0, 0
  00000052.117:-- JOIN Event (type 0 status 0 reason 0)
  00000052.122:host_rtos_deinit_semaphore, 314
  00000052.126:[WiFi] Join successfully ! sta_is_link_up=1
  Connection requested
  Connected
  uart:~$ net dhcpv4 client start 2
  uart:~$ net iface 2
  Hostname: zephyr
  Default interface: 2


  Interface wlan1 (0x32284f18) (WiFi) [2]
  ===============================
  Link addr : C0:F5:35:DC:DC:5B
  MTU       : 1500
  Flags     : NO_AUTO_START,IPv4,IPv6
  Device    : wifi_sta (0x1206b4a8)
  Status    : oper=UP, admin=UP, carrier=ON
  Ethernet capabilities supported:
  Ethernet PHY device: <none> (0)
  IPv6 unicast addresses (max 2):
        fe80::c2f5:35ff:fedc:dc5b autoconf preferred infinite
  IPv6 multicast addresses (max 3):
        ff02::1
        ff02::1:ffdc:dc5b
  IPv6 prefixes (max 2):
        <none>
  IPv6 hop limit           : 64
  IPv6 base reachable time : 30000
  IPv6 reachable time      : 19592
  IPv6 retransmit timer    : 0
  IPv4 unicast addresses (max 2):
        192.168.50.84/255.255.255.0 DHCP preferred
  IPv4 multicast addresses (max 2):
        224.0.0.1
  IPv4 gateway : 192.168.50.1
  DHCPv4 lease time : 86400
  DHCPv4 renew time : 43200
  DHCPv4 server     : 192.168.50.1
  DHCPv4 requested  : 192.168.50.84
  DHCPv4 state      : bound
  DHCPv4 attempts   : 1
  DHCPv4 state      : bound

Notes
*****

* If authentication fails with code 1006, verify AP security mode and
  key-management selection first.
* This Wi-Fi app supports 4381, 4612 and 43711. By default 4281 is selected in 
  samples/wifi/synaptics/prj.conf
