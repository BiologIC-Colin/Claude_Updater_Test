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

static const struct device *can_dev;
static enum can_update_status current_status = CAN_UPDATE_STATUS_IDLE;
static struct k_mutex update_mutex;
static uint32_t image_offset;
static uint32_t image_size;
static uint16_t current_sequence;

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
 * @brief CAN RX callback
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

	/* Setup CAN filter */
	filter.id = CAN_UPDATE_FILTER_ID;
	filter.mask = CAN_STD_ID_MASK;
	filter.flags = CAN_FILTER_DATA | CAN_FILTER_IDE;

	ret = can_add_rx_filter(can_dev, can_rx_callback, NULL, &filter);
	if (ret < 0) {
		LOG_ERR("Failed to add CAN filter: %d", ret);
		return ret;
	}

	ret = can_start(can_dev);
	if (ret) {
		LOG_ERR("Failed to start CAN: %d", ret);
		return ret;
	}

	LOG_INF("CAN update driver initialized");
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
