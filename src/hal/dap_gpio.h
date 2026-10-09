/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink GPIO HAL - SWD/JTAG pin control via Zephyr GPIO API
 */

#ifndef DAP_GPIO_H_
#define DAP_GPIO_H_

#include <zephyr/drivers/gpio.h>

/*
 * SWD pin device specifiers, initialized from device tree.
 * Used by DAP_config.h inline functions.
 */
extern const struct gpio_dt_spec swclk_spec;
extern const struct gpio_dt_spec swdio_spec;
extern const struct gpio_dt_spec swdio_in_spec;
extern const struct gpio_dt_spec nreset_spec;

/* JTAG-only pins (TCK/TMS reuse swclk/swdio) */
extern const struct gpio_dt_spec tdi_spec;
extern const struct gpio_dt_spec tdo_spec;

/* Indicates whether a separate SWDIO input pin is configured */
extern bool swdio_in_present;

/* Indicates whether JTAG (TDI/TDO) pins are configured */
extern bool jtag_present;

/**
 * Initialize SWD/JTAG GPIO pins from device tree configuration.
 *
 * @return 0 on success, negative errno on failure
 */
int dap_gpio_init(void);

#endif /* DAP_GPIO_H_ */
