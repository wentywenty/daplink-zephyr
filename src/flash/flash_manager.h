/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flash programming manager for drag-and-drop firmware update.
 * Coordinates SWD connection, flash algorithm download, and page programming.
 */

#ifndef FLASH_MANAGER_H_
#define FLASH_MANAGER_H_

#include "flash_blob.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize flash manager and connect to target via SWD.
 * Downloads flash algorithm to target RAM and calls Init.
 *
 * @param target  Target chip configuration (e.g., &target_stm32f4)
 * @return 0 on success, negative errno on failure
 */
int flash_manager_init(const target_cfg_t *target);

/**
 * Program a block of data to target flash.
 * Handles sector erase and page programming automatically.
 * Target must be connected (flash_manager_init called).
 *
 * @param addr  Target flash address (must be within flash region)
 * @param data  Binary data to program
 * @param size  Number of bytes
 * @return 0 on success, negative errno on failure
 */
int flash_manager_write(uint32_t addr, const uint8_t *data, uint32_t size);

/**
 * Finalize flash programming.
 * Calls UnInit on the flash algorithm and resets target.
 *
 * @return 0 on success, negative errno on failure
 */
int flash_manager_finish(void);

/**
 * Get the currently active target configuration.
 * Returns NULL if not initialized.
 */
const target_cfg_t *flash_manager_get_target(void);

/**
 * Look up sector size for a given flash address.
 *
 * @param target  Target configuration
 * @param addr    Flash address to look up
 * @return Sector size in bytes, or 0 if address not in flash
 */
uint32_t flash_sector_size(const target_cfg_t *target, uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_MANAGER_H_ */
