/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * J1939 Address Claim Implementation
 */

#include "j1939_address_claim.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(j1939_ac, CONFIG_LOG_DEFAULT_LEVEL);

/* Address Claim State */
static struct {
	const struct device *can_dev;
	j1939_name_t name;
	uint8_t current_address;
	uint8_t preferred_address;
	uint8_t priority;
	enum j1939_ac_state state;
	bool arbitrary_capable;
	uint32_t claim_timeout_ms;
	j1939_ac_callback_t callback;
	void *user_data;
	struct k_work_delayable claim_work;
	struct k_mutex mutex;
	int filter_id;
} ac_state = {
	.current_address = J1939_NULL_ADDRESS,
	.state = J1939_AC_STATE_INIT,
};

/* Forward declarations */
static void send_address_claimed(uint8_t address);
static void claim_timeout_handler(struct k_work *work);
static void can_rx_address_claimed_callback(const struct device *dev,
                                             struct can_frame *frame,
                                             void *user_data);

/**
 * @brief Build J1939 29-bit CAN ID
 */
static inline uint32_t build_can_id(uint8_t priority, uint32_t pgn, uint8_t src_addr)
{
	uint32_t can_id = 0x80000000; /* Extended frame */
	can_id |= (priority & 0x07) << 26;
	can_id |= (pgn & 0x3FFFF) << 8;
	can_id |= (src_addr & 0xFF);
	return can_id;
}

/**
 * @brief Extract NAME from CAN frame
 */
static j1939_name_t extract_name_from_frame(const struct can_frame *frame)
{
	j1939_name_t name;

	/* NAME is 8 bytes in little-endian format */
	name.value = 0;
	for (int i = 0; i < 8; i++) {
		name.value |= ((uint64_t)frame->data[i]) << (i * 8);
	}

	return name;
}

/**
 * @brief Extract source address from J1939 CAN ID
 */
static inline uint8_t extract_source_addr(uint32_t can_id)
{
	return (can_id & 0xFF);
}

/**
 * @brief Send Address Claimed message
 */
static void send_address_claimed(uint8_t address)
{
	struct can_frame frame;
	uint32_t can_id;
	int ret;

	/* Build CAN ID with ADDRESS_CLAIMED PGN */
	can_id = build_can_id(ac_state.priority, J1939_PGN_ADDRESS_CLAIMED, address);

	frame.id = can_id;
	frame.flags = CAN_FRAME_IDE; /* Extended ID */
	frame.dlc = 8;

	/* Pack NAME into data bytes (little-endian) */
	for (int i = 0; i < 8; i++) {
		frame.data[i] = (ac_state.name.value >> (i * 8)) & 0xFF;
	}

	ret = can_send(ac_state.can_dev, &frame, K_MSEC(100), NULL, NULL);
	if (ret) {
		LOG_ERR("Failed to send Address Claimed: %d", ret);
		return;
	}

	LOG_INF("Sent Address Claimed: addr=0x%02X, NAME=0x%016llX",
	        address, ac_state.name.value);
}

/**
 * @brief Handle contention - another node has higher priority NAME
 */
static void handle_contention(uint8_t conflicting_address, j1939_name_t other_name)
{
	int name_cmp = j1939_name_compare(ac_state.name, other_name);

	LOG_WRN("Address contention detected: addr=0x%02X", conflicting_address);
	LOG_DBG("Our NAME: 0x%016llX, Other NAME: 0x%016llX",
	        ac_state.name.value, other_name.value);

	k_mutex_lock(&ac_state.mutex, K_FOREVER);

	if (name_cmp < 0) {
		/* Our NAME has higher priority - we keep the address */
		LOG_INF("Our NAME has higher priority, keeping address 0x%02X",
		        ac_state.current_address);
		ac_state.state = J1939_AC_STATE_CLAIMED;

		if (ac_state.callback) {
			ac_state.callback(ac_state.current_address, ac_state.state,
			                  ac_state.user_data);
		}
	} else if (name_cmp > 0) {
		/* Other NAME has higher priority - we must find new address */
		LOG_WRN("Other NAME has higher priority, must find new address");
		ac_state.state = J1939_AC_STATE_CONTENTION;

		if (ac_state.arbitrary_capable) {
			/* Find next available address */
			uint8_t new_addr = (ac_state.current_address + 1) & 0xFF;

			/* Search for free address */
			while (new_addr != ac_state.current_address) {
				if (new_addr >= J1939_MIN_UNICAST_ADDRESS &&
				    new_addr <= J1939_MAX_UNICAST_ADDRESS) {
					/* Try this address */
					ac_state.current_address = new_addr;
					ac_state.state = J1939_AC_STATE_CLAIMING;

					LOG_INF("Trying new address: 0x%02X", new_addr);
					send_address_claimed(new_addr);

					/* Wait for contention again */
					k_work_reschedule(&ac_state.claim_work,
					                  K_MSEC(ac_state.claim_timeout_ms));
					break;
				}
				new_addr = (new_addr + 1) & 0xFF;
			}

			if (new_addr == ac_state.current_address) {
				/* No available addresses */
				LOG_ERR("No available addresses found");
				ac_state.state = J1939_AC_STATE_CANNOT_CLAIM;
				ac_state.current_address = J1939_NULL_ADDRESS;

				if (ac_state.callback) {
					ac_state.callback(J1939_NULL_ADDRESS, ac_state.state,
					                  ac_state.user_data);
				}
			}
		} else {
			/* Not arbitrary capable - cannot claim */
			LOG_ERR("Cannot claim address (not arbitrary capable)");
			ac_state.state = J1939_AC_STATE_CANNOT_CLAIM;
			ac_state.current_address = J1939_NULL_ADDRESS;

			if (ac_state.callback) {
				ac_state.callback(J1939_NULL_ADDRESS, ac_state.state,
				                  ac_state.user_data);
			}
		}
	} else {
		/* Identical NAMEs - this should not happen! */
		LOG_ERR("Duplicate NAME detected! This violates J1939 spec");
		ac_state.state = J1939_AC_STATE_CANNOT_CLAIM;
		ac_state.current_address = J1939_NULL_ADDRESS;

		if (ac_state.callback) {
			ac_state.callback(J1939_NULL_ADDRESS, ac_state.state,
			                  ac_state.user_data);
		}
	}

	k_mutex_unlock(&ac_state.mutex);
}

/**
 * @brief CAN RX callback for Address Claimed messages
 */
static void can_rx_address_claimed_callback(const struct device *dev,
                                             struct can_frame *frame,
                                             void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (frame->dlc < 8) {
		return;
	}

	uint8_t claimed_addr = extract_source_addr(frame->id);
	j1939_name_t other_name = extract_name_from_frame(frame);

	LOG_DBG("Received Address Claimed: addr=0x%02X, NAME=0x%016llX",
	        claimed_addr, other_name.value);

	k_mutex_lock(&ac_state.mutex, K_FOREVER);

	/* Check if this conflicts with our address */
	if (claimed_addr == ac_state.current_address &&
	    ac_state.state == J1939_AC_STATE_CLAIMING) {
		k_mutex_unlock(&ac_state.mutex);
		handle_contention(claimed_addr, other_name);
		return;
	}

	k_mutex_unlock(&ac_state.mutex);
}

/**
 * @brief Claim timeout handler
 */
static void claim_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&ac_state.mutex, K_FOREVER);

	if (ac_state.state == J1939_AC_STATE_CLAIMING) {
		/* No contention detected - address is claimed! */
		ac_state.state = J1939_AC_STATE_CLAIMED;
		LOG_INF("Address 0x%02X successfully claimed", ac_state.current_address);

		if (ac_state.callback) {
			ac_state.callback(ac_state.current_address, ac_state.state,
			                  ac_state.user_data);
		}
	}

	k_mutex_unlock(&ac_state.mutex);
}

int j1939_address_claim_init(const struct j1939_ac_config *config,
                              j1939_ac_callback_t callback,
                              void *user_data)
{
	struct can_filter filter;
	int ret;

	if (!config || !config->can_dev) {
		LOG_ERR("Invalid configuration");
		return -EINVAL;
	}

	if (!device_is_ready(config->can_dev)) {
		LOG_ERR("CAN device not ready");
		return -ENODEV;
	}

	k_mutex_lock(&ac_state.mutex, K_FOREVER);

	/* Initialize state */
	ac_state.can_dev = config->can_dev;
	ac_state.name = config->name;
	ac_state.preferred_address = config->preferred_address;
	ac_state.current_address = J1939_NULL_ADDRESS;
	ac_state.priority = config->priority;
	ac_state.arbitrary_capable = config->arbitrary_capable;
	ac_state.claim_timeout_ms = config->claim_timeout_ms;
	ac_state.callback = callback;
	ac_state.user_data = user_data;
	ac_state.state = J1939_AC_STATE_INIT;

	k_mutex_unlock(&ac_state.mutex);

	/* Initialize work item */
	k_work_init_delayable(&ac_state.claim_work, claim_timeout_handler);

	/* Setup filter for Address Claimed messages (global broadcast) */
	filter.id = build_can_id(6, J1939_PGN_ADDRESS_CLAIMED, 0);
	filter.mask = 0x00FFFF00; /* Match PGN only, any source address */
	filter.flags = CAN_FILTER_IDE;

	ret = can_add_rx_filter(ac_state.can_dev, can_rx_address_claimed_callback,
	                        NULL, &filter);
	if (ret < 0) {
		LOG_ERR("Failed to add Address Claimed filter: %d", ret);
		return ret;
	}

	ac_state.filter_id = ret;

	LOG_INF("J1939 Address Claim initialized");
	LOG_INF("NAME: 0x%016llX, Preferred Address: 0x%02X",
	        ac_state.name.value, ac_state.preferred_address);

	return 0;
}

int j1939_address_claim_start(void)
{
	k_mutex_lock(&ac_state.mutex, K_FOREVER);

	if (!ac_state.can_dev) {
		LOG_ERR("Not initialized");
		k_mutex_unlock(&ac_state.mutex);
		return -EINVAL;
	}

	if (ac_state.state == J1939_AC_STATE_CLAIMED) {
		LOG_WRN("Address already claimed: 0x%02X", ac_state.current_address);
		k_mutex_unlock(&ac_state.mutex);
		return 0;
	}

	/* Start with preferred address */
	ac_state.current_address = ac_state.preferred_address;
	ac_state.state = J1939_AC_STATE_CLAIMING;

	k_mutex_unlock(&ac_state.mutex);

	/* Send Address Claimed message */
	send_address_claimed(ac_state.current_address);

	/* Start timeout - if no contention, address is claimed */
	k_work_reschedule(&ac_state.claim_work, K_MSEC(ac_state.claim_timeout_ms));

	LOG_INF("Address claim procedure started for 0x%02X", ac_state.current_address);

	return 0;
}

int j1939_address_claim_stop(void)
{
	k_mutex_lock(&ac_state.mutex, K_FOREVER);

	if (!ac_state.can_dev) {
		LOG_ERR("Not initialized");
		k_mutex_unlock(&ac_state.mutex);
		return -EINVAL;
	}

	/* Cancel any pending work */
	k_work_cancel_delayable(&ac_state.claim_work);

	/* Remove filter */
	if (ac_state.filter_id >= 0) {
		can_remove_rx_filter(ac_state.can_dev, ac_state.filter_id);
	}

	/* Send Address Claimed with NULL address to release */
	if (ac_state.current_address != J1939_NULL_ADDRESS) {
		send_address_claimed(J1939_NULL_ADDRESS);
	}

	ac_state.current_address = J1939_NULL_ADDRESS;
	ac_state.state = J1939_AC_STATE_INIT;

	k_mutex_unlock(&ac_state.mutex);

	LOG_INF("Address claim stopped");

	return 0;
}

uint8_t j1939_address_claim_get_address(void)
{
	uint8_t addr;

	k_mutex_lock(&ac_state.mutex, K_FOREVER);
	addr = ac_state.current_address;
	k_mutex_unlock(&ac_state.mutex);

	return addr;
}

enum j1939_ac_state j1939_address_claim_get_state(void)
{
	enum j1939_ac_state state;

	k_mutex_lock(&ac_state.mutex, K_FOREVER);
	state = ac_state.state;
	k_mutex_unlock(&ac_state.mutex);

	return state;
}

uint64_t j1939_address_claim_get_name(void)
{
	uint64_t name;

	k_mutex_lock(&ac_state.mutex, K_FOREVER);
	name = ac_state.name.value;
	k_mutex_unlock(&ac_state.mutex);

	return name;
}
