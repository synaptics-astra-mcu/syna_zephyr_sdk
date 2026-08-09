.. zephyr:code-sample:: person_detection_mipi
   :name: Person Detection MIPI (TFLM + Ethos-U + Host API)

   Run on-device person detection inference using TensorFlow Lite Micro and the Ethos-U55 NPU.

Overview
********

This sample demonstrates on-device person detection using a TensorFlow Lite Micro (TFLM)
model and the Ethos-U55 NPU on the SR100 RDK board.

The TFLM version used is pinned in ``zephyr_srsdk/west.yml``:

- **Repository:** https://github.com/zephyrproject-rtos/tflite-micro
- **Revision:** ``fcc760af130f3a595b5802cdebcc77461e54f382``
- **Path:** ``modules/lib/tflite-micro``

To fetch it, run:

.. code-block:: console

   west update

Building and Running
********************

Build the sample for the SR100 RDK board from the workspace root:

.. zephyr-app-commands::
   :zephyr-app: zephyr_srsdk/samples/person_detection_mipi
   :board: sr100_rdk/sr100/m55
   :goals: build

Flash the built firmware using the `srsdk_tools` flashing tool described below.
This is the board-specific flashing method for the SR100 RDK.

Flashing
========

Use the following steps to flash the firmware image and the model to the SR100 RDK.

Clone the flashing tools repository:

.. code-block:: console

   git clone https://github.com/synaptics-astra-mcu/srsdk_tools.git

Flashing image using Windows
---------------------------

To flash the image, run the following in Windows PowerShell or Command Prompt (adjust paths to your environment):

.. code-block:: console

   cd srsdk_tools
   python openocd_flash.py --openocd <path/openocd.exe> --flash-offset 0x0 --file-offset 0x0 \
       --cfg_path <path\srsdk_tools\Input_Config\sr100_m55.cfg> \
      --image <path_to_image.bin>

After a successful build, the full flash image binary is available at
``syna_zephyr/srsdk_tools/Output/B0_Flash/B0_flash_full_image_GD25LE128_67Mhz_secured.bin``.
Use that file as the `--image` argument when running `openocd_flash.py`.

Loading model
-------------

A ready-to-use TFLite model compiled for Ethos-U55 is included at:
``zephyr_srsdk/samples/person_detection_mipi/models/person_detection_256x480.tflite``

Use the same `openocd_flash.py` command to program the TFLite model into flash at
the model address (``0x3C629000``). Example (Windows PowerShell / CMD):

.. code-block:: console

   python openocd_flash.py --openocd <path/openocd.exe> \
       --flash-offset 0x629000 --file-offset 0x0 --cfg_path <path\srsdk_tools\Input_Config\sr100_m55.cfg> \
       --image person_detection_256x480.tflite

Run
---

After programming the firmware and model binaries, press the SR100 RDK reset
button to start the application. The sample output shown in the "Sample Output" section
below demonstrates the expected logs when the application comes up.

Validation
==========

This sample is now controlled through Host API and UC manager. The default
usecase ID is ``2``.

On Windows, use the CDC ACM port shown in Device Manager for the Host API
control interface, for example ``COM7``.

Validation steps
----------------

1. Build the sample:

   .. code-block:: console

      west build -p always -b sr100_rdk/sr100/m55 samples/person_detection_mipi

2. Flash the firmware image from Windows PowerShell or Command Prompt:

   .. code-block:: console

      python openocd_flash.py --openocd <path/openocd.exe> --flash-offset 0x0 --file-offset 0x0 ^
          --cfg_path <path\srsdk_tools\Input_Config\sr100_m55.cfg> ^
          --image <path_to_zephyr.bin>

3. Flash the person-detection model:

   .. code-block:: console

      python openocd_flash.py --openocd <path/openocd.exe> --flash-offset 0x629000 --file-offset 0x0 ^
          --cfg_path <path\srsdk_tools\Input_Config\sr100_m55.cfg> ^
          --image samples\person_detection_mipi\models\person_detection_256x480.tflite

4. Reset the board and verify the UART log shows the application and Host API
   startup, including the usecase registration.

5. Sanity-check the Host API system service:

   .. code-block:: console

      py -3 tools\scripts\host_api\host_api_system_tool.py basic --port COM7

6. Open the stream viewer on the second CDC ACM port before starting the
   usecase:

   .. code-block:: console

      py -3 samples\subsys\host_api_mipi_capture\tools\recv_stream_cdc.py --port COM8 --save-latest --show

   ``COM8`` should be the second enumerated CDC port for the device. The Host
   API control port remains the first CDC port used by the Host API system tool.

7. Create the person detection usecase on the Host API control CDC:

   .. code-block:: console

      py -3 tools\scripts\host_api\host_api_uc_manager_tool.py create --port COM7 --usecase-id 2

8. Start the person detection usecase:

   .. code-block:: console

      py -3 tools\scripts\host_api\host_api_uc_manager_tool.py start --port COM7 --usecase-id 2

9. Stop the person detection usecase from the Host API control CDC:

   .. code-block:: console

      py -3 tools\scripts\host_api\host_api_uc_manager_tool.py stop --port COM7 --usecase-id 2

10. Optional kill validation:

   .. code-block:: console

      py -3 tools\scripts\host_api\host_api_uc_manager_tool.py kill --port COM7 --usecase-id 2

Verification checklist
----------------------

Verify the following during validation:

* Board UART shows Host API startup and usecase registration.
* ``basic`` system-service validation passes on the Host API control port.
* The stream viewer is opened first on the second CDC ACM port.
* ``create`` returns success and the board log shows inference and streaming
  runtime preparation.
* ``start`` returns success and the board log shows the live inference loop and
  JPEG stream worker starting.
* The stream viewer opens on the second CDC ACM port and shows live frames.
* ``stop`` returns success and the board log shows the live loop stopping.
* If used, ``kill`` returns success and the usecase returns to the not-created
  state.

Expected logs on the board UART:

* ``Host API is ready; usecase ID 2 is registered``
* ``create: complete``
* ``UC manager created usecase 2``
* ``startusecase accepted; worker will capture, encode, and stream JPEG frames``
* ``metadata updated for seq=...``
* ``stop: complete``
* ``UC manager stopped usecase 2``


Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   [00:00:00.194,000] <inf> person_detection: running inference
   [00:00:00.242,000] <inf> person_detection: Invoke done in 48 ms
   [00:00:00.243,000] <inf> person_detection: Person Detections: 2
   [00:00:00.243,000] <inf> person_detection: class=0 score=0.930 box=[229.7,8.9,179.2,246.3]
   [00:00:00.243,000] <inf> person_detection: class=0 score=0.910 box=[84.7,31.2,145.7,224.0]

