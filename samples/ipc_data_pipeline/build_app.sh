#!/bin/bash

echo "Starting build..."

source ~/.venvs/syna_zephyr/bin/activate
cd ~/syna_zephyr/zephyr_srsdk
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-1.0.0

# shell app
# west build -p always -b sr100_rdk/sr100/m55 ../zephyr/samples/subsys/shell/shell_module

# blinky app
# west build -p always -b sr100_rdk/sr100/m55 ../zephyr/samples/basic/blinky

# M4 + M55 Helloworld app
# west build -p always -b sr100_rdk/sr100/m4 ../zephyr/samples/hello_world -d build/m4
# west build -p always -b sr100_rdk/sr100/m55 ../zephyr/samples/hello_world -d build/m55 -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD="../m4"

# M4 + M55 Mbox app
# west build -p always -b sr100_rdk/sr100/m4 ../zephyr/samples/drivers/mbox/ -d build/m4
# west build -p always -b sr100_rdk/sr100/m55 ../zephyr/samples/drivers/mbox/remote -d build/m55 -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD="../m4"

# M4 + M55 Mbox data app
# west build -p always -b sr100_rdk/sr100/m4 ../zephyr/samples/drivers/mbox_data/ -d build/m4
# west build -p always -b sr100_rdk/sr100/m55 ../zephyr/samples/drivers/mbox_data/remote -d build/m55 -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD="../m4"

# M4 + M55 ipc_data_pipeline app
west build -p always -b sr100_rdk/sr100/m4 samples/ipc_data_pipeline/remote -d build/m4
west build -p always -b sr100_rdk/sr100/m55 samples/ipc_data_pipeline/ -d build/m55 -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD="../m4"
