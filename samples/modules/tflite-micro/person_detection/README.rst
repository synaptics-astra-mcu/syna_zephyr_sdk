.. zephyr:code-sample:: person_detection
   :name: Person Detection (TFLM + Ethos-U)

   Run on-device person detection inference using TensorFlow Lite Micro and the Ethos-U55 NPU.

Overview
********

This sample demonstrates on-device person detection using a TensorFlow Lite Micro (TFLM)
model and the Ethos-U55 NPU on the SR100 RDK board.

Building and Running
********************

Run the following commands once to fetch the ``tflite-micro`` dependency.

Run these commands from the workspace root.

.. code-block:: console

    west config manifest.project-filter -- +tflite-micro
    west update

- **Repository:** https://github.com/zephyrproject-rtos/tflite-micro
- **Path:** ``modules/lib/tflite-micro``

Build the sample for the SR100 RDK board from the workspace root:

.. zephyr-app-commands::
   :zephyr-app: samples/modules/tflite-micro/person_detection
   :board: sr100_rdk/sr100/m55
   :goals: build

Flash the built firmware using the `srsdk_tools` flashing tool described below.
This is the board-specific flashing method for the SR100 RDK.

Flashing
========

Use the following steps to flash the image, model and input binary to the SR100 RDK.

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
``samples/modules/tflite-micro/person_detection/models/person_detection_256x480.tflite``

Use the same `openocd_flash.py` command to program the TFLite model into flash at
the model address (``0x3C629000``). Example (Windows PowerShell / CMD):

.. code-block:: console

   python openocd_flash.py --openocd <path/openocd.exe> \
       --flash-offset 0x629000 --file-offset 0x0 --cfg_path <path\srsdk_tools\Input_Config\sr100_m55.cfg> \
       --image person_detection_256x480.tflite

Loading input
-------------

Pre-built interleaved test input binaries are provided in the
``samples/modules/tflite-micro/person_detection/inputs/``
directory. Flash one of these to the ``INPUT_FLASH_ADDRESS`` (``0x3CA00000``)
and reboot to run inference. You can also provide your own input binary
programmed to the same address.

Program a test input to the input address (``0x3CA00000``) with the same tool. Example:

.. code-block:: console

   python openocd_flash.py --openocd <path/openocd.exe> \
       --flash-offset 0xA00000 --file-offset 0x0 --cfg_path <path\srsdk_tools\Input_Config\sr100_m55.cfg> \
       --image two_person_usecase.bin

Run
---

After programming the firmware, model and input binaries, press the SR100 RDK reset
button to start the application. When running on SR110 RDK, the expected logs appear
on UART1; the sample output shown in the "Sample Output" section below demonstrates
the expected log messages when inference runs.


Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.3.0-8455-ga066b866be84 ***
   [00:00:00.194,000] <inf> person_detection: running inference
   [00:00:00.242,000] <inf> person_detection: Invoke done in 48 ms
   [00:00:00.243,000] <inf> person_detection: Person Detections: 2
   [00:00:00.243,000] <inf> person_detection: class=0 score=0.930 box=[229.7,8.9,179.2,246.3]
   [00:00:00.243,000] <inf> person_detection: class=0 score=0.910 box=[84.7,31.2,145.7,224.0]

Notes
=====

- The Ethos-U55 NPU driver is used when ``CONFIG_ETHOS_U=y`` and the hardware
  is present and enabled in the device tree.
- Key Kconfig options set in ``prj.conf``:

  - ``CONFIG_TENSORFLOW_LITE_MICRO=y``
  - ``CONFIG_ETHOS_U=y``, ``CONFIG_ETHOS_U55_128=y``
  - ``CONFIG_MAIN_STACK_SIZE=65536``
