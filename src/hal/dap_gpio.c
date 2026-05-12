/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink GPIO HAL implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "dap_gpio.h"

LOG_MODULE_REGISTER(dap_gpio, LOG_LEVEL_DBG);

/*
 * Device tree specifiers for SWD pins.
 * Uses DT_NODELABEL for direct node access.
 * The board overlay must define: daplink: daplink { compatible = "daplink,gpio"; ... };
 */
#define DAPLINK_GPIO_NODE DT_NODELABEL(daplink)

#if DT_NODE_HAS_STATUS(DAPLINK_GPIO_NODE, okay)
const struct gpio_dt_spec swclk_spec =
	GPIO_DT_SPEC_GET(DAPLINK_GPIO_NODE, swclk_gpios);
const struct gpio_dt_spec swdio_spec =
	GPIO_DT_SPEC_GET(DAPLINK_GPIO_NODE, swdio_gpios);

#define HAS_SWDIO_IN DT_NODE_HAS_PROP(DAPLINK_GPIO_NODE, swdio_in_gpios)
const struct gpio_dt_spec swdio_in_spec =
	COND_CODE_1(HAS_SWDIO_IN,
		(GPIO_DT_SPEC_GET(DAPLINK_GPIO_NODE, swdio_in_gpios)),
		({0}));
const struct gpio_dt_spec nreset_spec =
	GPIO_DT_SPEC_GET_OR(DAPLINK_GPIO_NODE, nreset_gpios, {0});
bool swdio_in_present = HAS_SWDIO_IN;

#else
/* Fallback: no device tree node */
const struct gpio_dt_spec swclk_spec = {0};
const struct gpio_dt_spec swdio_spec = {0};
const struct gpio_dt_spec swdio_in_spec = {0};
const struct gpio_dt_spec nreset_spec = {0};
bool swdio_in_present = false;
#endif

int dap_gpio_init(void)
{
	int ret;

	/* SWCLK: push-pull output, initially low */
	if (swclk_spec.port != NULL) {
		if (!gpio_is_ready_dt(&swclk_spec)) {
			LOG_ERR("SWCLK GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&swclk_spec, GPIO_OUTPUT_LOW);
		if (ret != 0) {
			LOG_ERR("Failed to configure SWCLK: %d", ret);
			return ret;
		}
	}

	/* SWDIO: push-pull output, initially low */
	if (swdio_spec.port != NULL) {
		if (!gpio_is_ready_dt(&swdio_spec)) {
			LOG_ERR("SWDIO GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&swdio_spec, GPIO_OUTPUT_LOW);
		if (ret != 0) {
			LOG_ERR("Failed to configure SWDIO: %d", ret);
			return ret;
		}
	}

	/* SWDIO input (optional separate pin) */
	if (swdio_in_present && swdio_in_spec.port != NULL) {
		if (!gpio_is_ready_dt(&swdio_in_spec)) {
			LOG_ERR("SWDIO-IN GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&swdio_in_spec, GPIO_INPUT);
		if (ret != 0) {
			LOG_ERR("Failed to configure SWDIO-IN: %d", ret);
			return ret;
		}
	}

	/* nRESET: open-drain output with pull-up, initially high (inactive) */
	if (nreset_spec.port != NULL) {
		if (!gpio_is_ready_dt(&nreset_spec)) {
			LOG_ERR("nRESET GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&nreset_spec,
			GPIO_OUTPUT_INACTIVE | GPIO_OPEN_DRAIN);
		if (ret != 0) {
			ret = gpio_pin_configure_dt(&nreset_spec,
				GPIO_OUTPUT_INACTIVE);
			if (ret != 0) {
				LOG_ERR("Failed to configure nRESET: %d", ret);
				return ret;
			}
		}
	}

	LOG_INF("DAP GPIO initialized");
	return 0;
}
