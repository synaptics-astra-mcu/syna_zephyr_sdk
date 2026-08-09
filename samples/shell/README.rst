.. zephyr:code-sample:: shell
   :name: sl261x shell

   Run test commands for I2C, SPI & GPIO on the SL261x-M52

Overview
********

The shell can be built for both the SR110 and the SL261x-M52. On the SL261x, commands for
testing I2C, SPI & GPIO are supported. The XSPI flash driver is currently not working.


Wiring
******

Create a loop between SDO and SDI of the SM-SPI master controller by connecting pins 19 & 21 on the
40-pin header of the I/O board.

SM-GPIO 26 can be monitored on pin 13 of the 40-pin header of the I/O board.


Building and Running
********************

Build the Zephyr firmware:

.. zephyr-app-commands::
   :zephyr-app: samples/shell
   :board: sl2619_rdk/sl2619/m52
   :goals: build

The firmware can be loaded through USB (``build/image/usb_boot/m52bl.bin``), installed on the SPI
(``build/image/spi/spi_boot.bin``) or eMMC (``build/image/eMMCimg/preboot.subimg``).


Sample Output
*************

The SPI loopback can be tested with the following command:

.. code-block:: console

   syna,m52:~$ spi transceive spim 0x00 0x01 0x02 0x03
   TX:
   00000000: 00 01 02 03                                      |....             |
   RX:
   00000000: 00 01 02 03                                      |....             |

Devices on the two I2C controllers can be detected with the following commands:

.. code-block:: console

   syna,m52:~$ i2c scan i2c@58035000
        0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
   00:             -- -- -- -- -- -- -- -- -- -- -- --
   10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   40: 40 41 42 43 44 45 -- -- -- -- -- -- -- -- -- --
   50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   70: -- -- -- -- -- -- -- --
   6 devices found on i2c@58035000

   syna,m52:~$ i2c scan i2c@58036000
        0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
   00:             -- -- -- -- -- -- -- -- -- -- -- --
   10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   60: 60 -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
   70: -- -- -- -- -- -- -- --
   1 devices found on i2c@58036000

The SM-GPIO 26 is currently not used for any other functionality. It can be monitored on the
40-pin header using a logic or scope. Its value can be toggled with the following commands:

.. code-block:: console

   syna,m52:~$ gpio conf gpio2 2 o0
   syna,m52:~$ gpio conf gpio2 2 o1

The device id can be read with the ``hwinfo`` command:

.. code-block:: console

   syna,m52:~$ hwinfo devid
   ID: 0x27006102a000000006000000

