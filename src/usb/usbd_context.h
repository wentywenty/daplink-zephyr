/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink USB Device Context
 */

#ifndef USBD_CONTEXT_H_
#define USBD_CONTEXT_H_

#include <zephyr/usb/usbd.h>

/**
 * Initialize and register the DAPLink USB device.
 *
 * @return 0 on success, negative errno on failure
 */
int daplink_usbd_init(void);

/**
 * Get the DAPLink USB device context pointer.
 */
struct usbd_context *daplink_usbd_ctx(void);

#endif /* USBD_CONTEXT_H_ */
