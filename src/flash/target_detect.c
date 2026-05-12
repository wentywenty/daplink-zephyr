/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Target chip auto-detection via SWD.
 *
 * Detection strategy:
 *   1. Read DP IDCODE to identify ARM core type
 *   2. Try reading STM32 DBGMCU_IDCODE at two possible addresses:
 *      - 0xE0042000 (F1/F2/F4/F7)
 *      - 0x40015800 (F0/F3/L0/L4)
 *   3. Match DEV_ID (bits 11:0) against known chip table
 *   4. Fall back to DP IDCODE for non-STM32 (nRF52, LPC)
 */

#include <zephyr/kernel.h>
#include "target_detect.h"
#include "swd_host.h"

/* Target headers */
#include "target_stm32f1.h"
#include "target_stm32f4.h"
#include "target_stm32f0.h"
#include "target_stm32f3.h"
#include "target_stm32f7.h"
#include "target_stm32l0.h"
#include "target_stm32l4.h"
#include "target_nrf52.h"
#include "target_lpc1768.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(target_detect);

/* STM32 DBGMCU_IDCODE register addresses */
#define STM32_DBGMCU_IDCODE_ADDR_1  0xE0042000U  /* F1/F2/F4/F7 */
#define STM32_DBGMCU_IDCODE_ADDR_2  0x40015800U  /* F0/F3/L0/L4 */

/* DEV_ID mask (bits 11:0) */
#define STM32_DEV_ID_MASK           0x0FFFU

/* Known DP IDCODE values */
#define DP_IDCODE_CORTEX_M3_M4      0x2BA01477U  /* nRF52, some STM32F4 */
#define DP_IDCODE_CORTEX_M0_M0P     0x0BC11477U  /* STM32F0/F3/L0/L4 */
#define DP_IDCODE_CORTEX_M3         0x1BA01477U  /* STM32F103 */

/*
 * STM32 DEV_ID to target mapping.
 * DEV_ID is the lower 12 bits of DBGMCU_IDCODE.
 */
typedef struct {
	uint32_t dev_id;
	const target_cfg_t *target;
	const char *name;
} stm32_dev_entry_t;

static const stm32_dev_entry_t stm32_dev_table[] = {
	/* STM32F1 */
	{ 0x414, &target_stm32f1,  "STM32F103" },
	/* STM32F4 family */
	{ 0x413, &target_stm32f4,  "STM32F407" },
	{ 0x419, &target_stm32f4,  "STM32F429" },
	{ 0x423, &target_stm32f4,  "STM32F401" },
	{ 0x431, &target_stm32f4,  "STM32F411" },
	/* STM32F0 */
	{ 0x445, &target_stm32f0,  "STM32F072" },
	/* STM32F3 */
	{ 0x438, &target_stm32f3,  "STM32F334" },
	/* STM32F7 */
	{ 0x449, &target_stm32f7,  "STM32F746" },
	/* STM32L4 */
	{ 0x415, &target_stm32l4,  "STM32L476" },
	/* STM32L0 */
	{ 0x407, &target_stm32l0,  "STM32L082" },
};

/*
 * Try to read STM32 DBGMCU_IDCODE at the given address.
 * Returns DEV_ID (lower 12 bits) on success, or 0xFFFFFFFF on failure.
 */
static uint32_t try_read_stm32_dev_id(uint32_t addr)
{
	uint32_t idcode;

	if (swd_read_word(addr, &idcode)) {
		return idcode & STM32_DEV_ID_MASK;
	}
	return 0xFFFFFFFF;
}

/*
 * Look up STM32 target by DEV_ID.
 */
static const target_cfg_t *lookup_stm32(uint32_t dev_id)
{
	for (int i = 0; i < (int)(sizeof(stm32_dev_table) / sizeof(stm32_dev_table[0])); i++) {
		if (stm32_dev_table[i].dev_id == dev_id) {
			return stm32_dev_table[i].target;
		}
	}
	return NULL;
}

const target_cfg_t *target_detect(void)
{
	uint32_t dp_idcode;
	uint32_t dev_id;
	const target_cfg_t *target;
	int rc;

	/* Initialize SWD debug connection */
	rc = swd_init_debug();
	if (rc != 0) {
		LOG_ERR("Target detection: SWD init failed: %d", rc);
		return NULL;
	}

	/* Read DP IDCODE */
	if (!swd_read_word(0x00, &dp_idcode)) {
		LOG_ERR("Target detection: failed to read DP IDCODE");
		return NULL;
	}
	LOG_INF("DP IDCODE: 0x%08X", dp_idcode);

	/* === Strategy 1: Try STM32 DBGMCU_IDCODE at primary address (F1/F2/F4/F7) === */
	dev_id = try_read_stm32_dev_id(STM32_DBGMCU_IDCODE_ADDR_1);
	if (dev_id != 0xFFFFFFFF && dev_id != 0) {
		target = lookup_stm32(dev_id);
		if (target != NULL) {
			LOG_INF("Detected: %s (DEV_ID=0x%03X via DBGMCU @0x%08X)",
				target->target_part_number, dev_id,
				STM32_DBGMCU_IDCODE_ADDR_1);
			return target;
		}
		LOG_WRN("Unknown STM32 DEV_ID 0x%03X at 0x%08X",
			dev_id, STM32_DBGMCU_IDCODE_ADDR_1);
	}

	/* === Strategy 2: Try STM32 DBGMCU_IDCODE at alternate address (F0/F3/L0/L4) === */
	dev_id = try_read_stm32_dev_id(STM32_DBGMCU_IDCODE_ADDR_2);
	if (dev_id != 0xFFFFFFFF && dev_id != 0) {
		target = lookup_stm32(dev_id);
		if (target != NULL) {
			LOG_INF("Detected: %s (DEV_ID=0x%03X via DBGMCU @0x%08X)",
				target->target_part_number, dev_id,
				STM32_DBGMCU_IDCODE_ADDR_2);
			return target;
		}
		LOG_WRN("Unknown STM32 DEV_ID 0x%03X at 0x%08X",
			dev_id, STM32_DBGMCU_IDCODE_ADDR_2);
	}

	/* === Strategy 3: Fall back to DP IDCODE for non-STM32 devices ===
	 *
	 * nRF52832/52840: DP IDCODE = 0x2BA01477
	 * LPC1768: DP IDCODE = 0x4BA00477
	 *
	 * Note: Some STM32F4 also have DP IDCODE = 0x2BA01477,
	 * but those should have been caught by STM32 DBGMCU above.
	 */
	if (dp_idcode == DP_IDCODE_CORTEX_M3_M4) {
		LOG_INF("Detected: nRF52832 (by DP IDCODE)");
		return &target_nrf52832;
	}

	if (dp_idcode == 0x4BA00477U) {
		LOG_INF("Detected: LPC1768 (by DP IDCODE)");
		return &target_lpc1768;
	}

	LOG_ERR("Target detection failed: DP IDCODE=0x%08X", dp_idcode);
	return NULL;
}
