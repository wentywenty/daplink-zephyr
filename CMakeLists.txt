# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(daplink-zephyr)

# Custom DTS bindings for daplink,gpio
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(app PRIVATE
    src
    src/dap
    src/usb
    src/hal
    src/flash
)

target_sources(app PRIVATE
    src/main.c
    src/hal/dap_gpio.c
    src/hal/dap_delay.c
    src/dap/DAP.c
    src/dap/SW_DP.c
    src/usb/usbd_context.c
    src/usb/hid_dap.c
    src/flash/flash_blob.h
    src/flash/target_stm32f1.c
    src/flash/target_stm32f4.c
    src/flash/target_stm32f0.c
    src/flash/target_stm32f3.c
    src/flash/target_stm32f7.c
    src/flash/target_stm32l0.c
    src/flash/target_stm32l4.c
    src/flash/target_nrf52.c
    src/flash/target_lpc1768.c
    src/flash/target_detect.c
    src/flash/swd_host.c
    src/flash/intelhex.c
    src/flash/flash_manager.c
    src/flash/vfs_disk.c
)

# Suppress warnings for DAPLink core files
set_source_files_properties(
    src/dap/DAP.c
    src/dap/SW_DP.c
    PROPERTIES
    COMPILE_FLAGS "-Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable"
)
