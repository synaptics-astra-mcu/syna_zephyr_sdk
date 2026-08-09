Bluetooth Samples
=================

This directory contains Bluetooth sample applications for the Synaptics Astra
SR110 Zephyr SDK workspace.

Supported target
----------------

The default target used by this workspace is::

  sr100_rdk/sr100/m55

Build requirements
------------------

Before building, make sure the Zephyr environment is set up::

  export ZEPHYR_BASE="$(cd ../zephyr && pwd)"
  export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
  export ZEPHYR_SDK_INSTALL_DIR=/path/to/zephyr-sdk

How to build
------------

Build a sample manually from the workspace root::

  west build -p always -b sr100_rdk/sr100/m55 samples/bluetooth/<sample-name> \
    -d build_samples/<sample-name>

Examples
--------

Build the Bluetooth shell sample::

  west build -p always -b sr100_rdk/sr100/m55 samples/bluetooth/bt_shell \
    -d build_samples/bt_shell

Build the peripheral sample::

  west build -p always -b sr100_rdk/sr100/m55 samples/bluetooth/peripheral \
    -d build_samples/peripheral

Notes
-----

* Use a pristine build when switching between different source trees or
  workspaces.
* Do not reuse ``build_samples`` across different repository paths.
* Some samples may require extra Kconfig options or shared overlays.
* Reuse the same build directory only for the same source tree.
