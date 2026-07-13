.. zephyr:code-sample:: ethernet
   :name: Ethernet
   :relevant-api: dhcpv4 net_mgmt net_config

   Start a DHCPv4 client to obtain an IPv4 address from a DHCPv4 server.
   Provide other networking commands like ping.
   Use the zperf shell utility to evaluate network bandwidth.

Overview
********

The ``dhcpv4_client`` sample demonstrates how to use the Ethernet stack of
Zephyr.

See ``zephyr/samples/net/dhcpv4_client/README.rst``.

The ``zperf`` sample can be run to evaluate the network bandwidth; see
``zephyr/samples/net/zperf/README.rst``.

The DMA of the Ethernet-MAC cannot access the ITCM/DTCM of the M52. It can
access the NPU memory of the SL261x and an external DDR memory. The DDR memory
needs to be initialized first (enabled through ``CONFIG_DDR``).

The Ethernet-MAC driver for the Synopsys DesignWare-IP in Zephyr
(``zephyr/drivers/ethernet/eth_dwmac.c``) is modified through a patch to allow
the modification of the physical address of the network buffers. This is
required as the addresses the NPU and DDR memories are mapped to different
address ranges on the M52.

Furthermore, the patch backports a Zephyr upstream commit to support
zero-copy in the transmit path of the ``dwmac`` Ethernet driver.


Requirements
************

Connect the RJ45 Ethernet port of the SL261x-RDK to a network switch.


Building and Running
********************

Build the firmware image for the SL261x-M52. If you omit ``-DCONFIG_DDR=y``,
the NPU memory will be used for the Ethernet-DMA (descriptors & packets).
For the DHCPv4 sample, run:

.. zephyr-app-commands::
   :zephyr-app: samples/net/dhcpv4_client
   :board: sl2619_rdk/sl2619/m52
   :goals: build
   :west-args: -DCONFIG_DDR=y

You can install the resulting image ``build/image/usb_boot/m52bl.bin`` via USB.

For zperf, run:

.. zephyr-app-commands::
   :zephyr-app: samples/net/zperf
   :board: sl2619_rdk/sl2619/m52
   :goals: build
   :west-args: -DCONFIG_DDR=y -DCONFIG_NET_IPV6=n -DCONFIG_NET_CONFIG_MY_IPV4_ADDR=\"172.27.160.107\"

The arguments above disable IPv6 support. Otherwise the resulting firmware
image will not fit in the ITCM/DTCM of the SL261x-M52.
The zperf sample does not enable DHCv4 per default but uses a fixed IPv4
address.  You might need to adapt this address specified in the above example.

The zperf throughput can be optimized by increasing the number & size of
network buffers & packets. DDR memory provides higher throughput compared to
NPU memory. With the following options, a UDP throughput of 178 MBit/s (zperf
client) & 154 (zperf server) and a TCP throughput of 78 (zperf client) &
107 (zperf server) can be achieved:

.. code-block:: console

   CONFIG_NET_PKT_RX_COUNT=200
   CONFIG_NET_PKT_TX_COUNT=200
   CONFIG_NET_BUF_RX_COUNT=200
   CONFIG_NET_BUF_TX_COUNT=200
   CONFIG_NET_BUF_DATA_SIZE=1536
   CONFIG_NET_ZPERF_MAX_PACKET_SIZE=1470
   CONFIG_NET_TC_THREAD_PREEMPTIVE=y

Sample Output of DHCPv4
***********************

The DHCPv4 client will start automatically. You can run other ``net`` commands
like ``ping`` or ``dns`` on the console:

.. code-block:: console

   uart:~$
   [00:00:00.222,000] <inf> phy_mii: PHY (2) ID 1CC916
   [00:00:00.230,000] <inf> dwmac_core: HW version 5.40
   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   [00:00:00.272,000] <inf> net_dhcpv4_client_sample: Run dhcpv4 client
   [00:00:00.272,000] <inf> net_dhcpv4_client_sample: Start on ethernet: index=1
   [00:00:03.170,000] <inf> phy_mii: PHY (2) Link speed 1000 Mb, full duplex
   [00:00:06.274,000] <inf> net_dhcpv4: Received: 172.27.160.43
   [00:00:06.274,000] <inf> net_dhcpv4_client_sample:    Address[1]: 172.27.160.43
   [00:00:06.274,000] <inf> net_dhcpv4_client_sample:     Subnet[1]: 255.255.255.0
   [00:00:06.274,000] <inf> net_dhcpv4_client_sample:     Router[1]: 172.27.160.254
   [00:00:06.274,000] <inf> net_dhcpv4_client_sample: Lease time[1]: 21574 seconds

   uart:~$ net ping 172.27.160.101
   PING 172.27.160.101
   28 bytes from 172.27.160.101 to 172.27.160.101: icmp_seq=1 ttl=64 time=0 ms
   28 bytes from 172.27.160.101 to 172.27.160.101: icmp_seq=2 ttl=64 time=0 ms

   uart:~$ net dns www.google.com
   Query for 'www.google.com' sent.
   dns: 142.251.155.119
   dns: 142.251.150.119
   dns: 142.251.156.119
   dns: 142.251.153.119
   dns: 142.251.154.119
   dns: 142.251.152.119
   dns: 142.251.157.119
   dns: 142.251.151.119
   dns: All results received
   uart:~$

Sample Output of zperf
**********************

Zperf commands are invoked from the shell. You can start zperf on the SL261x
either in client or server mode. Zperf support UDP & TCP. The examples below
show the results for UDP and DDR memory. For NPU, the throughput is
significantly lower (~41 Mbps for UDP & server mode).

**Server Mode**

SL261x-RDK/Zephyr console:

.. code-block:: console

   uart:~$ zperf udp download 5001
   UDP server started on port 5001
   [00:00:08.664,000] <inf> net_zperf: Binding to 172.27.160.107
   [00:00:08.664,000] <inf> net_zperf: Listening on port 5001
   New session started.
   End of session!
    duration:              10.01 s
    received packets:      109222
    nb packets lost:       1026490
    nb packets outorder:   0
    jitter:                        119 us
    rate:                  89.37 Mbps

Linux host:

.. code-block:: console

   iperf -l 1K -u -c 172.27.160.107 -b 100mps
   ------------------------------------------------------------
   Client connecting to 172.27.160.107, UDP port 5001
   Sending 1024 byte datagrams, IPG target: 0.01 us (kalman adjust)
   UDP buffer size:  208 KByte (default)
   ------------------------------------------------------------
   [  1] local 172.27.160.106 port 53574 connected with 172.27.160.107 port 5001
   [ ID] Interval       Transfer     Bandwidth
   [  1] 0.0000-10.0000 sec  1.08 GBytes   930 Mbits/sec
   [  1] Sent 1135718 datagrams
   [  1] Server Report:
   [ ID] Interval       Transfer     Bandwidth        Jitter   Lost/Total Datagrams
   [  1] 0.0000-10.0116 sec   107 MBytes  89.4 Mbits/sec   0.119 ms 1026490/109222 (9.4e+02%)


**Client Mode**

SL261x-RDK/Zephyr console:

.. code-block:: console

   uart:~$ zperf udp upload 172.27.160.106 5001 10 1K 100M
   Remote port is 5001
   Connecting to 172.27.160.106
   Duration:       10.00 s
   Packet size:    1000 bytes
   Rate:           100.00 Mbps
   Starting...
   Rate:           100.00 Mbps
   Packet duration 78 us
   -
   Upload completed!
   Statistics:             server  (client)
   Duration:               9.99 s  (10.00 s)
   Num packets:            81329   (81329)
   Num packets out order:  0
   Num packets lost:       0
   Jitter:                 35 us
   Rate:                   65.06 Mbps      (65.06 Mbps)

Linux host:

.. code-block:: console

   iperf -s -l 1K -u
   ------------------------------------------------------------
   Server listening on UDP port 5001
   UDP buffer size:  208 KByte (default)
   ------------------------------------------------------------
   [  1] local 172.27.160.106 port 5001 connected with 172.27.160.107 port 35494
   [ ID] Interval       Transfer     Bandwidth        Jitter   Lost/Total Datagrams
   [  1] 0.0000-9.9989 sec  77.6 MBytes  65.1 Mbits/sec   0.035 ms 0/81335 (0%)
