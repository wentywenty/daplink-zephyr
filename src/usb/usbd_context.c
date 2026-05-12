/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink USB Device Context - Composite device (HID + CDC + MSC)
 */

#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/drivers/uart/uart_bridge.h>
#include <zephyr/logging/log.h>

#include "usbd_context.h"

LOG_MODULE_REGISTER(usbd_ctx, LOG_LEVEL_INF);

#define DAPLINK_USB_VID    0x0D28
#define DAPLINK_USB_PID    0x0204

USBD_DEVICE_DEFINE(daplink_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   DAPLINK_USB_VID,
		   DAPLINK_USB_PID);

USBD_DESC_LANG_DEFINE(daplink_lang);
USBD_DESC_MANUFACTURER_DEFINE(daplink_mfr, "ARM");
USBD_DESC_PRODUCT_DEFINE(daplink_product, "DAPLink CMSIS-DAP");
USBD_DESC_SERIAL_NUMBER_DEFINE(daplink_sn);

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "DAPLink FS");

/* Full-Speed configuration: self-powered, 250mA (125 * 2mA) */
USBD_CONFIGURATION_DEFINE(daplink_fs_config,
			  USB_SCD_SELF_POWERED,
			  125,
			  &fs_cfg_desc);

/* MSC LUN: RAM disk "DAPLINK" */
USBD_DEFINE_MSC_LUN(daplink, "DAPLINK", "ARM", "DAPLink", "1.00");

/* UART bridge device references for CDC line coding updates */
#define DEVICE_DT_GET_COMMA(node_id) DEVICE_DT_GET(node_id),

static const struct device *uart_bridges[] = {
	DT_FOREACH_STATUS_OKAY(zephyr_uart_bridge, DEVICE_DT_GET_COMMA)
};

static void usbd_msg_cb(struct usbd_context *const usbd_ctx,
			const struct usbd_msg *const msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (msg->type == USBD_MSG_CONFIGURATION) {
		LOG_INF("  Configuration value %d", msg->status);
	}

	/* Propagate CDC ACM line coding changes to UART bridge */
	if (msg->type == USBD_MSG_CDC_ACM_LINE_CODING ||
	    msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		for (uint8_t i = 0; i < ARRAY_SIZE(uart_bridges); i++) {
			uart_bridge_settings_update(msg->dev, uart_bridges[i]);
		}
	}
}

int daplink_usbd_init(void)
{
	int err;

	err = usbd_add_descriptor(&daplink_usbd, &daplink_lang);
	if (err) {
		LOG_ERR("Failed to add language descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&daplink_usbd, &daplink_mfr);
	if (err) {
		LOG_ERR("Failed to add manufacturer descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&daplink_usbd, &daplink_product);
	if (err) {
		LOG_ERR("Failed to add product descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&daplink_usbd, &daplink_sn);
	if (err) {
		LOG_ERR("Failed to add serial number descriptor (%d)", err);
		return err;
	}

	err = usbd_add_configuration(&daplink_usbd, USBD_SPEED_FS,
				     &daplink_fs_config);
	if (err) {
		LOG_ERR("Failed to add FS configuration (%d)", err);
		return err;
	}

	/* Auto-register all USB class instances (HID, CDC ACM, MSC) */
	err = usbd_register_all_classes(&daplink_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) {
		LOG_ERR("Failed to register USB classes (%d)", err);
		return err;
	}

	/* Composite device: use IAD class code */
	usbd_device_set_code_triple(&daplink_usbd, USBD_SPEED_FS,
				    USB_BCC_MISCELLANEOUS, 0x02, 0x01);

	err = usbd_msg_register_cb(&daplink_usbd, usbd_msg_cb);
	if (err) {
		LOG_ERR("Failed to register msg callback (%d)", err);
		return err;
	}

	usbd_self_powered(&daplink_usbd, true);

	err = usbd_init(&daplink_usbd);
	if (err) {
		LOG_ERR("Failed to init USB device (%d)", err);
		return err;
	}

	LOG_INF("USB device initialized (VID=%04X PID=%04X)",
		DAPLINK_USB_VID, DAPLINK_USB_PID);
	return 0;
}

struct usbd_context *daplink_usbd_ctx(void)
{
	return &daplink_usbd;
}
