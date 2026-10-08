.. zephyr:code-sample:: flash
   :name: xSPI flash driver

   xSPI flash driver integration & tests

Overview
********

The xSPI flash driver can be tested with filesystem operations (littlefs) and
the flash tests commands of the flash shell.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: <path-to-zephyr_srsdk>/samples/shell
   :board: sr100_rdk/sr100/m55
   :goals: build

The shell provides flash test commands. You can run, e.g.,

.. code-block:: console

   flash test 0x40000 0x1000 10

Furthermore, flash commands for reading, writing and erase are supported.
At offset 0x800000, a littlefs partition is mounted:

.. code-block:: console

   [00:00:00.136,000] <inf> littlefs: LittleFS version 2.11, disk version 2.1
   [00:00:00.137,000] <inf> littlefs: FS at flash-controller@5031b000:0x800000 is 2048 0x1000-byte blocks with 512 cycle
   [00:00:00.137,000] <inf> littlefs: partition sizes: rd 256 ; pr 256 ; ca 256 ; la 256
   [00:00:00.137,000] <inf> lfs_mount: LittleFS mounted on /lfs

There is an additional shell test command ``lfs_test`` for littlefs; you can repeat it several
times.

Another test is to corrupt the littlefs partition by running

.. code-block:: console

   flash erase xspi 0x800000 1
   flash erase xspi 0x801000 1

When the device is started again, the littlefs partition should be created automatically:

.. code-block:: console

   [00:00:00.137,000] <err> littlefs: /home/aweissel/zephyrproject/modules/fs/littlefs/lfs.c:1386: Corrupted dir pair at {0x0, 0x1}
   [00:00:00.137,000] <wrn> littlefs: can't mount (LFS -84); formatting
   [00:00:00.158,000] <inf> littlefs: /lfs mounted
   [00:00:00.158,000] <inf> lfs_mount: LittleFS mounted on /lfs


Sample Output
*************

.. code-block:: console

   syna:~$ flash test 0x40000 0x1000 4
   Erase OK.
   Write OK.
   Verified OK.
   Erase OK.
   Write OK.
   Verified OK.
   Erase OK.
   Write OK.
   Verified OK.
   Erase OK.
   Write OK.
   Verified OK.
   Erase-Write-Verify test done.

   syna:~$ lfs_test
   LFS test: 1 KB write (verify each 512 B chunk) + full readback
   Write+verify: 1 KB in 40 ms (25 KB/s)
   Write CRC32: 0xD4B87511
   Read:  1 KB in 0 ms (0 KB/s)
   Read  CRC32: 0xD4B87511
   PASS: CRC32 match
