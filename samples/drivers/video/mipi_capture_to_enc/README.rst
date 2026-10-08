.. zephyr:code-sample:: mipi_capture_to_enc
   :name: MIPI Capture-to-Encoder Sample
   :relevant-api: video_interface

   Capture a 1920x1080 RAW8 Bayer frame through the MIPI path and encode 960x540
   quadrants to JPEG. Optionally store the four encoded quadrants in XSPI.

Description
***********

This sample integrates the MIPI capture driver and JPEG encoder. It captures a
1920x1080 RAW8 Bayer frame, copies 960x540 quadrants into the encoder
reserved-memory raw input region, and then encodes those quadrants to JPEG.

Two modes are supported:

* Default: encode one selected quadrant and keep it in an application buffer
  for debugger dump.
* XSPI store (``CONFIG_STORE_TO_XSPI=y``): encode all 4 quadrants
  and store each encoded JPEG in the XSPI ``jpeg_store`` partition.
* USB CDC stream (``CONFIG_USB_TRANSPORT_CDC_ACM=y``): send each
  encoded quadrant JPEG to the host over a CDC ACM (virtual COM port). When
  XSPI storage is also enabled, the sample stores to XSPI first, then
  enumerates USB and streams the quadrants by reading them back from XSPI.
* USB video stream (``CONFIG_USB_TRANSPORT_UVC=y``): enumerate as a USB Video
  Class (UVC) device and stream the quadrants as an MJPEG stream. When XSPI
  storage is also enabled, the sample stores to XSPI first, then streams by
  reading them back from XSPI.

Requirements
************

Hardware:

* Synaptics SR100 RDK (``sr100_rdk/sr100/m55``)
* OV02C10 camera sensor connected to the SR100 RDK MIPI input

Devicetree:

* An enabled MIPI capture node (``compatible = "syna,mipi-video"``)
* An enabled JPEG encoder node (``compatible = "syna,enc-video"``) in
  reserved-memory input mode (``mode = <1>``)
* Reserved non-cacheable ``memory-region`` nodes for:
  * MIPI capture working memory
  * JPEG encoder working memory (raw input and JPEG output)
* XSPI fixed partition labeled ``jpeg_store`` (optional; only required when
  ``CONFIG_STORE_TO_XSPI=y``)

SRAM/ITCM note:

The provided SR100 overlay maps the Zephyr code region (``zephyr,flash`` /
``zephyr,code-partition``) to SRAM (instead of ITCM) to avoid ITCM overflow when
USB + sample features are enabled.

Configuration
*************

The sample is configured via ``samples/drivers/video/mipi_capture_to_enc/Kconfig``.

Building and Running
********************

Build commands
==============

Build the default flow (no XSPI, no USB CDC):

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/mipi_capture_to_enc
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :compact:

Enable XSPI storage (encode all 4 quadrants and store to XSPI):

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/mipi_capture_to_enc
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :gen-args: -DCONFIG_STORE_TO_XSPI=y
   :compact:

Enable USB CDC ACM streaming (no XSPI):

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/mipi_capture_to_enc
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :gen-args: -DCONFIG_USB_TRANSPORT_CDC_ACM=y -DEXTRA_CONF_FILE=prj_cdc.conf -DDTC_OVERLAY_FILE="boards/sr100_rdk_m55.overlay;boards/sr100_rdk_m55_cdc.overlay"
   :compact:

Enable USB video streaming (no XSPI):

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/mipi_capture_to_enc
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :gen-args: -DCONFIG_USB_TRANSPORT_UVC=y -DEXTRA_CONF_FILE=prj_uvc.conf -DDTC_OVERLAY_FILE="boards/sr100_rdk_m55.overlay;boards/sr100_rdk_m55_uvc.overlay"
   :compact:

Enable XSPI storage + USB CDC streaming (store first, then stream from XSPI):

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/mipi_capture_to_enc
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :gen-args: -DCONFIG_STORE_TO_XSPI=y -DCONFIG_USB_TRANSPORT_CDC_ACM=y -DEXTRA_CONF_FILE=prj_cdc.conf -DDTC_OVERLAY_FILE="boards/sr100_rdk_m55.overlay;boards/sr100_rdk_m55_cdc.overlay"
   :compact:

Enable XSPI storage + USB video streaming (store first, then stream from XSPI):

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/mipi_capture_to_enc
   :board: sr100_rdk/sr100/m55
   :goals: build
   :west-args: -p always
   :gen-args: -DCONFIG_STORE_TO_XSPI=y -DCONFIG_USB_TRANSPORT_UVC=y -DEXTRA_CONF_FILE=prj_uvc.conf -DDTC_OVERLAY_FILE="boards/sr100_rdk_m55.overlay;boards/sr100_rdk_m55_uvc.overlay"
   :compact:

Flash/run using your normal runner, then monitor the UART log.

USB CDC host script
===================

When USB CDC streaming is enabled, the board enumerates as a CDC ACM UART
(``/dev/ttyACM*`` on Linux). Use the provided script to receive four JPEGs:

Host prerequisites
------------------

The CDC host script uses OpenCV (``cv2``) + NumPy when ``--show`` is enabled. Install them with:

.. code-block:: console

   python3 -m pip install opencv-python numpy

Windows + WSL (USBIPD) workflow
-------------------------------

If you are developing on Windows and building/running the host script inside WSL,
attach the enumerated CDC ACM device to your WSL distro with ``usbipd``:

1. On Windows (PowerShell as Administrator), list USB devices and find the SR100:

   .. code-block:: console

      usbipd list

2. Bind the device (replace ``<BUSID>`` with the value from ``usbipd list``):

   .. code-block:: console

      usbipd bind --busid <BUSID>

3. Attach it to WSL (replace ``<BUSID>``):

   .. code-block:: console

      usbipd attach --wsl --busid <BUSID>

4. In WSL, verify the CDC ACM node appears:

   .. code-block:: console

      ls -l /dev/ttyACM*

   Then run the script (examples below use ``/dev/ttyACM0``; adjust as needed).

Run with sudo (quick start)
---------------------------

On many Linux/WSL setups, access to ``/dev/ttyACM*`` requires elevated privileges
unless your user is in the ``dialout`` group. To run immediately:

.. code-block:: console

   sudo python3 samples/drivers/video/mipi_capture_to_enc/tools/recv_quadrants_cdc.py --port /dev/ttyACM0 --show --toggle-dtr --win-x 50 --win-y 50

Run without sudo (dialout group)
--------------------------------

Add your user to the serial device access group and re-login:

.. code-block:: console

   sudo usermod -aG dialout $USER

Apply the new group membership by logging out and back in. In WSL, you can also
restart the WSL session (e.g. close all WSL terminals, or run ``wsl --shutdown``
from Windows and open WSL again).

After that, you should be able to run without sudo:

.. code-block:: console

   python3 samples/drivers/video/mipi_capture_to_enc/tools/recv_quadrants_cdc.py --port /dev/ttyACM0 --show --toggle-dtr --win-x 50 --win-y 50

Viewing behavior
----------------

When ``--show`` is used, the script displays the four received quadrants (q0..q3)
in order. Close the window (or press a key, depending on your OpenCV backend)
to view the next quadrant.

Sample Output
*************

Example output from an XSPI storage build:

.. code-block:: console

   [0:0:0.137,0] <inf> video_syna_sr100_enc: JPEG encoder driver initialized: enc_mem=0xb4900000 size=645120 raw_off=0 max_jpeg=110336 (memory-region)
   [0:0:0.144,0] <inf> video_ov02c10: pinctrl default state applied
   [0:0:0.149,0] <inf> video_ov02c10: Power down GPIO active: pca6416@20 pin 8
   [0:0:0.155,0] <inf> video_ov02c10: Shutdown GPIO active: pca6416@20 pin 13
   [0:0:0.259,0] <inf> video_ov02c10: MCLK ctrl: wrote 0xb to 0x50330044, read back 0xb
   [0:0:0.459,0] <inf> video_syna_sr100_mipi: Driver initialized: csi=0 pool=0x33f6f000 size=2097152
   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   [0:0:0.467,0] <inf> mipi_capture_to_enc: MIPI capture-to-encoder sample start (encode 4 quadrants)
   [0:0:0.470,0] <inf> mipi_capture_to_enc: mipi video_get_caps ret=0 min_vbuf=2 align=64
   [0:0:0.553,0] <inf> mipi_capture_to_enc: mipi format configured width=1920 height=1080 pitch=1920 size=2073600
   [0:0:0.558,0] <inf> mipi_capture_to_enc: mipi video_set_format ret=0 size=2073600 pitch=1920 (1920x1080)
   [0:0:0.563,0] <inf> mipi_capture_to_enc: mipi app_buf addr=0x33f6f000 size=2073600
   [0:0:0.567,0] <inf> mipi_capture_to_enc: mipi video_enqueue ret=0
   [0:0:0.575,0] <inf> video_syna_sr100_mipi: Using DT SHM pool: addr=0x33f6f000 size=2097152
   [0:0:0.580,0] <inf> video_syna_sr100_mipi: Sensor input bpp=10, driver output pixelformat=0x42474752
   [0:0:0.588,0] <inf> video_syna_sr100_mipi: Configuring csi=0 shm=0x33f6f000 size=2073600
   [0:0:0.588,0] <inf> video_syna_sr100_mipi: Stream started (1920x1080 size=2073600)
   [0:0:0.628,0] <inf> mipi_capture_to_enc: mipi video_stream_start ret=0
   [0:0:1.631,0] <inf> mipi_capture_to_enc: mipi video_dequeue ret=0 frame=0x33f54180 bytesused=2073600
   [0:0:1.667,0] <inf> mipi_capture_to_enc: Copied bottom-left quadrant to encoder memory at 0xb4904000 (from x=0 y=540)
   [0:0:1.674,0] <inf> mipi_capture_to_enc: Configuring encoder JPEG output
   [0:0:1.677,0] <inf> mipi_capture_to_enc: enc video_set_format ret=0 capacity=110336 pitch=0 (960x540)
   [0:0:1.682,0] <inf> mipi_capture_to_enc: enc video_get_caps ret=0 min_vbuf=1 align=64
   [0:0:1.686,0] <inf> mipi_capture_to_enc: enc app_buf[0] addr=0x33eb69c0 size=110336 align=64
   [0:0:1.690,0] <inf> mipi_capture_to_enc: enc video_enqueue[0] ret=0
   [0:0:1.694,0] <inf> mipi_capture_to_enc: Starting encoder stream
   [0:0:1.699,0] <inf> mipi_capture_to_enc: enc video_stream_start ret=0
   [0:0:1.715,0] <inf> mipi_capture_to_enc: Frame captured at addr=0x33eb69c0 bytesused=35719
   [0:0:1.719,0] <inf> mipi_capture_to_enc: Dump JPEG from 0x33eb69c0 (0x33eb69c0 + 35719)
   [0:0:1.723,0] <inf> video_syna_sr100_enc: JPEG encoder stream stopped
   [0:0:1.727,0] <inf> video_syna_sr100_mipi: Stream stopped
   [0:0:1.730,0] <inf> mipi_capture_to_enc: MIPI capture-to-encoder sample finished

Example output from an XSPI + USB CDC build (store first, then stream):

.. code-block:: console

   [0:0:1.791,0] <inf> mipi_capture_to_enc: Stored JPEG to XSPI off=0x1000 bytes=17265 erase=4096 crc32=0x0
   [0:0:1.964,0] <inf> mipi_capture_to_enc: Stored JPEG to XSPI off=0x6000 bytes=15721 erase=4096 crc32=0x0
   [0:0:2.148,0] <inf> mipi_capture_to_enc: Stored JPEG to XSPI off=0xa000 bytes=15113 erase=4096 crc32=0x0
   [0:0:2.323,0] <inf> mipi_capture_to_enc: Stored JPEG to XSPI off=0xe000 bytes=14453 erase=4096 crc32=0x0
   [0:0:2.342,0] <inf> mipi_capture_to_enc: Updated XSPI header entries: magic=0x514a5047 count=4
   [0:0:2.367,0] <inf> mipi_capture_to_enc: USB CDC ready; waiting for host DTR...

When you see ``USB CDC ready; waiting for host DTR...``, attach/bind the CDC ACM
device to your development environment (see the Windows + WSL section above),
then run ``tools/recv_quadrants_cdc.py``. Opening the serial port asserts DTR, after
which the device streams the four quadrants.

USB Video (UVC) host script
===========================

When USB video streaming is enabled, the board enumerates as a video device
(``/dev/video*`` on Linux). Use the script below to capture 4 MJPEG frames
(``q0``..``q3``), show one-by-one and advance on key press:

.. code-block:: console

   sudo python3 samples/drivers/video/mipi_capture_to_enc/tools/recv_quadrants_uvc.py --dev /dev/video0 --show --show-mode seq --step --win-x 50 --win-y 50

The script always writes ``q0.jpg``..``q3.jpg`` into ``/tmp/uvc_dumps`` by default.
Install ``ffmpeg`` on the host if it is not already available.

XSPI Dump (GDB)
===============

Each stored JPEG is logged as:

.. code-block:: console

   Stored JPEG to XSPI off=0x1000 bytes=13235 ...

In addition, the sample prints a ready-to-copy GDB dump command like:

.. code-block:: console

   GDB dump command: dump binary memory top-left.jpg 0x3c801000 (0x3c801000 + 13235)

Use the printed ``GDB dump command: ...`` lines (one per quadrant) to dump the stored JPEGs.

Dump from memory (optional)
--------------------------

Use OpenOCD in one terminal and ``arm-none-eabi-gdb`` in another terminal.

Terminal 1 (OpenOCD)
^^^^^^^^^^^^^^^^^^^^

.. code-block:: console

   openocd -f boards/syna/astra_sr/sr100/support/openocd.cfg

Terminal 2 (GDB)
^^^^^^^^^^^^^^^^

Use the address and size printed by the sample for each quadrant:

.. code-block:: console

   arm-none-eabi-gdb
   target extended-remote :3333
   dump binary memory q0.jpg <ADDR> (<ADDR> + <BYTES>)

The XSPI ``jpeg_store`` partition starts at flash offset ``0x800000`` and is
memory-mapped at XIP base ``0x3c000000`` on SR100. You can use the exact command in the
``GDB dump command: ...`` lines printed by the sample log (recommended), or
compute the addresses using the partition offset + XIP base as shown below.

Dump each quadrant JPEG with:

.. code-block:: console

   # Use the off=0x... and bytes=... from the UART log for each quadrant.
   # Addresses below assume XIP base 0x3c000000 and jpeg_store offset 0x800000.

   dump binary memory q0_top_left.jpg     (0x3c800000 + 0x00001000) ((0x3c800000 + 0x00001000) + <Q0_BYTES> - 1)
   dump binary memory q1_top_right.jpg    (0x3c800000 + <Q1_OFFS>)  ((0x3c800000 + <Q1_OFFS>)  + <Q1_BYTES> - 1)
   dump binary memory q2_bottom_left.jpg  (0x3c800000 + <Q2_OFFS>)  ((0x3c800000 + <Q2_OFFS>)  + <Q2_BYTES> - 1)
   dump binary memory q3_bottom_right.jpg (0x3c800000 + <Q3_OFFS>)  ((0x3c800000 + <Q3_OFFS>)  + <Q3_BYTES> - 1)

Where:

* ``0x3c800000`` = XIP base + ``jpeg_store`` flash offset (``0x3c000000 + 0x800000``)
* Add the logged ``off=0x...`` to get the JPEG start address
* Use ``(<start> + <bytes> - 1)`` for the inclusive end address in GDB
