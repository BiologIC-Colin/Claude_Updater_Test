# SPDX-License-Identifier: Apache-2.0

# Add sources to app target instead of creating a library
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/can_update.c
)

# Export include directory
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
