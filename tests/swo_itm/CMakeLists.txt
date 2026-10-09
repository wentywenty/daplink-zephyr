cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(swo_itm_test)

# Compile the ITM decoder (guarded by CONFIG_DAPLINK_SWO_ITM) directly.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/hal)
target_compile_definitions(app PRIVATE CONFIG_DAPLINK_SWO_ITM=1)
target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/hal/swo_itm.c
)
