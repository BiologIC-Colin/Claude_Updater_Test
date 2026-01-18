/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * CAN Bootloader Application
 * Demonstrates MCUboot with CAN bus firmware updates
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/can.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "can_update.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* LED0 for status indication */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* CAN device - use CAN1 directly */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(can1), okay)
#define CAN_DEV DEVICE_DT_GET(DT_NODELABEL(can1))
#define HAS_CAN_BUS 1
#else
#define HAS_CAN_BUS 0
#warning "No CAN bus available on this board"
#endif

/* Status LED blink thread */
#define LED_THREAD_STACK_SIZE 512
#define LED_THREAD_PRIORITY 5

static void led_blink_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		enum can_update_status status = can_update_get_status();

		switch (status) {
		case CAN_UPDATE_STATUS_IDLE:
			/* Slow blink - idle */
			gpio_pin_toggle_dt(&led);
			k_sleep(K_MSEC(1000));
			break;
		case CAN_UPDATE_STATUS_IN_PROGRESS:
			/* Fast blink - updating */
			gpio_pin_toggle_dt(&led);
			k_sleep(K_MSEC(100));
			break;
		case CAN_UPDATE_STATUS_SUCCESS:
			/* Solid on - success */
			gpio_pin_set_dt(&led, 1);
			k_sleep(K_MSEC(100));
			break;
		case CAN_UPDATE_STATUS_ERROR:
			/* Very fast blink - error */
			gpio_pin_toggle_dt(&led);
			k_sleep(K_MSEC(50));
			break;
		}
	}
}

K_THREAD_DEFINE(led_thread, LED_THREAD_STACK_SIZE, led_blink_thread,
                NULL, NULL, NULL, LED_THREAD_PRIORITY, 0, 0);

int main(void)
{
	int ret;

	LOG_INF("CAN Bootloader Application v1.0.0");
	LOG_INF("Built: " __DATE__ " " __TIME__);

	/* Check if we're running from MCUboot */
	if (boot_is_img_confirmed()) {
		LOG_INF("Image already confirmed");
	} else {
		LOG_INF("Confirming image...");
		ret = boot_write_img_confirmed();
		if (ret) {
			LOG_ERR("Failed to confirm image: %d", ret);
		} else {
			LOG_INF("Image confirmed successfully");
		}
	}

	/* Initialize LED */
	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED device not ready");
		return -1;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure LED: %d", ret);
		return -1;
	}

	/* Initialize CAN update driver */
#if HAS_CAN_BUS
	ret = can_update_init(CAN_DEV);
	if (ret) {
		LOG_ERR("Failed to initialize CAN update: %d", ret);
		return -1;
	}
	LOG_INF("System initialized, waiting for CAN updates...");
#else
	LOG_WRN("CAN bus not available, update functionality disabled");
	LOG_INF("System initialized");
#endif

	/* Monitor for update completion and reboot if needed */
#if HAS_CAN_BUS
	enum can_update_status last_status = CAN_UPDATE_STATUS_IDLE;

	while (1) {
		enum can_update_status status = can_update_get_status();

		if (status == CAN_UPDATE_STATUS_SUCCESS && last_status != status) {
			LOG_INF("Update completed, rebooting in 5 seconds...");
			k_sleep(K_SECONDS(5));
			sys_reboot(SYS_REBOOT_COLD);
		}

		last_status = status;
		k_sleep(K_MSEC(100));
	}
#else
	/* Just blink LED without CAN functionality */
	while (1) {
		k_sleep(K_SECONDS(1));
	}
#endif

	return 0;
}
