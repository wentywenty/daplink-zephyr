/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink USB Device Context - Device descriptor and initialization
 */

#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>

#include "usbd_context.h"
#include "hid_dap.h"

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

static void usbd_msg_cb(struct usbd_context *const usbd_ctx,
			const struct usbd_msg *const msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (msg->type == USBD_MSG_CONFIGURATION) {
		LOG_INF("  Configuration value %d", msg->status);
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

	err = usbd_register_class(&daplink_usbd, "hid_dap", 1);
	if (err) {
		LOG_ERR("Failed to register HID DAP class (%d)", err);
		return err;
	}

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
