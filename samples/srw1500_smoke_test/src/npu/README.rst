Overview
********

This npu example is a copy taken from zephyr/samples/modules/tflite-micro/tflm_ethosu and modified for srw1500 board.

A sample application that demonstrates how to run an inference using the TFLM
framework and the Arm Ethos-U NPU.

The sample application runs a model that has been downloaded from the
`Arm model zoo <https://github.com/ARM-software/ML-zoo>`_. This model has then
been optimized using the
`Vela compiler <https://git.mlplatform.org/ml/ethos-u/ethos-u-vela.git>`_.

Vela takes a tflite file as input and produces another tflite file as output,
where the operators supported by Ethos-U have been replaced by an Ethos-U custom
operator. In an ideal case the complete network would be replaced by a single
Ethos-U custom operator.

Generating Vela-compiled model
******************************

Follow the steps below to generate Vela-compiled model and test input/output data.
Use `keyword_spotting_cnn_small_int8`_ model in this sample:

.. _keyword_spotting_cnn_small_int8: https://github.com/Arm-Examples/ML-zoo/tree/master/models/keyword_spotting/cnn_small/model_package_tf/model_archive/TFLite/tflite_int8

1. Downloading the files below from `keyword_spotting_cnn_small_int8`_:

   - cnn_s_quantized.tflite
   - testing_input/input/0.npy
   - testing_output/Identity/0.npy

2. Optimizing the model for Ethos-U using Vela

   Assuming target Ethos-U is U55 and 128 MAC:

   .. code-block:: console

       $ vela cnn_s_quantized.tflite \
       --output-dir . \
       --accelerator-config ethos-u55-128 \
       --system-config Ethos_U55_High_End_Embedded \
       --memory-mode Shared_Sram

3. Removing unnecessary header

   ``testing_input/input/0.npy`` and ``testing_output/0.npy`` have 128-byte header.
   They must be removed for integration with this sample.

   .. code-block:: console

       $ dd if=testing_input/input/0.npy of=testing_input/input/0_no-header.npy bs=1 skip=128
       $ dd if=testing_output/Identity/0.npy of=testing_output/Identity/0_no-header.npy bs=1 skip=128

4. Converting to C array

   .. code-block:: console

       $ xxd -c 16 -i cnn_s_quantized_vela.tflite cnn_s_quantized_vela.tflite.h
       $ xxd -c 16 -i testing_input/input/0_no-header.npy testing_input/input/0_no-header.npy.h
       $ xxd -c 16 -i testing_output/Identity/0_no-header.npy testing_output/Identity/0_no-header.npy.h

5. Synchronizing to this sample

   Synchronize the files below to ``keyword_spotting_cnn_small_int8`` directory
   in this sample:

   - cnn_s_quantized_vela.tflite.h > model.h
   - testing_input/input/0_no-header.npy.h > input.h
   - testing_output/Identity/0_no-header.npy.h > output.h

