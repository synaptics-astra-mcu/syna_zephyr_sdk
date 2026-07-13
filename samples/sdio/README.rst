.. zephyr:code-sample:: sdio
   :name: SDIO driver tests

   Test the SDIO interface (Wifi card) with the SDHC driver.

Overview
********

A Bluetooth/Wifi module can be connected to the SR110-RDK board via the SDIO
interface.  Shell commands are available to detect & initialize the card and
run further tests.


Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/sdio
   :board: sr100_rdk/sr100/m55
   :goals: build


Sample Output
*************

The corresponding test can be accessed via the custom ``sdio`` shell command.

.. code-block:: console

   *** Booting Zephyr OS build v4.4.1 ***
   Initializing SDIO application...
   syna:~$ sdio
   sdio - SDIO commands
   Subcommands:
     init  : Initialize SDIO interface
     test  : SDIO Sequence test
   syna:~$ sdio init
   Initializing SDIO interface...
     Initializing mutex...
     Checking card presence...
     Initializing SDIO card...
     10us Reading CCCR register...
   [rs.log] fn0: num=0 bs=32 card=0x33ea1264 manf_id=0x06CB manf_code=0x4612 func_id=0x0C max_blk_size=32 max_speed=0x2B rdy_timeout=0

   [rs.log] fn1: num=1 bs=64 card=0x33ea1264 manf_id=0x06CB manf_code=0x4612 func_id=0x0C max_blk_size=64 max_speed=0x00 rdy_timeout=0

   [rs.log] fn2: num=2 bs=256 card=0x33ea1264 manf_id=0x06CB manf_code=0x4612 func_id=0x0C max_blk_size=512 max_speed=0x00 rdy_timeout=200

   Card voltage: 1.8V

   Card timing: SDR25

   Card type: SDIO

   Card spec: 1.1

   SDIO initialization successful
   [00:00:33.269,000] <err> sdhc_mshc: error interrupt 0x1
   [00:00:33.270,000] <err> sdhc_mshc: error interrupt 0x1
   [00:00:33.270,000] <inf> sd: Card does not support CMD8, assuming legacy card
   syna:~$ sdio test
   SDIO Seq Check

   SDIO 0x10 write successful 64

   SDIO 0x110 write successful 64

   SDIO 0x110 read successful 64

   SDIO 0x1000a write successful 0

   SDIO 0x1000b write successful 27

   SDIO 0x1000c write successful 0

   SDIO 0x0 write successful bytes=64

   SDIO 0x0 write successful bytes=64

   SDIO 0x0 write successful bytes=64

   SDIO 0x0 write successful bytes=64

   SDIO 0x0 write successful bytes=64

   SDIO 0x0 write successful bytes=64

   SDIO 0x0 write successful bytes=64

   SDIO 0x0 write successful bytes=64

   SDIO 0x0 write successful bytes=64

   SDIO 0x0 write successful bytes=64

   SDIO 0x0 read successful bytes=64

   buffer content 0x0 0x1 0x2 0x3

   SDIO 0x0 write successful bytes=128

   SDIO 0x0 read successful bytes=128

   buffer content 0x0 0x1 0x2 0x3

   SDIO 0x1000b write successful 16

   SDIO 0x1000c write successful 24

   SDIO 0x10008 write successful 96

   SDIO 0x10009 write successful 16

   SDIO 0x1001d write successful 208

   SDIO 0x1000e write successful 0

   SDIO 0x1000e write successful 16

   SDIO 0x1000e read successful 208

   SDIO 0x1000e write successful 210

   SDIO 0x1000e read successful 210

   SDIO Seq Check End
