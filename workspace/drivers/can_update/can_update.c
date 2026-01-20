/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "can_update.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(can_update, CONFIG_LOG_DEFAULT_LEVEL);

#define CAN_UPDATE_FILTER_ID CONFIG_CAN_UPDATE_FILTER_ID
#define CAN_UPDATE_CHUNK_SIZE CONFIG_CAN_UPDATE_CHUNK_SIZE

/* J1939 Configuration */
#define J1939_SRC_ADDR 0x80  /* Our device address */
#define J1939_DST_ADDR 0x00  /* Host address */
#define J1939_PRIORITY 6     /* Default priority */

static const struct device *can_dev;
static enum can_update_status current_status = CAN_UPDATE_STATUS_IDLE;
static struct k_mutex update_mutex;
static uint32_t image_offset;
static uint32_t image_size;
static uint16_t current_sequence;
static uint8_t total_packets;
static uint8_t packets_received;

/* Flash area for image update */
static const struct flash_area *flash_area_image;

/**
 * @brief Process CAN update start message
 */
static int process_start_message(const uint8_t *data, uint8_t len)
{
	int ret;

	if (len < 4) {
		LOG_ERR("Invalid start message length");
		return -EINVAL;
	}

	k_mutex_lock(&update_mutex, K_FOREVER);

	if (current_status == CAN_UPDATE_STATUS_IN_PROGRESS) {
		LOG_WRN("Update already in progress");
		k_mutex_unlock(&update_mutex);
		return -EBUSY;
	}

	/* Extract image size from message (4 bytes, little-endian) */
	image_size = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
	image_offset = 0;
	current_sequence = 0;

	LOG_INF("Starting CAN update, image size: %u bytes", image_size);

	/* Open flash area for update (slot 1) */
	ret = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &flash_area_image);
	if (ret) {
		LOG_ERR("Failed to open flash area: %d", ret);
		current_status = CAN_UPDATE_STATUS_ERROR;
		k_mutex_unlock(&update_mutex);
		return ret;
	}

	/* Erase flash area */
	ret = flash_area_erase(flash_area_image, 0, flash_area_image->fa_size);
	if (ret) {
		LOG_ERR("Failed to erase flash area: %d", ret);
		flash_area_close(flash_area_image);
		current_status = CAN_UPDATE_STATUS_ERROR;
		k_mutex_unlock(&update_mutex);
		return ret;
	}

	current_status = CAN_UPDATE_STATUS_IN_PROGRESS;
	k_mutex_unlock(&update_mutex);

	LOG_INF("CAN update started successfully");
	return 0;
}

/**
 * @brief Process CAN update data message
 */
static int process_data_message(const uint8_t *data, uint8_t len)
{
	int ret;
	uint16_t sequence;

	if (len < 3) {
		LOG_ERR("Invalid data message length");
		return -EINVAL;
	}

	k_mutex_lock(&update_mutex, K_FOREVER);

	if (current_status != CAN_UPDATE_STATUS_IN_PROGRESS) {
		LOG_ERR("No update in progress");
		k_mutex_unlock(&update_mutex);
		return -EINVAL;
	}

	/* Extract sequence number (2 bytes) */
	sequence = data[0] | (data[1] << 8);

	if (sequence != current_sequence) {
		LOG_ERR("Sequence mismatch: expected %u, got %u", current_sequence, sequence);
		k_mutex_unlock(&update_mutex);
		return -EINVAL;
	}

	/* Data starts at byte 2 */
	uint8_t data_len = len - 2;
	const uint8_t *payload = &data[2];

	/* Write to flash */
	ret = flash_area_write(flash_area_image, image_offset, payload, data_len);
	if (ret) {
		LOG_ERR("Failed to write to flash at offset %u: %d", image_offset, ret);
		current_status = CAN_UPDATE_STATUS_ERROR;
		k_mutex_unlock(&update_mutex);
		return ret;
	}

	image_offset += data_len;
	current_sequence++;

	if (image_offset % 1024 == 0) {
		LOG_INF("Progress: %u/%u bytes", image_offset, image_size);
	}

	k_mutex_unlock(&update_mutex);
	return 0;
}

/**
 * @brief Process CAN update end message
 */
static int process_end_message(void)
{
	int ret;

	k_mutex_lock(&update_mutex, K_FOREVER);

	if (current_status != CAN_UPDATE_STATUS_IN_PROGRESS) {
		LOG_ERR("No update in progress");
		k_mutex_unlock(&update_mutex);
		return -EINVAL;
	}

	flash_area_close(flash_area_image);

	if (image_offset != image_size) {
		LOG_ERR("Image size mismatch: expected %u, received %u",
		        image_size, image_offset);
		current_status = CAN_UPDATE_STATUS_ERROR;
		k_mutex_unlock(&update_mutex);
		return -EINVAL;
	}

	/* Mark image as pending for MCUboot */
	ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
	if (ret) {
		LOG_ERR("Failed to request upgrade: %d", ret);
		current_status = CAN_UPDATE_STATUS_ERROR;
		k_mutex_unlock(&update_mutex);
		return ret;
	}

	current_status = CAN_UPDATE_STATUS_SUCCESS;
	k_mutex_unlock(&update_mutex);

	LOG_INF("CAN update completed successfully, reboot to apply");
	return 0;
}

/**
 * @brief Send J1939 CTS (Clear to Send) message
 */
static void send_j1939_cts(uint8_t num_packets, uint8_t next_packet)
{
	struct can_frame frame;
	uint32_t can_id;

	can_id = j1939_build_can_id(J1939_PRIORITY, J1939_PGN_TP_CM,
	                              J1939_SRC_ADDR, J1939_DST_ADDR);

	frame.id = can_id;
	frame.flags = CAN_FRAME_IDE; /* Extended ID */
	frame.dlc = 8;

	frame.data[0] = J1939_TP_CM_CTS;
	frame.data[1] = num_packets;  /* Number of packets that can be sent */
	frame.data[2] = next_packet;  /* Next packet number to be sent */
	frame.data[3] = 0xFF;         /* Reserved */
	frame.data[4] = 0xFF;         /* Reserved */
	frame.data[5] = (J1939_PGN_FIRMWARE_UPDATE) & 0xFF;
	frame.data[6] = (J1939_PGN_FIRMWARE_UPDATE >> 8) & 0xFF;
	frame.data[7] = (J1939_PGN_FIRMWARE_UPDATE >> 16) & 0xFF;

	can_send(can_dev, &frame, K_MSEC(100), NULL, NULL);
	LOG_DBG("Sent CTS: %d packets, next=%d", num_packets, next_packet);
}

/**
 * @brief Send J1939 EOM (End of Message) acknowledgment
 */
static void send_j1939_eom(uint32_t total_bytes, uint8_t total_pkts)
{
	struct can_frame frame;
	uint32_t can_id;

	can_id = j1939_build_can_id(J1939_PRIORITY, J1939_PGN_TP_CM,
	                              J1939_SRC_ADDR, J1939_DST_ADDR);

	frame.id = can_id;
	frame.flags = CAN_FRAME_IDE;
	frame.dlc = 8;

	frame.data[0] = J1939_TP_CM_EOM;
	frame.data[1] = total_bytes & 0xFF;
	frame.data[2] = (total_bytes >> 8) & 0xFF;
	frame.data[3] = total_pkts;
	frame.data[4] = 0xFF;
	frame.data[5] = (J1939_PGN_FIRMWARE_UPDATE) & 0xFF;
	frame.data[6] = (J1939_PGN_FIRMWARE_UPDATE >> 8) & 0xFF;
	frame.data[7] = (J1939_PGN_FIRMWARE_UPDATE >> 16) & 0xFF;

	can_send(can_dev, &frame, K_MSEC(100), NULL, NULL);
	LOG_INF("Sent EOM acknowledgment");
}

/**
 * @brief Process J1939 TP.CM RTS (Request to Send)
 */
static int process_j1939_rts(const uint8_t *data)
{
	int ret;
	uint16_t msg_size;
	uint8_t num_packets;

	msg_size = data[1] | (data[2] << 8);
	num_packets = data[3];

	k_mutex_lock(&update_mutex, K_FOREVER);

	if (current_status == CAN_UPDATE_STATUS_IN_PROGRESS) {
		LOG_WRN("Update already in progress");
		k_mutex_unlock(&update_mutex);
		return -EBUSY;
	}

	image_size = msg_size;
	image_offset = 0;
	current_sequence = 0;
	total_packets = num_packets;
	packets_received = 0;

	LOG_INF("J1939 RTS: size=%u bytes, packets=%u", msg_size, num_packets);

	/* Open flash area for update (slot 1) */
	ret = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &flash_area_image);
	if (ret) {
		LOG_ERR("Failed to open flash area: %d", ret);
		current_status = CAN_UPDATE_STATUS_ERROR;
		k_mutex_unlock(&update_mutex);
		return ret;
	}

	/* Erase flash area */
	ret = flash_area_erase(flash_area_image, 0, flash_area_image->fa_size);
	if (ret) {
		LOG_ERR("Failed to erase flash area: %d", ret);
		flash_area_close(flash_area_image);
		current_status = CAN_UPDATE_STATUS_ERROR;
		k_mutex_unlock(&update_mutex);
		return ret;
	}

	current_status = CAN_UPDATE_STATUS_IN_PROGRESS;
	k_mutex_unlock(&update_mutex);

	/* Send CTS to start receiving packets */
	send_j1939_cts(255, 1); /* Request all packets starting from 1 */

	return 0;
}

/**
 * @brief Process J1939 TP.DT (Data Transfer) packet
 */
static int process_j1939_dt(const uint8_t *data, uint8_t len)
{
	int ret;
	uint8_t seq_num;

	if (len < 2) {
		LOG_ERR("Invalid TP.DT length");
		return -EINVAL;
	}

	k_mutex_lock(&update_mutex, K_FOREVER);

	if (current_status != CAN_UPDATE_STATUS_IN_PROGRESS) {
		LOG_ERR("No update in progress");
		k_mutex_unlock(&update_mutex);
		return -EINVAL;
	}

	seq_num = data[0];

	if (seq_num != (current_sequence + 1)) {
		LOG_ERR("Sequence error: expected %u, got %u", current_sequence + 1, seq_num);
		k_mutex_unlock(&update_mutex);
		return -EINVAL;
	}

	/* Data starts at byte 1, up to 7 bytes per packet */
	uint8_t data_len = len - 1;
	const uint8_t *payload = &data[1];

	/* Don't write beyond image size */
	if (image_offset + data_len > image_size) {
		data_len = image_size - image_offset;
	}

	/* Write to flash */
	ret = flash_area_write(flash_area_image, image_offset, payload, data_len);
	if (ret) {
		LOG_ERR("Failed to write to flash at offset %u: %d", image_offset, ret);
		current_status = CAN_UPDATE_STATUS_ERROR;
		k_mutex_unlock(&update_mutex);
		return ret;
	}

	image_offset += data_len;
	current_sequence = seq_num;
	packets_received++;

	if (image_offset % 1024 == 0) {
		LOG_INF("Progress: %u/%u bytes (%u%%)",
		        image_offset, image_size, (image_offset * 100) / image_size);
	}

	/* Check if transfer is complete */
	if (image_offset >= image_size) {
		flash_area_close(flash_area_image);

		/* Mark image as pending for MCUboot */
		ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
		if (ret) {
			LOG_ERR("Failed to request upgrade: %d", ret);
			current_status = CAN_UPDATE_STATUS_ERROR;
			k_mutex_unlock(&update_mutex);
			return ret;
		}

		current_status = CAN_UPDATE_STATUS_SUCCESS;
		k_mutex_unlock(&update_mutex);

		/* Send EOM acknowledgment */
		send_j1939_eom(image_size, total_packets);
		LOG_INF("J1939 update completed successfully");
		return 0;
	}

	k_mutex_unlock(&update_mutex);
	return 0;
}

/**
 * @brief CAN RX callback for J1939 TP.CM messages
 */
static void can_rx_tp_cm_callback(const struct device *dev, struct can_frame *frame,
                                   void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (frame->dlc < 8) {
		return;
	}

	uint8_t control_byte = frame->data[0];

	switch (control_byte) {
	case J1939_TP_CM_RTS:
		process_j1939_rts(frame->data);
		break;
	case J1939_TP_CM_ABORT:
		k_mutex_lock(&update_mutex, K_FOREVER);
		if (flash_area_image) {
			flash_area_close(flash_area_image);
		}
		current_status = CAN_UPDATE_STATUS_IDLE;
		k_mutex_unlock(&update_mutex);
		LOG_INF("J1939 connection aborted");
		break;
	default:
		LOG_DBG("Unhandled TP.CM control byte: 0x%02x", control_byte);
		break;
	}
}

/**
 * @brief CAN RX callback for J1939 TP.DT messages
 */
static void can_rx_tp_dt_callback(const struct device *dev, struct can_frame *frame,
                                   void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (frame->dlc < 2) {
		return;
	}

	process_j1939_dt(frame->data, frame->dlc);
}

/**
 * @brief CAN RX callback (legacy protocol support)
 */
static void can_rx_callback(const struct device *dev, struct can_frame *frame,
			     void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (frame->dlc < 1) {
		return;
	}

	uint8_t msg_type = frame->data[0];
	const uint8_t *data = &frame->data[1];
	uint8_t len = frame->dlc - 1;

	switch (msg_type) {
	case CAN_UPDATE_START:
		process_start_message(data, len);
		break;
	case CAN_UPDATE_DATA:
		process_data_message(data, len);
		break;
	case CAN_UPDATE_END:
		process_end_message();
		break;
	case CAN_UPDATE_ABORT:
		k_mutex_lock(&update_mutex, K_FOREVER);
		if (flash_area_image) {
			flash_area_close(flash_area_image);
		}
		current_status = CAN_UPDATE_STATUS_IDLE;
		k_mutex_unlock(&update_mutex);
		LOG_INF("CAN update aborted");
		break;
	default:
		LOG_WRN("Unknown message type: 0x%02x", msg_type);
		break;
	}
}

int can_update_init(const struct device *dev)
{
	int ret;
	struct can_filter filter;
	uint32_t tp_cm_id, tp_dt_id;

	if (!device_is_ready(dev)) {
		LOG_ERR("CAN device not ready");
		return -ENODEV;
	}

	can_dev = dev;
	k_mutex_init(&update_mutex);

	/* Configure CAN mode */
	ret = can_set_mode(can_dev, CAN_MODE_NORMAL);
	if (ret) {
		LOG_ERR("Failed to set CAN mode: %d", ret);
		return ret;
	}

	/* Setup J1939 TP.CM filter (Connection Management) */
	tp_cm_id = j1939_build_can_id(J1939_PRIORITY, J1939_PGN_TP_CM,
	                               J1939_DST_ADDR, J1939_SRC_ADDR);
	filter.id = tp_cm_id;
	filter.mask = CAN_EXT_ID_MASK;
	filter.flags = CAN_FILTER_IDE;

	ret = can_add_rx_filter(can_dev, can_rx_tp_cm_callback, NULL, &filter);
	if (ret < 0) {
		LOG_ERR("Failed to add TP.CM filter: %d", ret);
		return ret;
	}

	/* Setup J1939 TP.DT filter (Data Transfer) */
	tp_dt_id = j1939_build_can_id(J1939_PRIORITY, J1939_PGN_TP_DT,
	                               J1939_DST_ADDR, J1939_SRC_ADDR);
	filter.id = tp_dt_id;
	filter.mask = CAN_EXT_ID_MASK;
	filter.flags = CAN_FILTER_IDE;

	ret = can_add_rx_filter(can_dev, can_rx_tp_dt_callback, NULL, &filter);
	if (ret < 0) {
		LOG_ERR("Failed to add TP.DT filter: %d", ret);
		return ret;
	}

	/* Also setup legacy filter for backward compatibility */
	filter.id = CAN_UPDATE_FILTER_ID;
	filter.mask = CAN_STD_ID_MASK;
	filter.flags = 0; /* Standard 11-bit ID, data frames */

	ret = can_add_rx_filter(can_dev, can_rx_callback, NULL, &filter);
	if (ret < 0) {
		LOG_WRN("Failed to add legacy filter: %d", ret);
		/* Not fatal, continue */
	}

	ret = can_start(can_dev);
	if (ret) {
		LOG_ERR("Failed to start CAN: %d", ret);
		return ret;
	}

	LOG_INF("CAN update driver initialized with J1939 support");
	LOG_INF("Device address: 0x%02x, Host address: 0x%02x",
	        J1939_SRC_ADDR, J1939_DST_ADDR);
	return 0;
}

int can_update_start(void)
{
	if (!can_dev) {
		return -ENODEV;
	}

	return can_start(can_dev);
}

int can_update_stop(void)
{
	if (!can_dev) {
		return -ENODEV;
	}

	return can_stop(can_dev);
}

enum can_update_status can_update_get_status(void)
{
	return current_status;
}
