# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(update_protocol.c)

# Export include directory to interface
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
