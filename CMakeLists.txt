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

# Core sources (always compiled)
target_sources(app PRIVATE
    src/main.c
    src/hal/dap_gpio.c
    src/hal/dap_delay.c
    src/dap/DAP.c
    src/dap/SW_DP.c
    src/usb/usbd_context.c
    src/usb/hid_dap.c
)

# JTAG transport (optional; ~9KB Flash)
if (CONFIG_DAPLINK_JTAG)
target_sources(app PRIVATE
    src/dap/JTAG_DP.c
)
endif()

# CMSIS-DAP SWO capture over a UART (optional; needs a daplink,swo DT node)
if (CONFIG_DAPLINK_SWO)
target_sources(app PRIVATE
    src/hal/dap_swo.c
)
endif()

# SEGGER RTT / option bytes over CMSIS-DAP vendor commands (optional)
if (CONFIG_DAPLINK_RTT)
target_sources(app PRIVATE
    src/flash/rtt.c
)
endif()
if (CONFIG_DAPLINK_RTT OR CONFIG_DAPLINK_OPTION_BYTES OR CONFIG_DAPLINK_VENDOR_REBOOT OR CONFIG_DAPLINK_SPI_FLASH OR CONFIG_DAPLINK_SWO_ITM)
target_sources(app PRIVATE
    src/dap/dap_vendor.c
)
endif()

# SWO ITM software decoder (optional)
if (CONFIG_DAPLINK_SWO_ITM)
target_sources(app PRIVATE
    src/hal/swo_itm.c
)
endif()

# Probe-side SPI NOR flash (optional)
if (CONFIG_DAPLINK_SPI_FLASH)
target_sources(app PRIVATE
    src/flash/spi_flash.c
    src/flash/sfdp.c
)
endif()

# MSC drag-and-drop flash programming (optional, requires ~32KB RAM)
if (CONFIG_DAPLINK_MSC_FLASH)
target_sources(app PRIVATE
    src/flash/flash_blob.h
    src/flash/target_probe.h
    src/flash/target_detect.c
    src/flash/flash_mem.c
    src/flash/swd_host.c
    src/flash/intelhex.c
    src/flash/flash_manager.c
    src/flash/vfs_disk.c
)

if (CONFIG_DAPLINK_TARGET_STM32)
target_sources(app PRIVATE
    src/flash/target_stm32_detect.c
    src/flash/target_stm32f0.c
    src/flash/target_stm32f1.c
    src/flash/target_stm32f2.c
    src/flash/target_stm32f3.c
    src/flash/target_stm32f4.c
    src/flash/target_stm32f429.c
    src/flash/target_stm32f7.c
    src/flash/target_stm32l0.c
    src/flash/target_stm32l1.c
    src/flash/target_stm32l4.c
    src/flash/flash_native_stm32.c
    src/flash/flash_native_stm32l4.c
)
endif()

if (CONFIG_DAPLINK_TARGET_MISC)
target_sources(app PRIVATE
    src/flash/target_simple.c
    src/flash/target_nrf52.c
    src/flash/target_lpc1768.c
    src/flash/flash_native_nrf52.c
)
endif()

if (CONFIG_DAPLINK_RISCV)
target_sources(app PRIVATE
    src/flash/riscv_probe.h
    src/flash/target_riscv.c
    src/flash/target_riscv32.c
    src/flash/riscv_jtag_dtm.c
    src/flash/riscv_adi_dtm.c
    src/flash/riscv_gateway_dtm.c
    src/flash/riscv_dm.c
)
endif()
endif()

# Suppress warnings for DAPLink core files
set_source_files_properties(
    src/dap/DAP.c
    src/dap/SW_DP.c
    src/dap/JTAG_DP.c
    PROPERTIES
    COMPILE_FLAGS "-Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable"
)
