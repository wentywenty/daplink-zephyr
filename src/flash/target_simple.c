/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Simple DP-IDCODE based target probes (nRF, LPC).
 */

#include <zephyr/kernel.h>

#include "target_probe.h"
#include "target_nrf52.h"
#include "target_lpc1768.h"

#define DP_IDCODE_CORTEX_M3_M4 0x2BA01477U /* nRF52, some STM32F4 */
#define DP_IDCODE_LPC1768      0x4BA00477U

const target_cfg_t *nrf_probe(uint32_t dp_idcode)
{
	if (dp_idcode == DP_IDCODE_CORTEX_M3_M4) {
		return &target_nrf52832;
	}
	return NULL;
}

const target_cfg_t *lpc_probe(uint32_t dp_idcode)
{
	if (dp_idcode == DP_IDCODE_LPC1768) {
		return &target_lpc1768;
	}
	return NULL;
}
