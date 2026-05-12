/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink Zephyr Port - CMSIS-DAP Debug Probe
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(daplink, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("DAPLink Zephyr starting...");

	while (true) {
		k_sleep(K_MSEC(1000));
	}

	return 0;
}
