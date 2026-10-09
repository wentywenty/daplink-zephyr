/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Target auto-detection.
 *
 * Brings up SWD, reads the CoreSight DP IDCODE, then walks the probe registry
 * (target_probe.h). Each vendor probe is offered the DP IDCODE and returns a
 * target_cfg_t if it recognises the part. Uncompiled probes are NULL (weak
 * symbols) and skipped. Add a new target by implementing a <name>_probe() and
 * adding it to `probes[]`.
 */

#include <zephyr/kernel.h>

#include "target_detect.h"
#include "target_probe.h"
#include "swd_host.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(target_detect);

static const target_probe_fn probes[] = {
	stm32_probe, /* STM32 family (DBGMCU DEV_ID) */
	nrf_probe,   /* Nordic nRF */
	lpc_probe,   /* NXP LPC */
	riscv_probe, /* RISC-V (JTAG / ADI / gateway DTM) */
};

const target_cfg_t *target_detect(void)
{
	uint32_t dp_idcode;

	/* Initialize SWD debug connection */
	if (swd_init_debug() != 0) {
		LOG_ERR("Target detection: SWD init failed");
		return NULL;
	}

	/* Read DP IDCODE */
	if (!swd_read_word(0x00, &dp_idcode)) {
		LOG_ERR("Target detection: failed to read DP IDCODE");
		return NULL;
	}
	LOG_INF("DP IDCODE: 0x%08X", dp_idcode);

	for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
		if (probes[i] == NULL) {
			continue; /* Driver not built in */
		}
		const target_cfg_t *target = probes[i](dp_idcode);

		if (target != NULL) {
			LOG_INF("Detected: %s",
				target->target_part_number ? target->target_part_number : "?");
			return target;
		}
	}

	LOG_ERR("Target detection failed: DP IDCODE=0x%08X", dp_idcode);
	return NULL;
}
