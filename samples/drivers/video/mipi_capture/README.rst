.. zephyr:code-sample:: mipi-capture
   :name: Video capture
   :relevant-api: video_interface

   Use the video API to retrieve video frames from a capture device.

Description
***********

This sample captures a fixed number of frames from a MIPI CSI-2 sensor using the
Zephyr :ref:`video_api`.

Requirements
************

Hardware:

* Synaptics SR100 RDK (``sr100_rdk/sr100/m55``)
* A MIPI CSI-2 camera sensor connected to the SR100 RDK (default devicetree uses
  an OV02C10 sensor on I2C address ``0x36``)

Wiring
******

Connect the camera sensor to the SR100 RDK as follows:

* MIPI CSI-2 data/clock lanes to the CSI-2 input used by ``video_syna0`` (SR100
  capture datapath ``1``, 1 lane by default).
* Sensor control bus to the SR100 RDK I2C1 bus (default OV02C10 node is
  ``ov02c10@36``).

Console / UART
==============

If your SR100 RDK DebugIC exposes a USB-to-UART bridge (CDC-ACM), prefer that for
console output. Otherwise use an external USB-UART adapter connected to the
board console UART pins (board revisions/firmware may differ).

Configuration
*************

The sample is configured via ``samples/drivers/video/mipi_capture/Kconfig``.

Key options:

* ``VIDEO_SAMPLE_CAPTURE_COUNT``: number of frames to capture (default: 10)
* ``VIDEO_SAMPLE_RES_WQVGA`` / ``VIDEO_SAMPLE_RES_FHD``: resolution preset
* ``VIDEO_SAMPLE_BUFFER_COUNT``: number of capture buffers used by the sample

To change the number of captured frames, set ``CONFIG_VIDEO_SAMPLE_CAPTURE_COUNT``
in your build configuration (for example in an overlay ``prj.conf``).

Building and Running
********************

Supported resolutions
=====================

This sample supports:

* FHD (1920x1080) (default)
* WQVGA (480x270)

Build commands
==============

FHD (1920x1080):

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/mipi_capture
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :gen-args: -DFILE_SUFFIX=_fhd
   :compact:

WQVGA (480x270):

Note: For WQVGA builds, select the resolution in Kconfig by setting
   ``VIDEO_SAMPLE_RESOLUTION`` to ``VIDEO_SAMPLE_RES_WQVGA``.

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/mipi_capture
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :gen-args: -DFILE_SUFFIX=_wqvga
   :compact:

Flash/run using your normal runner (for example ``west flash``), then monitor
the UART log.

Sample Output
*************

.. code-block:: console

   [00:00:00.136,000]  [0m<inf> video_ov02c10: pinctrl default state applied [0m
   [00:00:00.142,000]  [0m<inf> video_ov02c10: Power down GPIO active: pca6416@20 pin 8 [0m
   [00:00:00.148,000]  [0m<inf> video_ov02c10: Shutdown GPIO active: pca6416@20 pin 13 [0m
   [00:00:00.252,000]  [0m<inf> video_ov02c10: MCLK ctrl: wrote 0x0000000b to 0x50330044, read back 0x0000000b [0m
   [00:00:00.454,000]  [0m<inf> video_syna_sr100_mipi: Driver initialized: csi=0 pool=0x33f6f000 size=2097152 [0m
   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   [00:00:00.461,000]  [0m<inf> video_sample_app: Zephyr RAM window: [0x33ea0000..0x33f6f000) [0m
   [00:00:00.465,000]  [0m<inf> video_sample_app: Video caps: min_vbuf_count=2 align=64 [0m
   [00:00:00.470,000]  [0m<inf> video_sample_app: Video format configured: 1920x1080 pitch=1920 size=2073600 [0m
   [00:00:00.562,000]  [0m<inf> video_syna_sr100_mipi: Sensor input bpp=10, driver output pixelformat=0x31384142 [0m
   [00:00:00.567,000]  [0m<inf> video_syna_sr100_mipi: Configuring csi=0 shm=0x33f6f000 size=2073600 [0m
   [00:00:00.575,000]  [0m<inf> video_syna_sr100_mipi: Stream started (1920x1080 size=2073600) [0m
   [00:00:01.580,000]  [0m<inf> video_sample_app: Waiting for frame 1/10... [0m
   [00:00:01.598,000]  [0m<inf> video_sample_app: Frame 1 captured: bytesused=2073600 timestamp=1598 buffer=0x33f6f000 [0m
   [00:00:01.603,000]  [0m<inf> video_sample_app: Stored captured frame: 2073600 bytes at 0x33f6f000 [0m
   ...
   [00:00:01.914,000]  [0m<inf> video_sample_app: Video sample finished [0m

Frame Dump (Optional)
=============================

The sample logs the destination address of the stored frame (for example
``Stored captured frame: ... bytes at 0x33f6f000``).

1) Dump the frame from memory
=============================

Launch OpenOCD in one terminal (from the ``zephyr_srsdk`` repository root):

.. code-block:: console

   openocd -f boards/syna/astra_sr/sr100/support/openocd.cfg

In another terminal, start ``arm-none-eabi-gdb`` and connect:

.. code-block:: console

   arm-none-eabi-gdb
   target extended-remote :3333
   dump binary memory frame_dump.raw <addr> (<addr> + <size>)

Use the address printed by the sample and a size of ``width * height`` bytes.
Large dumps (for example FHD) can take several minutes; this is expected.

Example (FHD)
-------------

If the log prints ``Stored captured frame: 2073600 bytes at 0x33f6f000``:

.. code-block:: console

   target extended-remote :3333
   dump binary memory frame_dump.raw 0x33f6f000 (0x33f6f000 + 2073600)

2) View the RAW dump (ffplay) / optionally convert (ffmpeg)
==========================================================

The captured ``frame_dump.raw`` is a RAW8 Bayer dump. Use ``ffplay`` to preview
it with debayering, or ``ffmpeg`` to convert it to a PNG.

From the dump directory, launch ``ffplay`` with debayering and scaling:

.. code-block:: console

   cd zephyr_srsdk/samples/drivers/video/mipi_capture/tools
   ffplay -hide_banner -loglevel error -f rawvideo -pixel_format bayer_rggb8 -video_size 1920x1080 \
     -vf scale=iw*0.5:ih*0.5 frame_dump.raw

Adjust ``-video_size`` for your captured resolution (for example ``480x270`` for
WQVGA). Adjust the Bayer pattern in ``-pixel_format`` if your sensor differs.
