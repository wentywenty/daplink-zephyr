/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native (register-level) STM32L4 flash programming.
 *
 * Programs the L4 flash controller directly over SWD. Ported from the Black
 * Magic Debug STM32L4 driver (src/target/stm32l4.c).
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <errno.h>

#include "flash_blob.h"
#include "swd_host.h"
#include "flash_mem.h"

#define swd_read_word  flash_mem_read32
#define swd_write_word flash_mem_write32

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(flash_native_l4);

#define L4_POLL_LIMIT 2000000U

#define L4_FPEC_BASE   0x40022000U
#define L4_FPEC_KEY    (L4_FPEC_BASE + 0x08U)
#define L4_FPEC_STATUS (L4_FPEC_BASE + 0x10U)
#define L4_FPEC_CTRL   (L4_FPEC_BASE + 0x14U)
#define L4_FPEC_OPTR   (L4_FPEC_BASE + 0x20U)

#define L4_KEY1 0x45670123U
#define L4_KEY2 0xCDEF89ABU

#define L4_CTRL_PG       (1U << 0)
#define L4_CTRL_PER      (1U << 1)
#define L4_CTRL_MER1     (1U << 2)
#define L4_CTRL_PAGE_SHIFT 3U
#define L4_CTRL_BKER     (1U << 11)
#define L4_CTRL_MER2     (1U << 15)
#define L4_CTRL_STRT     (1U << 16)
#define L4_CTRL_LOCK     (1U << 31)

#define L4_STATUS_BUSY   (1U << 16)
#define L4_STATUS_ERR    0x0000C3FAU

#define L4_PAGE_SIZE     0x800U /* 2 KiB */

static int l4_wait(void)
{
	uint32_t sr = 0;

	for (uint32_t i = 0; i < L4_POLL_LIMIT; i++) {
		if (!swd_read_word(L4_FPEC_STATUS, &sr)) {
			return -EIO;
		}
		if (sr & L4_STATUS_ERR) {
			return -EIO;
		}
		if (!(sr & L4_STATUS_BUSY)) {
			return 0;
		}
	}
	return -ETIMEDOUT;
}

static int l4_unlock(void)
{
	uint32_t cr;

	if (!swd_read_word(L4_FPEC_CTRL, &cr)) {
		return -EIO;
	}
	if (cr & L4_CTRL_LOCK) {
		if (!swd_write_word(L4_FPEC_KEY, L4_KEY1)) {
			return -EIO;
		}
		if (!swd_write_word(L4_FPEC_KEY, L4_KEY2)) {
			return -EIO;
		}
		if (!swd_read_word(L4_FPEC_CTRL, &cr)) {
			return -EIO;
		}
		if (cr & L4_CTRL_LOCK) {
			return -EACCES;
		}
	}
	return 0;
}

static int l4_clear_status(void)
{
	uint32_t sr;

	if (!swd_read_word(L4_FPEC_STATUS, &sr)) {
		return -EIO;
	}
	/* All error flags are write-1-to-clear. */
	if (!swd_write_word(L4_FPEC_STATUS, sr)) {
		return -EIO;
	}
	return 0;
}

static int stm32l4_native_erase(const target_cfg_t *cfg, uint32_t addr)
{
	const uint32_t base = cfg->flash_regions[0].start;
	const uint32_t len = cfg->flash_regions[0].end - base;
	uint32_t optr = 0;
	uint32_t page;
	uint32_t ctrl;
	int rc = l4_unlock();

	if (rc != 0) {
		return rc;
	}
	rc = l4_clear_status();
	if (rc != 0) {
		return rc;
	}

	/* DBANK (FLASH_OPTR bit 21) selects dual-bank page numbering. */
	bool dual = false;

	if (swd_read_word(L4_FPEC_OPTR, &optr)) {
		dual = ((optr >> 21U) & 1U) != 0U;
	}

	ctrl = L4_CTRL_PER;
	if (dual && addr >= base + len / 2U) {
		page = (addr - (base + len / 2U)) / L4_PAGE_SIZE;
		ctrl |= L4_CTRL_BKER;
	} else {
		page = (addr - base) / L4_PAGE_SIZE;
	}
	ctrl |= (page & 0xFFU) << L4_CTRL_PAGE_SHIFT;

	if (!swd_write_word(L4_FPEC_CTRL, ctrl)) {
		return -EIO;
	}
	if (!swd_write_word(L4_FPEC_CTRL, ctrl | L4_CTRL_STRT)) {
		return -EIO;
	}
	return l4_wait();
}

static int stm32l4_native_program(const target_cfg_t *cfg, uint32_t addr, const uint8_t *data, uint32_t size)
{
	int rc;

	(void)cfg;
	rc = l4_unlock();
	if (rc != 0) {
		return rc;
	}
	rc = l4_clear_status();
	if (rc != 0) {
		return rc;
	}

	if (!swd_write_word(L4_FPEC_CTRL, L4_CTRL_PG)) {
		return -EIO;
	}

	/* L4 programs in 64-bit double words; issue two 32-bit words per step. */
	for (uint32_t off = 0; off < size; off += 8U) {
		uint32_t w0 = 0xFFFFFFFFU;
		uint32_t w1 = 0xFFFFFFFFU;
		uint32_t n0 = size - off;
		uint32_t n1 = (size > off + 4U) ? size - off - 4U : 0U;

		if (n0 > 4U) {
			n0 = 4U;
		}
		if (n1 > 4U) {
			n1 = 4U;
		}
		memcpy(&w0, data + off, n0);
		if (n1 != 0U) {
			memcpy(&w1, data + off + 4U, n1);
		}

		if (!swd_write_word(addr + off, w0)) {
			return -EIO;
		}
		if (!swd_write_word(addr + off + 4U, w1)) {
			return -EIO;
		}
	}
	return l4_wait();
}

static int stm32l4_native_mass_erase(const target_cfg_t *cfg)
{
	uint32_t ctrl = L4_CTRL_MER1 | L4_CTRL_MER2;
	int rc;

	(void)cfg;
	rc = l4_unlock();
	if (rc != 0) {
		return rc;
	}
	rc = l4_clear_status();
	if (rc != 0) {
		return rc;
	}

	if (!swd_write_word(L4_FPEC_CTRL, ctrl)) {
		return -EIO;
	}
	if (!swd_write_word(L4_FPEC_CTRL, ctrl | L4_CTRL_STRT)) {
		return -EIO;
	}
	return l4_wait();
}

const flash_native_ops_t flash_native_stm32l4 = {
	.prepare = NULL,
	.erase_sector = stm32l4_native_erase,
	.program = stm32l4_native_program,
	.mass_erase = stm32l4_native_mass_erase,
};
