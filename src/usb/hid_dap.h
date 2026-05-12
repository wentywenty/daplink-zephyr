/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * HID-DAP Adapter - Bridges USB HID and CMSIS-DAP command processing
 */

#ifndef HID_DAP_H_
#define HID_DAP_H_

#include <zephyr/device.h>
#include <stdbool.h>

int hid_dap_init(void);
bool hid_dap_is_ready(void);

#endif /* HID_DAP_H_ */
