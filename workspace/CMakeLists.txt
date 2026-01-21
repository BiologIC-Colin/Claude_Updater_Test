# SPDX-License-Identifier: Apache-2.0
#
# Top-level CMakeLists.txt for workspace module
# This integrates custom drivers and libraries into Zephyr build system

# Add drivers subdirectory
add_subdirectory(drivers)

# Add libraries subdirectory
add_subdirectory(libs)
