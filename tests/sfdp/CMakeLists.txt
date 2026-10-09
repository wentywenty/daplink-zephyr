cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sfdp_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash)
target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash/sfdp.c
)
