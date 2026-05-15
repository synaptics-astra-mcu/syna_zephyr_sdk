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

For console output and flashing, connect to the SR100 RDK console UART
(``uart1`` in the board devicetree).

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

Use OpenOCD in one terminal and ``arm-none-eabi-gdb`` in another terminal, then
in the GDB terminal:

.. code-block:: console

   target extended-remote :3333
   dump binary memory frame_dump.raw <addr> (<addr> + <size>)

Use the address printed by the sample and a size of ``width * height`` bytes.

Example (FHD)
-------------

If the log prints ``Stored captured frame: 2073600 bytes at 0x33f6f000``:

.. code-block:: console

   target extended-remote :3333
   dump binary memory frame_dump.raw 0x33f6f000 (0x33f6f000 + 2073600)

2) Convert RAW to PNG
======================

Use the provided script. It will prompt for:

* The input resolution (``1`` = FHD 1920x1080, ``2`` = WQVGA 480x270)
* The RAW file path (press Enter to use the default ``frame_dump.raw``)

Python prerequisites (host)
---------------------------

The conversion script depends on ``numpy`` and ``matplotlib``. Install them on
your host before running:

.. code-block:: console

   python3 -m pip install --user numpy matplotlib

.. code-block:: console

   python3 samples/drivers/video/tools/raw2png.py
