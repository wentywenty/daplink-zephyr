# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(daplink-zephyr)

target_include_directories(app PRIVATE
    src
    src/dap
    src/usb
    src/hal
)

target_sources(app PRIVATE
    src/main.c
    src/hal/dap_gpio.c
    src/hal/dap_delay.c
    src/dap/DAP.c
    src/dap/SW_DP.c
    src/usb/usbd_context.c
    src/usb/hid_dap.c
)

# Suppress warnings for DAPLink core files
set_source_files_properties(
    src/dap/DAP.c
    src/dap/SW_DP.c
    PROPERTIES
    COMPILE_FLAGS "-Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable"
)
