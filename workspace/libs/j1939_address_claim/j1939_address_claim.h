/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * J1939 Address Claim Library
 * Implements SAE J1939-81 Address Claim and NAME management
 */

#ifndef J1939_ADDRESS_CLAIM_H_
#define J1939_ADDRESS_CLAIM_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief J1939 Address Claim PGN
 */
#define J1939_PGN_ADDRESS_CLAIMED 0xEE00  /* Address Claimed (59904) */
#define J1939_PGN_REQUEST         0xEA00  /* Request PGN */

/**
 * @brief J1939 Address Constants
 */
#define J1939_NULL_ADDRESS        0xFE    /* Address not claimed yet */
#define J1939_BROADCAST_ADDRESS   0xFF    /* Global broadcast */
#define J1939_MIN_UNICAST_ADDRESS 0x00    /* Minimum unicast address */
#define J1939_MAX_UNICAST_ADDRESS 0xFD    /* Maximum unicast address */

/**
 * @brief J1939 NAME Field Structure (64-bit)
 *
 * Per SAE J1939-81, the NAME field is a 64-bit identifier with the following structure:
 * - Bit 63: Arbitrary Address Capable (0=No, 1=Yes)
 * - Bit 62-56: Industry Group (7 bits)
 * - Bit 55-52: Vehicle System Instance (4 bits)
 * - Bit 51-44: Vehicle System (8 bits)
 * - Bit 43: Reserved (1 bit)
 * - Bit 42-35: Function (8 bits)
 * - Bit 34-24: Function Instance (5 bits)
 * - Bit 23-21: ECU Instance (3 bits)
 * - Bit 20-16: Manufacturer Code (11 bits)
 * - Bit 15-0: Identity Number (21 bits)
 */
typedef struct {
	uint32_t identity_number:21;      /* Unique identity number (bits 0-20) */
	uint32_t manufacturer_code:11;    /* Manufacturer code (bits 21-31) */
	uint8_t ecu_instance:3;           /* ECU instance (bits 32-34) */
	uint8_t function_instance:5;      /* Function instance (bits 35-39) */
	uint8_t function;                 /* Function code (bits 40-47) */
	uint8_t reserved:1;               /* Reserved, must be 0 (bit 48) */
	uint8_t vehicle_system:7;         /* Vehicle system (bits 49-55) */
	uint8_t vehicle_system_instance:4; /* Vehicle system instance (bits 56-59) */
	uint8_t industry_group:3;         /* Industry group (bits 60-62) */
	uint8_t arbitrary_address:1;      /* Arbitrary address capable (bit 63) */
} __attribute__((packed)) j1939_name_fields_t;

/**
 * @brief J1939 NAME as 64-bit value
 */
typedef union {
	uint64_t value;
	j1939_name_fields_t fields;
} j1939_name_t;

/**
 * @brief Address Claim State
 */
enum j1939_ac_state {
	J1939_AC_STATE_INIT,              /* Initialization */
	J1939_AC_STATE_WAIT_CLAIM,        /* Waiting to send claim */
	J1939_AC_STATE_CLAIMING,          /* Claiming address */
	J1939_AC_STATE_CLAIMED,           /* Address successfully claimed */
	J1939_AC_STATE_CANNOT_CLAIM,      /* Cannot claim address */
	J1939_AC_STATE_CONTENTION,        /* Address contention detected */
};

/**
 * @brief Address Claim Configuration
 */
struct j1939_ac_config {
	const struct device *can_dev;    /* CAN device */
	j1939_name_t name;               /* 64-bit NAME */
	uint8_t preferred_address;       /* Preferred address (0xFE for arbitrary) */
	uint8_t priority;                /* Message priority (default 6) */
	bool arbitrary_capable;          /* Can use arbitrary addressing */
	uint32_t claim_timeout_ms;       /* Timeout after sending claim (default 250ms) */
};

/**
 * @brief Address Claim Callback Function
 *
 * @param address The claimed address (J1939_NULL_ADDRESS if cannot claim)
 * @param state Current state
 * @param user_data User-provided data pointer
 */
typedef void (*j1939_ac_callback_t)(uint8_t address, enum j1939_ac_state state, void *user_data);

/**
 * @brief Initialize J1939 Address Claim
 *
 * @param config Configuration structure
 * @param callback Callback function for state changes
 * @param user_data User data passed to callback
 * @return 0 on success, negative errno on failure
 */
int j1939_address_claim_init(const struct j1939_ac_config *config,
                              j1939_ac_callback_t callback,
                              void *user_data);

/**
 * @brief Start Address Claim Procedure
 *
 * Initiates the address claim procedure. The device will:
 * 1. Send Address Claimed message with preferred address
 * 2. Wait for contention (250ms)
 * 3. If no contention, address is claimed
 * 4. If contention and our NAME has lower priority, find new address
 *
 * @return 0 on success, negative errno on failure
 */
int j1939_address_claim_start(void);

/**
 * @brief Stop Address Claim
 *
 * Releases the claimed address and stops the claim procedure.
 *
 * @return 0 on success, negative errno on failure
 */
int j1939_address_claim_stop(void);

/**
 * @brief Get Current Claimed Address
 *
 * @return Current address (J1939_NULL_ADDRESS if not claimed)
 */
uint8_t j1939_address_claim_get_address(void);

/**
 * @brief Get Current State
 *
 * @return Current address claim state
 */
enum j1939_ac_state j1939_address_claim_get_state(void);

/**
 * @brief Get NAME as 64-bit value
 *
 * @return NAME value
 */
uint64_t j1939_address_claim_get_name(void);

/**
 * @brief Helper to build J1939 NAME
 *
 * @param identity_number Unique identity number (21 bits)
 * @param manufacturer_code Manufacturer code (11 bits)
 * @param ecu_instance ECU instance (3 bits)
 * @param function_instance Function instance (5 bits)
 * @param function Function code (8 bits)
 * @param vehicle_system Vehicle system (7 bits)
 * @param vehicle_system_instance Vehicle system instance (4 bits)
 * @param industry_group Industry group (3 bits)
 * @param arbitrary_address Arbitrary address capable (1 bit)
 * @return Constructed NAME
 */
static inline j1939_name_t j1939_name_build(
	uint32_t identity_number,
	uint16_t manufacturer_code,
	uint8_t ecu_instance,
	uint8_t function_instance,
	uint8_t function,
	uint8_t vehicle_system,
	uint8_t vehicle_system_instance,
	uint8_t industry_group,
	bool arbitrary_address)
{
	j1939_name_t name;
	name.value = 0;

	name.fields.identity_number = identity_number & 0x1FFFFF;
	name.fields.manufacturer_code = manufacturer_code & 0x7FF;
	name.fields.ecu_instance = ecu_instance & 0x07;
	name.fields.function_instance = function_instance & 0x1F;
	name.fields.function = function;
	name.fields.reserved = 0;
	name.fields.vehicle_system = vehicle_system & 0x7F;
	name.fields.vehicle_system_instance = vehicle_system_instance & 0x0F;
	name.fields.industry_group = industry_group & 0x07;
	name.fields.arbitrary_address = arbitrary_address ? 1 : 0;

	return name;
}

/**
 * @brief Compare two NAMEs (lower value has higher priority)
 *
 * @param name1 First NAME
 * @param name2 Second NAME
 * @return -1 if name1 has higher priority, 0 if equal, 1 if name2 has higher priority
 */
static inline int j1939_name_compare(j1939_name_t name1, j1939_name_t name2)
{
	if (name1.value < name2.value) {
		return -1;  /* name1 has higher priority */
	} else if (name1.value > name2.value) {
		return 1;   /* name2 has higher priority */
	}
	return 0;       /* Equal */
}

#ifdef __cplusplus
}
#endif

#endif /* J1939_ADDRESS_CLAIM_H_ */
