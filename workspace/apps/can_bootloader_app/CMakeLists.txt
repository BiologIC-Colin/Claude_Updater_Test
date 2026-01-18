# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Set board root for custom boards
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Set DTS root for custom boards
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Find Zephyr package
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(can_bootloader_app VERSION 1.0.0)

# Add application sources
target_sources(app PRIVATE
    src/main.c
)

# Add custom drivers
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../drivers ${CMAKE_BINARY_DIR}/custom_drivers)

# Add custom libs
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../libs ${CMAKE_BINARY_DIR}/custom_libs)
