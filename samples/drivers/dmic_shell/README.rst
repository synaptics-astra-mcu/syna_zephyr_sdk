.. zephyr:code-sample:: dmic-shell
   :name: DMIC shell
   :relevant-api: audio_dmic_interface

   Use the DMIC API to run shell-driven microphone capture and playback tests.

Description
***********

This sample provides an interactive shell for validating the SR100 DMIC driver
with Zephyr's DMIC API. It supports RAM-backed capture, LittleFS file-backed
capture, sidecar metadata, and playback of recorded audio through the configured
I2S TX path.

File-backed capture stores the raw audio file and a matching metadata file in
the LittleFS partition mounted at ``/lfs``. The LittleFS partition can be dumped
from XSPI flash and extracted on the host for inspection in Audacity.

Requirements
************

Hardware:

* Synaptics SR100 RDK (``sr100_rdk/sr100/m55``)
* On-board DMIC path enabled through the sample overlay
* Speaker, amplifier, or codec connected to the selected I2S TX output for
  playback validation

Devicetree:

* An enabled DMIC controller selected by alias ``dmic-0``
* An enabled I2S TX controller selected by ``syna,i2s-tx``
* DMA channels connected to the selected DMIC and I2S controllers
* A ``dmic_payload_partition`` flash partition mounted as LittleFS at ``/lfs``

Console / UART
==============

Use the SR100 RDK console UART and run the ``dmic`` shell commands from the
Zephyr shell prompt.

Configuration
*************

The sample is configured via ``samples/drivers/dmic_shell/Kconfig``.

Key options:

* ``DMIC_CAPTURE_BUFFER_KB``: RAM capture buffer size
* ``AUDIO_DMIC_SYNA_SR100_PCM_WIDTH``: PCM container width used by the sample

Building and Running
********************

Build commands
==============

Build the DMIC shell sample:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/dmic_shell
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :compact:

Flash/run using your normal runner, then monitor the UART log.

Shell commands
==============

Supported ``dmic`` commands:

* ``dmic info``
* ``dmic caps <rate> <bits> <channels>``
* ``dmic config <rate> <bits> <channels> [block]``
* ``dmic capture <seconds>``
* ``dmic capture <seconds> <file>``
* ``dmic capture <file>``
* ``dmic play``
* ``dmic play <file-or-meta-file>``
* ``dmic stop``

Supported validation matrix
===========================

Rates:

* ``16000``
* ``44100``
* ``48000``

Bit depths:

* ``24``

Channel counts:

* ``1``
* ``2``

Storage format:

* Signed 32-bit little-endian PCM
* 24 valid microphone bits in each 32-bit container

Validation Flow
***************

Device information:

.. code-block:: console

   dmic info

Configure the capture format:

.. code-block:: console

   dmic config 44100 24 2

Check RAM and LittleFS capacity for a format:

.. code-block:: console

   dmic caps 44100 24 2

Capture to RAM:

.. code-block:: console

   dmic capture 5

Replay the most recent RAM capture:

.. code-block:: console

   dmic play

Capture to a LittleFS-backed file:

.. code-block:: console

   dmic capture 5 captures/dmic_audio.bin

Replay a captured file:

.. code-block:: console

   dmic play captures/dmic_audio.bin.meta

Stop an active capture or playback:

.. code-block:: console

   dmic stop

Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.3.0 ***
   uart:~$ dmic info
   DMIC sample
     device          : dmic@b4800000 (READY)
     i2s tx          : i2s@5031e000 (READY)
     state           : idle
     playback        : idle
     reads           : 0
     captured bytes  : 0
     RAM dump bytes  : 0
     last RAM bytes  : 0
     file bytes      : 0
     LittleFS        : mounted
     last status     : 0
   uart:~$ dmic config 44100 24 2
   configured: rate=44100 container_bits=32 hw_bits=24 channels=2 block=8192
   uart:~$ dmic caps 44100 24 2
   bytes_per_sec=352800 ram_bytes=2097152 ram_max_sec=5 lfs_bytes=<bytes> lfs_max_sec=<seconds> littlefs_file_mode=yes
   uart:~$ dmic capture 5 captures/dmic_audio.bin
   capture started: audio=/lfs/captures/dmic_audio.bin meta=/lfs/captures/dmic_audio.bin.meta
   capture complete: blocks=<blocks> read_bytes=<bytes> stored_bytes=<bytes> status=0
   uart:~$ dmic play captures/dmic_audio.bin.meta
   play started: captures/dmic_audio.bin.meta
   [00:00:00.000,000] <inf> dmic_sample: playback complete: captures/dmic_audio.bin.meta status=0

RAM Dump (Optional)
*******************

RAM-backed capture stores audio in the sample RAM buffer. When capture
completes, the shell prints the captured address range and a command that can be
used with a debugger or dump tool. The most recent RAM capture can also be
played directly with ``dmic play``.

Example:

.. code-block:: console

   uart:~$ dmic config 44100 24 1
   configured: rate=44100 container_bits=32 hw_bits=24 channels=1 block=8192
   uart:~$ dmic capture 5
   capture started: 5 seconds to RAM
   capture complete: blocks=<blocks> read_bytes=<bytes> stored_bytes=<bytes> status=0
   RAM capture saved in memory: start=0x<start> end=0x<end> bytes=<bytes>
   To save it: dump binary memory dmic_audio.bin 0x<start> 0x<end>
   uart:~$ dmic play
   play started: RAM

File Capture
************

File-backed capture creates two files on target. For example,
``dmic capture 5 captures/dmic_audio.bin`` creates:

* ``/lfs/captures/dmic_audio.bin``
* ``/lfs/captures/dmic_audio.bin.meta``

When LittleFS is full or a recording is no longer needed, remove both files
with the filesystem shell:

.. code-block:: console

   fs rm /lfs/captures/dmic_audio.bin
   fs rm /lfs/captures/dmic_audio.bin.meta

The audio file contains signed 32-bit little-endian PCM with 24 valid data bits
in each 32-bit container. The metadata file records the rate, channels,
hardware bit depth, container bit depth, stored byte count, and audio file path.

``dmic capture <file>`` records without a fixed duration. It stops only when
requested with ``dmic stop`` or when LittleFS can no longer accept more data, in
which case the capture fails and the incomplete files should be removed with
``fs rm``.

Playback
********

.. code-block:: console

   dmic play
   dmic play <file-or-meta-file>
   dmic play captures/dmic_audio.bin
   dmic play captures/dmic_audio.bin.meta

Use ``dmic play`` after ``dmic capture <seconds>`` to replay the most recent
RAM-backed capture without reading from LittleFS. File playback reads the
recording metadata and streams the captured audio through the configured I2S TX
output path. Both the audio file path and the matching ``.meta`` file path are
accepted.

Logic-analyzer CSV conversion
=============================

For playback or host-side inspection from a logic-analyzer capture, export the
captured I2S/PCM data to ``.csv`` and convert it to a raw PCM ``.bin`` file with
the helper in this sample's ``tools`` directory.

Configure the logic analyzer I2S/PCM decoder with these settings before export:

.. list-table::
   :header-rows: 1

   * - Setting
     - Value
   * - Clock channel
     - ``Channel 5``
   * - Frame
     - ``Channel 4``
   * - Data
     - ``Channel 6``
   * - Data significant bit
     - ``Most Significant Bit Sent First``
   * - Clock state
     - ``Falling edge``
   * - Audio bit depth
     - ``32 Bits/Word``
   * - Frame signal transitions
     - ``Once each word (I2S, PCM standard)``
   * - Data bits alignment
     - ``Left aligned``
   * - Data bits shift
     - ``Right-shifted by one (I2S typical)``
   * - Signed/Unsigned
     - ``Unsigned``
   * - Word select high
     - ``Channel 2 (right - I2S typical)``

Use the channel numbers that match the connected logic-analyzer probes.

Linux/macOS:

.. code-block:: console

   python samples/drivers/dmic_shell/tools/csv_to_bin.py \
     /path/to/48_24_test.csv \
     dmic_48Khz_24Bit_new.bin \
     --bits 24

Windows PowerShell:

.. code-block:: powershell

   python .\samples\drivers\dmic_shell\tools\csv_to_bin.py `
     "C:\path\to\48_24_test.csv" `
     dmic_48Khz_24Bit_new.bin `
     --bits 24

Use ``--channel 0`` or ``--channel 1`` to export only one channel. By default,
the converter writes both channels in the order present in the CSV.

When ``--bits 24`` is used, the output file contains packed signed 24-bit
little-endian PCM from the hardware sample data. Import this converted file as
24-bit PCM in host tools such as Audacity:

.. code-block:: text

   File -> Import -> Raw Data
   Encoding    : Signed 24-bit PCM
   Byte order  : Little-endian
   Channels    : 1 Channel / Mono or 2 Channels / Stereo
   Sample rate : use the logic-analyzer capture rate, for example 48000 Hz
   Offset      : 0

Storage Dump for Audacity
*************************

Captured file-backed audio is stored as a LittleFS file together with a metadata
descriptor. To copy the recording to a host PC, dump the full
``dmic_payload_partition`` from XSPI flash, then extract the LittleFS image.
The dumped partition image is not a raw PCM audio file. Do not import the full
dump image directly into Audacity.

Example XSPI dump:

.. code-block:: console

   python tools/openocd/scripts/read_xspi_tcl.py \
     --cfg_path tools/openocd/sr100_m55.cfg \
     --flash-offset 0x880000 \
     --size 0x780000 \
     --output dmic_payload_lfs.bin

Extract all files from the dumped LittleFS image:

.. code-block:: console

   python tools/openocd/scripts/lfs_extract.py \
     --image ./dmic_payload_lfs.bin \
     --output-dir ./extracted_lfs

On Windows PowerShell:

.. code-block:: powershell

   python .\tools\openocd\scripts\lfs_extract.py `
     --image .\dmic_payload_lfs.bin `
     --output-dir .\extracted_lfs

After extraction, the captured audio file and its metadata sidecar will appear
under the output directory. For example:

.. code-block:: text

   extracted_lfs/captures/dmic_audio.bin
   extracted_lfs/captures/dmic_audio.bin.meta

Import only the extracted audio ``.bin`` file into Audacity. For a
44.1 kHz, 24-bit, stereo DMIC capture stored in the sample's 32-bit container,
use:

.. code-block:: text

   File -> Import -> Raw Data
   Encoding    : Signed 32-bit PCM
   Byte order  : Little-endian
   Channels    : 2 Channels / Stereo
   Sample rate : 44100 Hz
   Offset      : 0

Use the sample rate and channel count from the matching ``.meta`` file when
importing other captures.

Notes
*****

* Run ``dmic config`` before ``dmic capture`` after boot or reset.
* Audio is stored in LittleFS for file-backed capture.
* Metadata is stored as ``<file>.meta``.
* Delete old file-backed captures with ``fs rm <audio-file>`` and
  ``fs rm <audio-file>.meta`` when LittleFS needs space.
* Playback accepts both audio-file and metadata-file paths.
* File-backed capture replaces any existing file with the same name.
* Capture statistics are printed when recording completes.
