.. zephyr:code-sample:: jpeg-encode
   :name: JPEG encode
   :relevant-api: video_interface

   Use the video API to encode JPEG frames from a capture device.

Description
***********

This sample exercises the Synaptics SR100 JPEG encoder video driver
(``compatible = "syna,enc-video"``) via Zephyr's :ref:`video_api`.

Requirements
************

Hardware:

* Synaptics SR100 RDK (``sr100_rdk/sr100/m55``)
* For live (sensor) mode: a supported camera sensor connected to the SR100 RDK

Devicetree:

* An enabled JPEG encoder node (``compatible = "syna,enc-video"``)
* A reserved ``memory-region`` for encoder working memory referenced by the node
  (must be CPU-accessible and non-cacheable via ``zephyr,memory-attr``)

Wiring
******

For live (sensor) mode (``mode = <0>``), connect:

* Sensor CSI-2 data/clock lanes to the CSI-2 input used by the JPEG encoder node
* Sensor control bus to the SR100 RDK I2C bus used by the sensor devicetree node

Configuration
*************

The sample is configured via ``samples/drivers/video/enc/Kconfig``.

Building and Running
********************

Supported modes
===============

The run mode is selected through the ``mode`` devicetree property on the JPEG
encoder node:

* ``mode = <0>``: Sensor/CSI -> JPEG encoder -> reserved memory (live path).
* ``mode = <1>``: Reserved-memory input -> JPEG encoder -> reserved memory.

Build commands
==============

Default (live sensor path, ``mode = <0>``):

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/enc
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :compact:

Reserved-memory input mode build:

Note: This build flag enables the reserved-memory input path used by the sample (it
generates a deterministic 960x540 frame at runtime when enabled).

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/enc
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :gen-args: -DDTS_EXTRA_CPPFLAGS="-DENC_MEMORY_INPUT"
   :compact:

Flash/run using your normal runner (for example ``west flash``), then monitor
the UART log for the GDB dump commands.

Sample Output
*************

.. code-block:: console

   [00:00:00.137,000] <inf> video_syna_sr100_enc: JPEG driver initialized: enc_mem=0xb4900000 size=645120 raw_off=16384 max_jpeg=110336 (memory-region)
   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   [00:00:00.146,000] <inf> enc_video_sample: JPEG encoder validator start (enc-memory-input, mode=1)
   [00:00:00.151,000] <inf> enc_video_sample: enc video_set_format ret=0 capacity=110336 pitch=0 (960x540)
   [00:00:00.200,000] <inf> enc_video_sample: Seeded encoder-memory raw Bayer color-bar pattern at 0xb4904000 size=518400 (960x540)
   [00:00:00.206,000] <inf> enc_video_sample: enc video_get_caps ret=0 min_vbuf=2 align=64
   [00:00:00.210,000] <inf> enc_video_sample: enc app_buf[0] addr=0x33ea64c0 size=131072 align=64
   [00:00:00.215,000] <inf> enc_video_sample: enc video_enqueue[0] ret=0
   [00:00:00.218,000] <inf> enc_video_sample: enc app_buf[1] addr=0x33ec64c0 size=131072 align=64
   [00:00:00.223,000] <inf> enc_video_sample: enc video_enqueue[1] ret=0
   [00:00:00.229,000] <inf> enc_video_sample: enc video_stream_start ret=0
   [00:00:00.274,000] <inf> enc_video_sample: enc video_dequeue ret=0
   [00:00:00.277,000] <inf> enc_video_sample: JPEG output: addr=0x33ea64c0 bytesused=31066
   [00:00:00.281,000] <inf> enc_video_sample: GDB dump (JPEG): dump binary memory out.jpg 0x33ea64c0 (0x33ea64c0 + 31066)
   [00:00:00.285,000] <inf> enc_video_sample: GDB dump (raw): dump binary memory frame_dump.raw 0xb4904000 (0xb4904000 + 0x7e900)
   [00:00:02.189,000] <inf> enc_video_sample: JPEG encoder validator done ret=0

Frame Dump (Optional)
=====================

The sample logs the GDB dump commands needed to extract either the raw input
frame (LP memory) or the encoded JPEG output buffer.

Start OpenOCD in one terminal:

.. code-block:: console

   openocd -f <PATH>/syna_zephyr/srsdk_tools/Input_Config/sr100_m55.cfg

1) Dump from memory
===================

Use OpenOCD in one terminal and ``arm-none-eabi-gdb`` in another terminal, then
in the GDB terminal:

.. code-block:: console

   target extended-remote :3333
   dump binary memory frame_dump.raw 0x33ea64c0 (0x33ea64c0 + 0x7E900)  # Mode 1 (M2M) (960x540 RAW8)
   dump binary memory frame_dump.raw 0x33ea64c0 (0x33ea64c0 + 0x1FA40)  # Mode 0 (S2M) (480x270 RAW8)

For the JPEG output, use the address and size printed by the sample:

.. code-block:: console

   target extended-remote :3333
   dump binary memory out.jpg <APP_BUF_ADDR> (<APP_BUF_ADDR> + <BYTESUSED>)
