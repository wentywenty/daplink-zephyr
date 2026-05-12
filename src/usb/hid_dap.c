/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * HID-DAP Adapter Implementation
 *
 * Data flow:
 *   Host HID OUT Report -> output_report() -> k_msgq -> work queue
 *   work queue -> DAP_ExecuteCommand() -> hid_device_submit_report()
 *   Host HID IN Report <-
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/logging/log.h>

#include "hid_dap.h"
#include "DAP_config.h"
#include "DAP.h"

LOG_MODULE_REGISTER(hid_dap, LOG_LEVEL_INF);

static const uint8_t hid_report_desc[] = {
	0x06, 0x00, 0xFF,    /* Usage Page (Vendor Defined 0xFF00) */
	0x09, 0x01,          /* Usage (0x01) */
	0xA1, 0x01,          /* Collection (Application) */
	/* Input report: device to host */
	0x09, 0x02,          /*   Usage (0x02) */
	0x15, 0x00,          /*   Logical Minimum (0) */
	0x26, 0xFF, 0x00,    /*   Logical Maximum (255) */
	0x75, 0x08,          /*   Report Size (8) */
	0x95, 0x40,          /*   Report Count (64) */
	0x81, 0x02,          /*   Input (Data,Var,Abs) */
	/* Output report: host to device */
	0x09, 0x03,          /*   Usage (0x03) */
	0x15, 0x00,          /*   Logical Minimum (0) */
	0x26, 0xFF, 0x00,    /*   Logical Maximum (255) */
	0x75, 0x08,          /*   Report Size (8) */
	0x95, 0x40,          /*   Report Count (64) */
	0x91, 0x02,          /*   Output (Data,Var,Abs) */
	0xC0                 /* End Collection */
};

struct dap_msg {
	uint8_t request[DAP_PACKET_SIZE];
	uint16_t req_len;
};

K_MSGQ_DEFINE(dap_cmd_msgq, sizeof(struct dap_msg), 8, 1);

static uint8_t dap_response[DAP_PACKET_SIZE];
static bool hid_ready;
static const struct device *hid_dev;
static struct k_work dap_work;

/* Mutex to protect DAP bus access */
K_MUTEX_DEFINE(dap_bus_mutex);

static void dap_work_handler(struct k_work *work)
{
	struct dap_msg msg;
	uint32_t result;

	while (k_msgq_get(&dap_cmd_msgq, &msg, K_NO_WAIT) == 0) {
		k_mutex_lock(&dap_bus_mutex, K_FOREVER);

		memset(dap_response, 0, DAP_PACKET_SIZE);
		result = DAP_ExecuteCommand(msg.request, dap_response);

		uint16_t resp_len = (uint16_t)(result & 0xFFFF);

		LOG_DBG("DAP cmd 0x%02X: req=%u resp=%u",
			msg.request[0],
			(uint16_t)(result >> 16),
			resp_len);

		k_mutex_unlock(&dap_bus_mutex);

		if (hid_ready) {
			int ret = hid_device_submit_report(hid_dev,
							   resp_len,
							   dap_response);
			if (ret != 0) {
				LOG_WRN("HID submit report failed: %d", ret);
			}
		}
	}
}

static void dap_iface_ready(const struct device *dev, const bool ready)
{
	LOG_INF("HID DAP interface %s", ready ? "ready" : "not ready");
	hid_ready = ready;
}

static int dap_get_report(const struct device *dev,
			  const uint8_t type, const uint8_t id,
			  const uint16_t len, uint8_t *const buf)
{
	return -ENOTSUP;
}

static int dap_set_report(const struct device *dev,
			  const uint8_t type, const uint8_t id,
			  const uint16_t len, const uint8_t *const buf)
{
	if (type != HID_REPORT_TYPE_OUTPUT) {
		return -ENOTSUP;
	}

	struct dap_msg msg;

	msg.req_len = (len > DAP_PACKET_SIZE) ? DAP_PACKET_SIZE : len;
	memcpy(msg.request, buf, msg.req_len);

	if (k_msgq_put(&dap_cmd_msgq, &msg, K_NO_WAIT) != 0) {
		LOG_WRN("DAP command queue full, dropping request");
		return -ENOMEM;
	}

	k_work_submit(&dap_work);
	return 0;
}

static void dap_output_report(const struct device *dev, const uint16_t len,
			      const uint8_t *const buf)
{
	dap_set_report(dev, HID_REPORT_TYPE_OUTPUT, 0, len, buf);
}

static void dap_set_protocol(const struct device *dev, const uint8_t proto)
{
	LOG_INF("HID protocol: %s", proto == 0U ? "Boot" : "Report");
}

static const struct hid_device_ops hid_dap_ops = {
	.iface_ready = dap_iface_ready,
	.get_report = dap_get_report,
	.set_report = dap_set_report,
	.output_report = dap_output_report,
	.set_protocol = dap_set_protocol,
};

int hid_dap_init(void)
{
	int ret;

	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID device not ready");
		return -ENODEV;
	}

	ret = hid_device_register(hid_dev,
				  hid_report_desc, sizeof(hid_report_desc),
				  &hid_dap_ops);
	if (ret != 0) {
		LOG_ERR("Failed to register HID device: %d", ret);
		return ret;
	}

	k_work_init(&dap_work, dap_work_handler);

	LOG_INF("HID-DAP adapter initialized");
	return 0;
}

bool hid_dap_is_ready(void)
{
	return hid_ready;
}
