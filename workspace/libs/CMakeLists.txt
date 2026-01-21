# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_UPDATE_PROTOCOL update_protocol)
add_subdirectory_ifdef(CONFIG_J1939_ADDRESS_CLAIM j1939_address_claim)
