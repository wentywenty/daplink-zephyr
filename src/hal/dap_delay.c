/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink Delay HAL implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/arch_interface.h>
#include <zephyr/logging/log.h>
#include "dap_delay.h"

LOG_MODULE_REGISTER(dap_delay, LOG_LEVEL_DBG);

static uint32_t fast_delay_count;

void dap_delay_init(void)
{
	dap_delay_calibrate(1000000);
	LOG_INF("Delay subsystem initialized");
}

void dap_delay_calibrate(uint32_t clock_hz)
{
	uint32_t cpu_hz = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;

	if (clock_hz == 0) {
		clock_hz = 1;
	}

	/*
	 * Each SWD clock cycle needs two half-periods (SET + CLR).
	 * Half-period = 1 / (2 * clock_hz) seconds.
	 * NOP loop takes ~3 CPU cycles per iteration (NOP + branch + counter dec).
	 */
	uint32_t cycles_per_loop = 3;
	uint32_t half_period_cycles = cpu_hz / (2 * clock_hz);
	fast_delay_count = half_period_cycles / cycles_per_loop;

	if (fast_delay_count == 0) {
		fast_delay_count = 1;
	}

	LOG_DBG("SWD clock calibration: %u Hz, delay count: %u",
		clock_hz, fast_delay_count);
}

uint32_t dap_delay_get_fast_count(void)
{
	return fast_delay_count;
}
