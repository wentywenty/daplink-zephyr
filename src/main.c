/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink Zephyr Port - CMSIS-DAP Debug Probe
 *
 * Phase 1: HID debugging interface
 * Phase 2: CDC ACM serial bridge
 * Phase 3: MSC drag-and-drop flash programming
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "dap_gpio.h"
#include "dap_delay.h"
#include "DAP.h"
#include "usbd_context.h"
#include "hid_dap.h"
#include "vfs_disk.h"

LOG_MODULE_REGISTER(daplink, LOG_LEVEL_INF);

int main(void)
{
	int ret;

	LOG_INF("DAPLink Zephyr starting...");

	/* Step 1: Initialize HAL */
	ret = dap_gpio_init();
	if (ret != 0) {
		LOG_ERR("GPIO init failed: %d", ret);
		return ret;
	}

	dap_delay_init();

	/* Step 2: Initialize DAP core */
	DAP_Setup();
	LOG_INF("CMSIS-DAP initialized");

	/* Step 3: Initialize VFS disk (must be before USB) */
	ret = vfs_disk_init();
	if (ret != 0) {
		LOG_ERR("VFS disk init failed: %d", ret);
		return ret;
	}

	/* Step 4: Initialize HID-DAP adapter */
	ret = hid_dap_init();
	if (ret != 0) {
		LOG_ERR("HID-DAP init failed: %d", ret);
		return ret;
	}

	/* Step 5: Initialize USB device */
	ret = daplink_usbd_init();
	if (ret != 0) {
		LOG_ERR("USB device init failed: %d", ret);
		return ret;
	}

	/* Step 6: Enable USB */
	struct usbd_context *usbd = daplink_usbd_ctx();
	if (!usbd_can_detect_vbus(usbd)) {
		ret = usbd_enable(usbd);
		if (ret != 0) {
			LOG_ERR("USB enable failed: %d", ret);
			return ret;
		}
	}

	LOG_INF("DAPLink ready - HID + CDC + MSC active");

	while (true) {
		k_sleep(K_MSEC(1000));
	}

	return 0;
}
