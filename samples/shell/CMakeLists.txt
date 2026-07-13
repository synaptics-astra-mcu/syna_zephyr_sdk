cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(syna_app)

target_sources(app PRIVATE main.c)
target_sources_ifdef(CONFIG_SHELL app PRIVATE prompt.c)
target_sources_ifdef(CONFIG_SHELL app PRIVATE syna_shell.c)
target_sources_ifdef(CONFIG_FILE_SYSTEM_LITTLEFS app PRIVATE lfs_mount.c)
target_sources_ifdef(CONFIG_FILE_SYSTEM_LITTLEFS app PRIVATE shell_lfs_test.c)
