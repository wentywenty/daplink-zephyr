/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink Delay HAL - Precise timing for SWD/JTAG
 */

#ifndef DAP_DELAY_H_
#define DAP_DELAY_H_

#include <stdint.h>
#include <zephyr/kernel.h>

/**
 * Initialize delay subsystem.
 */
void dap_delay_init(void);

/**
 * Microsecond-level busy wait.
 */
static inline void delay_us(uint32_t us)
{
	k_busy_wait(us);
}

/**
 * Nanosecond-level busy wait using NOP loop.
 *
 * @param n  Number of NOP iterations
 */
static inline void delay_ns(uint32_t n)
{
	while (n--) {
		arch_nop();
	}
}

/**
 * Calibrate fast delay for target SWD clock frequency.
 *
 * @param clock_hz  Target SWD/JTAG clock frequency in Hz
 */
void dap_delay_calibrate(uint32_t clock_hz);

/**
 * Get current fast delay loop count.
 */
uint32_t dap_delay_get_fast_count(void);

#endif /* DAP_DELAY_H_ */
