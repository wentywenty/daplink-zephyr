/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flash programming manager.
 * Coordinates SWD connection, algorithm download, erase, and programming.
 */

#include <zephyr/kernel.h>
#include <string.h>
#include "flash_manager.h"
#include "swd_host.h"
#include "flash_mem.h"
#include "target_detect.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(flash_mgr);

/* Current target state */
static const target_cfg_t *active_target;
static const program_target_t *active_algo;
static const flash_native_ops_t *active_native;
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
		if (target->flash_regions[i].flash_algo == NULL && target->native_ops == NULL) {
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
	active_native = target->native_ops;

	/* Find default flash region */
	active_algo = NULL;
	for (int i = 0; i < TARGET_MAX_FLASH_REGIONS; i++) {
		if (target->flash_regions[i].flash_algo != NULL || target->native_ops != NULL) {
			active_algo = target->flash_regions[i].flash_algo;
			break;
		}
	}
	if (active_algo == NULL && active_native == NULL) {
		LOG_ERR("No flash programming backend found in target config");
		return -ENODEV;
	}

	/* Initialize SWD debug connection */
	rc = swd_init_debug();
	if (rc != 0) {
		LOG_ERR("SWD init failed: %d", rc);
		return rc;
	}

	if (active_native != NULL) {
		/* Register-level backend: freeze watchdogs, nothing to download. */
		LOG_INF("Using native register-level flash backend");
		if (active_native->prepare != NULL &&
		    active_native->prepare(active_target) != 0) {
			LOG_WRN("Target prepare (watchdog freeze) failed");
		}
	} else {
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

	uint32_t page_size = (active_algo != NULL) ? active_algo->program_buffer_size : 256U;
	uint32_t offset = 0;

	LOG_INF("Programming %u bytes at 0x%08X", size, addr);

	while (offset < size) {
		uint32_t current_addr = addr + offset;
		uint32_t remaining = size - offset;
		uint32_t chunk_size = (remaining > page_size) ? page_size : remaining;

		/* Look up the sector geometry for this address */
		uint32_t sec_size = flash_sector_size(active_target, current_addr);

		if (sec_size == 0) {
			LOG_ERR("No sector info for address 0x%08X", current_addr);
			return -EINVAL;
		}

		uint32_t sector_start = current_addr - (current_addr % sec_size);
		uint32_t sector_end = sector_start + sec_size;

		/* Never program across a sector boundary so erase bookkeeping stays valid */
		if (current_addr + chunk_size > sector_end) {
			chunk_size = sector_end - current_addr;
		}

		/* Erase the sector the first time we reach its start address */
		if (current_addr == sector_start) {
			LOG_INF("Erasing sector at 0x%08X", current_addr);
			if (active_native != NULL) {
				if (active_native->erase_sector(active_target, current_addr) != 0) {
					LOG_ERR("Native erase failed at 0x%08X", current_addr);
					return -EIO;
				}
			} else if (!swd_flash_syscall_exec(&active_algo->sys_call_s,
							   active_algo->erase_sector,
							   current_addr, 0)) {
				LOG_ERR("Erase sector failed at 0x%08X", current_addr);
				return -EIO;
			}
		} else if (offset == 0) {
			LOG_WRN("Write starts mid-sector at 0x%08X (sector 0x%08X); "
				"that sector is not erased",
				current_addr, sector_start);
		}

		if (chunk_size == 0) {
			LOG_ERR("Zero-length programming chunk at 0x%08X", current_addr);
			return -EIO;
		}

		if (active_native != NULL) {
			/* Register-level backend programs straight from the source buffer */
			if (active_native->program(active_target, current_addr,
						   &data[offset], chunk_size) != 0) {
				LOG_ERR("Native program failed at 0x%08X", current_addr);
				return -EIO;
			}
		} else {
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
		}

		offset += chunk_size;
	}

	LOG_INF("Programming complete");
	return 0;
}

int flash_manager_verify(uint32_t addr, const uint8_t *data, uint32_t size)
{
	uint8_t buf[256];
	uint32_t offset = 0;

	while (offset < size) {
		uint32_t chunk = size - offset;

		if (chunk > sizeof(buf)) {
			chunk = sizeof(buf);
		}
		if (!flash_mem_read(addr + offset, buf, chunk)) {
			LOG_ERR("Verify read failed at 0x%08X", addr + offset);
			return -EIO;
		}
		if (memcmp(buf, &data[offset], chunk) != 0) {
			for (uint32_t i = 0; i < chunk; i++) {
				if (buf[i] != data[offset + i]) {
					LOG_ERR("Verify mismatch at 0x%08X (got 0x%02X, want 0x%02X)",
						addr + offset + i, buf[i], data[offset + i]);
					break;
				}
			}
			return -EIO;
		}
		offset += chunk;
	}

	LOG_INF("Verify OK: %u bytes at 0x%08X", size, addr);
	return 0;
}

/* Resolve a target that supports option-byte access, detecting if needed. */
static const target_cfg_t *option_target(void)
{
	const target_cfg_t *t = active_target;

	if (t != NULL && t->native_ops != NULL && t->native_ops->option_write != NULL) {
		return t;
	}
	t = target_detect();
	if (t != NULL && t->native_ops != NULL && t->native_ops->option_write != NULL) {
		return t;
	}
	return NULL;
}

int flash_manager_option_read(uint32_t idx, uint32_t *val)
{
	const target_cfg_t *t = option_target();

	if (t == NULL || t->native_ops->option_read == NULL) {
		return -ENOTSUP;
	}
	return t->native_ops->option_read(t, idx, val);
}

int flash_manager_option_write(uint32_t idx, uint32_t val)
{
	const target_cfg_t *t = option_target();

	if (t == NULL) {
		return -ENOTSUP;
	}
	return t->native_ops->option_write(t, idx, val);
}

int flash_manager_finish(void)
{
	if (!initialized) {
		return -EPERM;
	}

	if (active_native == NULL && active_algo != NULL) {
		/* Call flash algorithm UnInit */
		LOG_INF("Calling flash UnInit");
		if (!swd_flash_syscall_exec(&active_algo->sys_call_s,
					    active_algo->uninit, 0, 0)) {
			LOG_WRN("Flash UnInit failed");
		}
	}

	/* Reset target and let it run */
	swd_set_target_state(TARGET_RESET_RUN);

	initialized = false;
	active_target = NULL;
	active_algo = NULL;
	active_native = NULL;

	LOG_INF("Flash manager finished");
	return 0;
}

const target_cfg_t *flash_manager_get_target(void)
{
	return active_target;
}
