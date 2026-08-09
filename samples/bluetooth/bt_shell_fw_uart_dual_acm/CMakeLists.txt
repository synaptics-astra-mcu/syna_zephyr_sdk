cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bt_shell_fw_uart_dual_acm)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

target_sources(app PRIVATE
  ../../fw_uart/src/main.c
  ../bt_shell/src/syna_shell.c
)
