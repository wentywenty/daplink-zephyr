/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flash programming manager.
 * Coordinates SWD connection, algorithm download, erase, and programming.
 */

#include <zephyr/kernel.h>
#include "flash_manager.h"
#include "swd_host.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(flash_mgr);

/* Current target state */
static const target_cfg_t *active_target;
static const program_target_t *active_algo;
static bool initialized;

uint32_t flash_sector_size(const target_cfg_t *target, uint32_t addr)
{
	const sector_info_t *info = target->sectors_info;
	uint32_t count = target->sector_info_length;

	/* Walk sectors from highest to lowest address to find match */
	for (int i = (int)count - 1; i >= 0; i--) {
		if (addr >= info[i].start) {
			return info[i].size;
		}
	}
	return 0;
}

/*
 * Check if address is within a valid flash region.
 */
static bool addr_in_flash(const target_cfg_t *target, uint32_t addr)
{
	for (int i = 0; i < TARGET_MAX_FLASH_REGIONS; i++) {
		if (target->flash_regions[i].flash_algo == NULL) {
			continue;
		}
		if (addr >= target->flash_regions[i].start &&
		    addr < target->flash_regions[i].end) {
			return true;
		}
	}
	return false;
}

int flash_manager_init(const target_cfg_t *target)
{
	int rc;

	if (target == NULL) {
		return -EINVAL;
	}

	active_target = target;

	/* Find default flash region */
	active_algo = NULL;
	for (int i = 0; i < TARGET_MAX_FLASH_REGIONS; i++) {
		if (target->flash_regions[i].flash_algo != NULL) {
			active_algo = target->flash_regions[i].flash_algo;
			break;
		}
	}
	if (active_algo == NULL) {
		LOG_ERR("No flash algorithm found in target config");
		return -ENODEV;
	}

	/* Initialize SWD debug connection */
	rc = swd_init_debug();
	if (rc != 0) {
		LOG_ERR("SWD init failed: %d", rc);
		return rc;
	}

	/* Download flash algorithm blob to target RAM */
	LOG_INF("Downloading flash algo to 0x%08X (%u bytes)",
		active_algo->algo_start, active_algo->algo_size);

	if (!swd_write_memory(active_algo->algo_start,
			      (const uint8_t *)active_algo->algo_blob,
			      active_algo->algo_size)) {
		LOG_ERR("Failed to download flash algorithm");
		return -EIO;
	}

	/* Call flash algorithm Init function */
	LOG_INF("Calling flash Init at 0x%08X", active_algo->init);
	if (!swd_flash_syscall_exec(&active_algo->sys_call_s,
				    active_algo->init, 0, 0)) {
		LOG_ERR("Flash algorithm Init failed");
		return -EIO;
	}

	initialized = true;
	LOG_INF("Flash manager initialized for %s %s",
		target->target_vendor ? target->target_vendor : "",
		target->target_part_number ? target->target_part_number : "");
	return 0;
}

int flash_manager_write(uint32_t addr, const uint8_t *data, uint32_t size)
{
	if (!initialized || active_algo == NULL) {
		return -EPERM;
	}

	if (!addr_in_flash(active_target, addr)) {
		LOG_ERR("Address 0x%08X not in flash region", addr);
		return -EINVAL;
	}

	uint32_t page_size = active_algo->program_buffer_size;
	uint32_t offset = 0;

	LOG_INF("Programming %u bytes at 0x%08X", size, addr);

	while (offset < size) {
		uint32_t current_addr = addr + offset;
		uint32_t remaining = size - offset;
		uint32_t chunk_size = (remaining > page_size) ? page_size : remaining;

		/* Check if we need to erase the sector */
		uint32_t sec_size = flash_sector_size(active_target, current_addr);
		uint32_t sector_start = (current_addr / sec_size) * sec_size;

		/* Erase sector if we're at the start of one */
		if (current_addr == sector_start) {
			LOG_INF("Erasing sector at 0x%08X", current_addr);
			if (!swd_flash_syscall_exec(&active_algo->sys_call_s,
						    active_algo->erase_sector,
						    current_addr, 0)) {
				LOG_ERR("Erase sector failed at 0x%08X", current_addr);
				return -EIO;
			}
		}

		/* Write data chunk to target RAM buffer */
		if (!swd_write_memory(active_algo->program_buffer,
				      &data[offset], chunk_size)) {
			LOG_ERR("Write to RAM buffer failed");
			return -EIO;
		}

		/* Call ProgramPage */
		if (!swd_flash_syscall_exec(&active_algo->sys_call_s,
					    active_algo->program_page,
					    current_addr, chunk_size)) {
			LOG_ERR("Program page failed at 0x%08X", current_addr);
			return -EIO;
		}

		offset += chunk_size;
	}

	LOG_INF("Programming complete");
	return 0;
}

int flash_manager_finish(void)
{
	if (!initialized || active_algo == NULL) {
		return -EPERM;
	}

	/* Call flash algorithm UnInit */
	LOG_INF("Calling flash UnInit");
	if (!swd_flash_syscall_exec(&active_algo->sys_call_s,
				    active_algo->uninit, 0, 0)) {
		LOG_WRN("Flash UnInit failed");
	}

	/* Reset target and let it run */
	swd_set_target_state(TARGET_RESET_RUN);

	initialized = false;
	active_target = NULL;
	active_algo = NULL;

	LOG_INF("Flash manager finished");
	return 0;
}

const target_cfg_t *flash_manager_get_target(void)
{
	return active_target;
}
