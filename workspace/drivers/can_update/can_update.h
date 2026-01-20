*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CAN_UPDATE_H_
#define CAN_UPDATE_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief J1939 Transport Protocol Control Bytes
 */
#define J1939_TP_CM_RTS   16  /* Request to Send */
#define J1939_TP_CM_CTS   17  /* Clear to Send */
#define J1939_TP_CM_EOM   19  /* End of Message Acknowledgment */
#define J1939_TP_CM_BAM   32  /* Broadcast Announce Message */
#define J1939_TP_CM_ABORT 255 /* Connection Abort */

/**
 * @brief J1939 PGN Definitions
 */
#define J1939_PGN_TP_CM 0xEC00  /* Transport Protocol - Connection Management */
#define J1939_PGN_TP_DT 0xEB00  /* Transport Protocol - Data Transfer */
#define J1939_PGN_REQUEST 0xEA00 /* Request PGN */
#define J1939_PGN_FIRMWARE_UPDATE 0xEF00 /* Custom PGN for firmware updates */

/**
 * @brief CAN Update Protocol Message Types
 */
enum can_update_msg_type {
	CAN_UPDATE_START = 0x01,    /* Start update session */
	CAN_UPDATE_DATA = 0x02,     /* Data chunk */
	CAN_UPDATE_END = 0x03,      /* End update session */
	CAN_UPDATE_ABORT = 0x04,    /* Abort update */
	CAN_UPDATE_STATUS = 0x05,   /* Status request/response */
	CAN_UPDATE_ACK = 0x06,      /* Acknowledgment */
	CAN_UPDATE_NACK = 0x07,     /* Negative acknowledgment */
};

/**
 * @brief CAN Update Status Codes
 */
enum can_update_status {
	CAN_UPDATE_STATUS_IDLE = 0x00,
	CAN_UPDATE_STATUS_IN_PROGRESS = 0x01,
	CAN_UPDATE_STATUS_SUCCESS = 0x02,
	CAN_UPDATE_STATUS_ERROR = 0x03,
};

/**
 * @brief Initialize CAN update driver
 *
 * @param dev CAN device
 * @return 0 on success, negative errno on failure
 */
int can_update_init(const struct device *dev);

/**
 * @brief Start CAN update listener
 *
 * @return 0 on success, negative errno on failure
 */
int can_update_start(void);

/**
 * @brief Stop CAN update listener
 *
 * @return 0 on success, negative errno on failure
 */
int can_update_stop(void);

/**
 * @brief Get current update status
 *
 * @return Current update status
 */
enum can_update_status can_update_get_status(void);

/**
 * @brief Helper to build J1939 29-bit CAN ID
 *
 * @param priority Priority (0-7, lower is higher priority)
 * @param pgn Parameter Group Number
 * @param src_addr Source address
 * @param dst_addr Destination address (0xFF for broadcast)
 * @return 29-bit CAN ID
 */
static inline uint32_t j1939_build_can_id(uint8_t priority, uint32_t pgn,
                                           uint8_t src_addr, uint8_t dst_addr)
{
	uint32_t can_id = 0x80000000; /* Extended frame */
	can_id |= (priority & 0x07) << 26;
	can_id |= (pgn & 0x3FFFF) << 8;
	can_id |= (src_addr & 0xFF);

	/* For PDU1 format (PF < 240), include destination address */
	if (((pgn >> 8) & 0xFF) < 240) {
		can_id &= ~(0xFF << 8);
		can_id |= (dst_addr & 0xFF) << 8;
	}

	return can_id;
}

#ifdef __cplusplus
}
#endif

#endif /* CAN_UPDATE_H_ */
