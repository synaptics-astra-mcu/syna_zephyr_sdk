.. zephyr:code-sample:: dma
   :name: DMA sample application

   Test DMA memory-to-memory and DMA device transfers (UART & SPI)

Overview
********

The DMA test application will run DMA memory-to-memory transfers, including 2D operations like
rotation and mirroring, and peripheral transfers to/from UART & SPI.

Various synchronous & asynchronous transfers between the SPI-master and the SPI-slave controller
(master TX and slave RX, master RX and slave TX, multi-transfer tests) are tested, as well as
different transfer sizes between 1 and 32 bytes.
The ``spi_dw`` driver does not yet support DMA for scatter-gather operations.

The file ``include/zephyr/drivers/dma/dma_arm350.h`` lists the defines for the various rotation
and flip operations. They can be set in the ``dma_slot`` field for memory-to-memory transfers.
Please note that the current version of the driver assumes the data to be organized in a square in
memory, i.e., the ``block_size`` parameter defines the size of both a row and a column.


Wiring
******

UART0 is used on J24. So for the console output and UART-DMA testing, connect pins 13 + 14 on J24
(+ GND).

For testing SPI with DMA, connect the SPI master pins to the corresponding SPI slave pins on J25
on the RDK board (SPI loopback between master & slave):

.. code-block:: console

   Pin 11 (SPI_MSTR_CLK)  ↔︎ Pin  7 (SPI_SLV_CLK_B)
   Pin 12 (SPI_MSTR_CS)   ↔︎ Pin  8 (SPI_SLV_CS_B)
   Pin 13 (SPI_MSTR_MISO) ↔︎ Pin  9 (SPI_SLV_MISO_B)
   Pin 14 (SPI_MSTR_MOSI) ↔︎ Pin 10 (SPI_SLV_MOSI_B)


Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: <path-to-zephyr_srsdk>/samples/dma
   :board: sr100_rdk/sr100/m55
   :goals: build

The console log will print the SPI-DMA test results, followed by various 2D memory-to-memory
operations (rotation & flips). The final prints will be for UART-TX with DMA, followed by UART-RX
with DMA. Every 8th character typed in the console will trigger a print of the received string
(e.g., keys 1..8 in the output below).


Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   Hello World! sr100_rdk/sr100/m55
   
   ========================================
        SPI TEST SUITE BEGINNING
   ========================================
   
   [MASTER SYNC TESTS]
   ? PASS: Master TX only (sync)
   ? PASS: Master RX only (sync)
   ? PASS: Master TX + RX (sync)
   
   [MASTER ASYNC TESTS]
   ? PASS: Master TX (async)
   ? PASS: Master RX (async)
   ? PASS: Master TX + RX (async)
   
   [SLAVE SYNC TESTS]
   ? PASS: Slave TX only (sync)
   ? PASS: Slave RX only (sync)
   ? PASS: Slave TX + RX (sync)
   
   [SLAVE ASYNC TESTS]
   ? PASS: Slave TX (async)
   ? PASS: Slave RX (async)
   ? PASS: Slave TX + RX (async)
   
   [MULTI-TRANSFER TESTS]
   
   --- Sequential Master Transfers ---
     Transfer 1: OK
     Transfer 2: OK
     Transfer 3: OK
   Sequential test: 3 passed, 0 failed
   
   --- Interleaved Async Transfers ---
     Transfer 1 queued: OK
     Transfer 2 queued: OK
     Both transfers completed
   
   --- Various Transfer Sizes ---
     Size 1 bytes: OK
     Size 2 bytes: OK
     Size 4 bytes: OK
     Size 8 bytes: OK
     Size 16 bytes: OK
     Size 32 bytes: OK
   Size test: 6/6 passed
   
   ========================================
        TEST RESULTS SUMMARY
   ========================================
   Total Tests: 15
   Passed: 15
   Failed: 0
   Success Rate: 100%
   ========================================
   
   
   ? All SPI tests passed!
   DMA successful
   DMA: 0 (48 40 32 24)
   56 48 40 32 24 16  8  0
   57 49 41 33 25 17  9  1
   58 50 42 34 26 18 10  2
   59 51 43 35 27 19 11  3
   60 52 44 36 28 20 12  4
   61 53 45 37 29 21 13  5
   62 54 46 38 30 22 14  6
   63 55 47 39 31 23 15  7
   DMA successful
   DMA: 0 (62 61 60 59)
   63 62 61 60 59 58 57 56
   55 54 53 52 51 50 49 48
   47 46 45 44 43 42 41 40
   39 38 37 36 35 34 33 32
   31 30 29 28 27 26 25 24
   23 22 21 20 19 18 17 16
   15 14 13 12 11 10  9  8
    7  6  5  4  3  2  1  0
   DMA successful
   DMA: 0 (15 23 31 39)
    7 15 23 31 39 47 55 63
    6 14 22 30 38 46 54 62
    5 13 21 29 37 45 53 61
    4 12 20 28 36 44 52 60
    3 11 19 27 35 43 51 59
   2 10 18 26 34 42 50 58
    1  9 17 25 33 41 49 57
    0  8 16 24 32 40 48 56
   DMA successful
   DMA: 0 (6 5 4 3)
    7  6  5  4  3  2  1  0
   15 14 13 12 11 10  9  8
   23 22 21 20 19 18 17 16
   31 30 29 28 27 26 25 24
   39 38 37 36 35 34 33 32
   47 46 45 44 43 42 41 40
   55 54 53 52 51 50 49 48
   63 62 61 60 59 58 57 56
   DMA successful
   DMA: 0 (57 58 59 60)
   56 57 58 59 60 61 62 63
   48 49 50 51 52 53 54 55
   40 41 42 43 44 45 46 47
   32 33 34 35 36 37 38 39
   24 25 26 27 28 29 30 31
   16 17 18 19 20 21 22 23
    8  9 10 11 12 13 14 15
    0  1  2  3  4  5  6  7
   DMA successful
   DMA: 0 (8 16 24 32)
    0  8 16 24 32 40 48 56
    1  9 17 25 33 41 49 57
    2 10 18 26 34 42 50 58
    3 11 19 27 35 43 51 59
    4 12 20 28 36 44 52 60
    5 13 21 29 37 45 53 61
    6 14 22 30 38 46 54 62
    7 15 23 31 39 47 55 63
   DMA successful
   DMA: 0 (55 47 39 31)
   63 55 47 39 31 23 15  7
   62 54 46 38 30 22 14  6
   61 53 45 37 29 21 13  5
   60 52 44 36 28 20 12  4
   59 51 43 35 27 19 11  3
   58 50 42 34 26 18 10  2
   57 49 41 33 25 17  9  1
   56 48 40 32 24 16  8  0
   Hello World!
               uart_tx 0
   event 3
   RX buf request
   event 2
   received (off 0, len 8): 12345678
   event 4
   RX buf released
   event 3
   RX buf request
