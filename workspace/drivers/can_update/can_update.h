/*
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

#ifdef __cplusplus
}
#endif

#endif /* CAN_UPDATE_H_ */
