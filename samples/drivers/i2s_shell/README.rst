.. SPDX-License-Identifier: Apache-2.0

.. zephyr:code-sample:: i2s-shell
   :name: I2S shell
   :relevant-api: i2s_interface

   Use the I2S API to run shell-driven playback and capture tests.

Description
***********

This sample provides an interactive shell for validating the SR100 I2S driver
with Zephyr's I2S API. It supports sine playback, file-backed capture and
playback using sample storage, and per-command I2S format selection.

The capture command prints profiling and storage dump information when it
completes. This lets users dump captured data from flash without running a
separate locate command.

Requirements
************

Hardware:

* Synaptics SR100 RDK (``sr100_rdk/sr100/m55``)
* Microphone, speaker, or external I2S loopback wiring depending on the test

Devicetree:

* An enabled I2S RX controller selected by ``syna,i2s-rx``
* An enabled I2S TX controller selected by ``syna,i2s-tx``
* DMA channels connected to the selected I2S controller
* LittleFS storage mounted internally at `/lfs`
* An ``i2s_payload_partition`` flash partition for captured payload data

Wiring
******

The sample overlay enables the SR100 RDK I2S pin group for ``i2s_di``,
``i2s_do``, ``i2s_fsync``, and ``i2s_bclk``. Use the board schematics or silk
labels to locate the routed header/test-point signals for these pins.

Microphone capture:

* Connect microphone data output to ``i2s_di``.
* Connect the microphone BCLK input/output to ``i2s_bclk``.
* Connect LRCLK/FSYNC/WS to ``i2s_fsync``.
* Connect a common ground between the SR100 RDK and the microphone source.
* If the microphone requires a separate supply or master clock, provide those
  from the board or an external source according to the microphone data sheet.

Speaker playback:

* Connect ``i2s_do`` to the speaker amplifier or codec data input.
* Connect ``i2s_bclk`` to the speaker amplifier or codec BCLK pin.
* Connect ``i2s_fsync`` to the speaker amplifier or codec LRCLK/FSYNC/WS pin.
* Connect a common ground between the SR100 RDK and the speaker or codec.
* Provide any codec/amplifier reset, power, or control-interface wiring needed
  by the external device.

External loopback:

* Connect ``i2s_do`` to ``i2s_di``.
* Keep ``i2s_bclk`` and ``i2s_fsync`` routed only once from the controller side.
* Connect a common ground between the loopback wiring and the SR100 RDK.
* Run capture and playback with the same format, rate, bit depth, and channel count.

Console / UART
==============

Use the SR100 RDK console UART and run the ``i2s`` shell commands from the
Zephyr shell prompt.

Configuration
*************

The sample is configured via ``samples/drivers/i2s_shell/Kconfig``.

Key options:

* ``I2S_SHELL_LOG_LEVEL``: sample log level
* ``I2S_SHELL_PRINT_CAPTURE_INFO``: print storage dump information after capture
* ``I2S_SHELL_BLOCK_SIZE``: I2S slab block size
* ``I2S_SHELL_BLOCK_COUNT``: number of TX/RX slab blocks
* ``I2S_SHELL_WRITE_BUFFER_COUNT``: storage capture write-buffer count
* ``I2S_SHELL_WRITE_BUFFER_BYTES``: storage capture write-buffer size
* ``I2S_SHELL_WORKER_STACK_SIZE``: stream worker stack size
* ``I2S_SHELL_WRITER_STACK_SIZE``: storage writer stack size
* ``I2S_SHELL_WORKER_PRIORITY``: stream worker priority

The storage write buffers are staging buffers for file-backed capture. They do
not set the maximum file capture duration. File capture duration is limited by
the free space in `i2s_payload_partition`.

RAM capture duration is limited by `I2S_SHELL_RAM_BUFFER_BYTES`.

Building and Running
********************

Build commands
==============

Build the I2S shell sample:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/i2s_shell
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :compact:

Flash/run using your normal runner, then monitor the UART log.

Shell commands
==============

Supported ``i2s`` commands:

* ``i2s info``
* ``i2s caps <rate> <bits> <channels>``
* ``i2s capture <format> <rate> <bits> <channels> <duration_sec> [file]``
* ``i2s play``
* ``i2s play <file>``
* ``i2s play <file> <duration_sec>``
* ``i2s play sine <format> <rate> <bits> <channels> <duration_sec>``
* ``i2s stop``

Where ``<format>`` is one of:

* ``i2s``
* ``left``
* ``right``

Capture command behavior:

* If `file` is provided, capture is stored under `/lfs/<file>` and metadata
  is stored as `/lfs/<file>.meta`.
* If `file` is omitted, capture is stored in RAM.
* For RAM capture, `duration_sec` must be nonzero.
* For file capture, `duration_sec` may be `0` to capture until `i2s stop`
  or until storage/write failure.

Playback command behavior:

* `i2s play` plays the most recent RAM capture.
* `i2s play <file>` plays the full captured file using `<file>.meta`.
* `i2s play <file> <duration_sec>` plays only the requested duration.
* File playback does not take format, rate, bit depth, or channel count on the
  command line. These values are read from the sidecar metadata file.


Supported validation matrix
===========================

Rates:

* ``8000``
* ``16000``
* ``44100``
* ``48000``

Bit depths:

* ``8``
* ``16``
* ``24``

Channel counts:

* ``1``
* ``2``

Formats:

* ``i2s``
* ``left``
* ``right``

Validation Flow
***************

Bring-up:

.. code-block:: console

i2s info
i2s caps 16000 16 2
i2s capture i2s 16000 16 2 1 bringup.bin
i2s play bringup.bin

Sine playback:

.. code-block:: console

   i2s play sine i2s 16000 16 2 5

RAM capture and playback:

.. code-block:: console

   i2s capture i2s 16000 16 2 5
   i2s play

Capture to a storage-backed file:

.. code-block:: console

   i2s capture i2s 16000 16 2 10 mic_16k.bin

Replay captured file:

.. code-block:: console

   i2s play mic_16k.bin

Replay only the first five seconds of a captured file:

.. code-block:: console

   i2s play mic_16k.bin 5

Left-justified loopback example:

.. code-block:: console

   i2s capture left 16000 16 2 5 captures/left/capture_left.bin
   i2s play captures/left/capture_left.bin

Right-justified sine playback:

.. code-block:: console

   i2s play sine right 48000 16 2 5

Stop an infinite sine playback:

.. code-block:: console

   i2s play sine i2s 48000 16 2 0
   i2s stop

Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   [00:00:00.347,000] <inf> i2s_shell: I2S shell ready: rx=i2s@5031e000 tx=i2s@5031e000 source=chosen:syna_i2s_rx/syna_i2s_tx role=controller fs=/lfs
   uart:~$ i2s info
   I2S shell
   rx              : i2s@5031e000 ready
   tx              : i2s@5031e000 ready
   source          : chosen:syna_i2s_rx/syna_i2s_tx
   role            : controller
   state           : idle
   LittleFS        : mounted (/lfs)
   file bytes      : 0
   RAM bytes       : 0
   last result     : 0
   uart:~$ i2s capture i2s 16000 16 2 10 mic_16k.bin
   capture started: format=i2s 16000 Hz 16-bit 2 ch sec=10 -> /lfs/mic_16k.bin and /lfs/mic_16k.bin.meta
   [00:00:10.500,000] <inf> i2s_shell: operation done: 0
   uart:~$ i2s play mic_16k.bin
   playback started: /lfs/mic_16k.bin
   [00:00:21.000,000] <inf> i2s_shell: operation done: 0

Storage Dump (Optional)
***********************

The XSPI dump command reads the full `i2s_payload_partition` from flash. The
resulting file is a LittleFS filesystem image, not a raw PCM audio file. Do not
import the full dump image directly into Audacity.

Example XSPI dump:

.. code-block:: console

   python tools/openocd/scripts/read_xspi_tcl.py 
   --cfg_path tools/openocd/sr100_m55.cfg 
   --flash-offset 0x880000 
   --size 0x780000 
   --output i2s_payload_lfs.bin

Extract all files from the dumped LittleFS image:

.. code-block:: console

   python tools/openocd/scripts/lfs_extract.py 
   --image ./i2s_payload_lfs.bin 
   --output-dir ./extracted_lfs

On Windows PowerShell:

.. code-block:: powershell

   python .\tools\openocd\scripts\lfs_extract.py `     --image .\i2s_payload_lfs.bin`
   --output-dir .\extracted_lfs

After extraction, the captured audio file and its metadata sidecar will appear
under the output directory. For example:

.. code-block:: text

   extracted_lfs/44Khz_capture_i2s_16.bin
   extracted_lfs/44Khz_capture_i2s_16.bin.meta

Import only the extracted audio `.bin` file into Audacity. Do not import
`i2s_payload_lfs.bin` directly.

For a 44.1 kHz, 16-bit, stereo capture, use:

.. code-block:: text

   File -> Import -> Raw Data
   Encoding    : Signed 16-bit PCM
   Byte order  : Little-endian
   Channels    : 2 Channels / Stereo
   Sample rate : 44100 Hz
   Offset      : 0

Storage Notes

---

For file-backed capture, the maximum duration depends on free LittleFS space:

.. code-block:: text

max_seconds = usable_free_bytes / (rate * channels * bytes_per_sample)

For this shell:

* 8-bit samples are stored as 1 byte per sample.
* 16-bit samples are stored as 2 bytes per sample.
* 24-bit samples are stored as 4 bytes per sample.

Examples for stereo capture:

.. code-block:: text

16000 Hz, 16-bit, 2 ch  ->  64,000 bytes/sec
16000 Hz, 24-bit, 2 ch  -> 128,000 bytes/sec
44100 Hz, 16-bit, 2 ch  -> 176,400 bytes/sec
44100 Hz, 24-bit, 2 ch  -> 352,800 bytes/sec
48000 Hz, 24-bit, 2 ch  -> 384,000 bytes/sec

The write-buffer settings control temporary staging during file capture. They
help tolerate LittleFS write latency, but they do not increase the final storage
capacity.

Notes
*****

* Capture takes the I2S data format as a command argument.
* File playback does not take the I2S data format as a command argument.
* File-backed replay uses the sidecar metadata stored as `<file>.meta`.
* Files are addressed by relative paths and resolved under `/lfs`.
* Logical mono playback uses two physical I2S slots by duplicating the mono
  sample into both left and right slots.
* Use `i2s stop` to stop active capture or playback.
