/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native (register-level) STM32 flash programming.
 *
 * Programs the internal flash controller directly over SWD instead of
 * downloading and running a RAM algorithm blob. Ported from the Black Magic
 * Debug STM32F1/F4 flash drivers (src/target/stm32f1.c, stm32f4.c).
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <errno.h>

#include "flash_blob.h"
#include "swd_host.h"
#include "flash_mem.h"

/*
 * Route native flash 32-bit memory access through the backend abstraction so
 * the same drivers can run over SWD (Cortex-M) or RISC-V System Bus Access.
 */
#define swd_read_word  flash_mem_read32
#define swd_write_word flash_mem_write32
#define swd_write_halfword flash_mem_write16

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(flash_native);

/* Busy-wait bound: ~2M SWD reads is well over any real erase/program time. */
#define FLASH_POLL_LIMIT 2000000U

/*
 * STM32 DBGMCU control register (F1/F2/F4/F7). Freezing the watchdogs and
 * low-power modes prevents the target from resetting itself while the flash
 * controller is busy, which is a common cause of failed programming.
 */
#define STM32_DBGMCU_CR      0xE0042004U
#define DBGMCU_CR_FREEZE     0x00000307U /* DBG_SLEEP|DBG_STOP|DBG_STANDBY|IWDG|WWDG */

static int stm32_common_prepare(const target_cfg_t *cfg)
{
	uint32_t cr;

	(void)cfg;
	if (!swd_read_word(STM32_DBGMCU_CR, &cr)) {
		return -EIO;
	}
	if (!swd_write_word(STM32_DBGMCU_CR, cr | DBGMCU_CR_FREEZE)) {
		return -EIO;
	}
	return 0;
}

/* ====================================================================== */
/* STM32F1 (F103 and close relatives)                                     */
/* ====================================================================== */

#define F1_FPEC_BASE   0x40022000U
#define F1_FPEC_KEY    (F1_FPEC_BASE + 0x04U)
#define F1_FPEC_STATUS (F1_FPEC_BASE + 0x0CU)
#define F1_FPEC_CTRL   (F1_FPEC_BASE + 0x10U)
#define F1_FPEC_ADDR   (F1_FPEC_BASE + 0x14U)

#define F1_KEY1 0x45670123U
#define F1_KEY2 0xCDEF89ABU

#define F1_CTRL_PG   (1U << 0)
#define F1_CTRL_PER  (1U << 1)
#define F1_CTRL_MER  (1U << 2)
#define F1_CTRL_STRT (1U << 6)
#define F1_CTRL_LOCK (1U << 7)

#define F1_STATUS_BSY   (1U << 0)
#define F1_STATUS_ERROR 0x14U
#define F1_STATUS_EOP   0x20U

static int f1_wait_bsy(void)
{
	uint32_t sr = F1_STATUS_BSY;

	for (uint32_t i = 0; i < FLASH_POLL_LIMIT; i++) {
		if (!swd_read_word(F1_FPEC_STATUS, &sr)) {
			return -EIO;
		}
		if (!(sr & F1_STATUS_BSY)) {
			break;
		}
	}

	if (sr & F1_STATUS_ERROR) {
		return -EIO;
	}
	return 0;
}

static int f1_unlock(void)
{
	uint32_t cr;

	if (!swd_read_word(F1_FPEC_CTRL, &cr)) {
		return -EIO;
	}
	if (cr & F1_CTRL_LOCK) {
		if (!swd_write_word(F1_FPEC_KEY, F1_KEY1)) {
			return -EIO;
		}
		if (!swd_write_word(F1_FPEC_KEY, F1_KEY2)) {
			return -EIO;
		}
		if (!swd_read_word(F1_FPEC_CTRL, &cr)) {
			return -EIO;
		}
		if (cr & F1_CTRL_LOCK) {
			return -EACCES;
		}
	}
	return 0;
}

static int stm32f1_native_erase(const target_cfg_t *cfg, uint32_t addr)
{
	uint32_t sr;
	int rc;

	(void)cfg;
	rc = f1_unlock();
	if (rc != 0) {
		return rc;
	}

	if (!swd_read_word(F1_FPEC_STATUS, &sr)) {
		return -EIO;
	}
	swd_write_word(F1_FPEC_STATUS, sr | F1_STATUS_EOP); /* EOP is write-1-to-clear */

	if (!swd_write_word(F1_FPEC_CTRL, F1_CTRL_PER)) {
		return -EIO;
	}
	if (!swd_write_word(F1_FPEC_ADDR, addr)) {
		return -EIO;
	}
	if (!swd_write_word(F1_FPEC_CTRL, F1_CTRL_STRT | F1_CTRL_PER)) {
		return -EIO;
	}
	return f1_wait_bsy();
}

static int stm32f1_native_program(const target_cfg_t *cfg, uint32_t addr, const uint8_t *data, uint32_t size)
{
	uint32_t sr;
	int rc;

	(void)cfg;
	rc = f1_unlock();
	if (rc != 0) {
		return rc;
	}

	if (!swd_read_word(F1_FPEC_STATUS, &sr)) {
		return -EIO;
	}
	swd_write_word(F1_FPEC_STATUS, sr | F1_STATUS_EOP);

	if (!swd_write_word(F1_FPEC_CTRL, F1_CTRL_PG)) {
		return -EIO;
	}

	/* F1 flash is programmed a halfword (16 bits) at a time. */
	for (uint32_t off = 0; off < size; off += 2U) {
		uint16_t hw;

		if (off + 2U <= size) {
			hw = (uint16_t)(data[off] | ((uint16_t)data[off + 1U] << 8U));
		} else {
			/* Pad an odd trailing byte with the erased value 0xFF. */
			hw = (uint16_t)(data[off] | 0xFF00U);
		}

		if (!swd_write_halfword(addr + off, hw)) {
			return -EIO;
		}
	}
	return f1_wait_bsy();
}

static int stm32f1_native_mass_erase(const target_cfg_t *cfg)
{
	uint32_t sr;
	int rc;

	(void)cfg;
	rc = f1_unlock();
	if (rc != 0) {
		return rc;
	}

	if (!swd_read_word(F1_FPEC_STATUS, &sr)) {
		return -EIO;
	}
	swd_write_word(F1_FPEC_STATUS, sr | F1_STATUS_EOP);

	if (!swd_write_word(F1_FPEC_CTRL, F1_CTRL_MER)) {
		return -EIO;
	}
	if (!swd_write_word(F1_FPEC_CTRL, F1_CTRL_STRT | F1_CTRL_MER)) {
		return -EIO;
	}
	return f1_wait_bsy();
}

const flash_native_ops_t flash_native_stm32f1 = {
	.prepare = stm32_common_prepare,
	.erase_sector = stm32f1_native_erase,
	.program = stm32f1_native_program,
	.mass_erase = stm32f1_native_mass_erase,
};

/* ====================================================================== */
/* STM32F4                                                                */
/* ====================================================================== */

#define F4_FPEC_BASE   0x40023C00U
#define F4_FPEC_KEY    (F4_FPEC_BASE + 0x04U)
#define F4_FPEC_STATUS (F4_FPEC_BASE + 0x0CU)
#define F4_FPEC_CTRL   (F4_FPEC_BASE + 0x10U)

#define F4_KEY1 0x45670123U
#define F4_KEY2 0xCDEF89ABU

#define F4_CTRL_PG      (1U << 0)
#define F4_CTRL_SER     (1U << 1)
#define F4_CTRL_MER     (1U << 2)
#define F4_CTRL_PSIZE32 (2U << 8)
#define F4_CTRL_MER1    (1U << 15)
#define F4_CTRL_STRT    (1U << 16)
#define F4_CTRL_LOCK    (1U << 31)

#define F4_STATUS_EOP   (1U << 0)
#define F4_STATUS_BSY   (1U << 16)
#define F4_STATUS_ERROR 0x000000F2U

static int f4_wait_bsy(void)
{
	uint32_t sr = F4_STATUS_BSY;

	for (uint32_t i = 0; i < FLASH_POLL_LIMIT; i++) {
		if (!swd_read_word(F4_FPEC_STATUS, &sr)) {
			return -EIO;
		}
		if (sr & F4_STATUS_ERROR) {
			return -EIO;
		}
		if (!(sr & F4_STATUS_BSY)) {
			break;
		}
	}
	return 0;
}

static int f4_unlock(void)
{
	uint32_t cr;

	if (!swd_read_word(F4_FPEC_CTRL, &cr)) {
		return -EIO;
	}
	if (cr & F4_CTRL_LOCK) {
		if (!swd_write_word(F4_FPEC_KEY, F4_KEY1)) {
			return -EIO;
		}
		if (!swd_write_word(F4_FPEC_KEY, F4_KEY2)) {
			return -EIO;
		}
		if (!swd_read_word(F4_FPEC_CTRL, &cr)) {
			return -EIO;
		}
		if (cr & F4_CTRL_LOCK) {
			return -EACCES;
		}
	}
	return 0;
}

/*
 * Compute the sequential F4 sector number for addr.
 *
 * The F4 family uses a fixed layout per bank: 4 x 16 KiB, 1 x 64 KiB and
 * 7 x 128 KiB (12 sectors). Parts larger than 1 MiB have a second bank whose
 * layout mirrors the first, with sector numbers continuing at 12.
 */
static uint32_t f4_sector_in_bank(uint32_t off)
{
	if (off < 0x10000U) {
		return off / 0x4000U; /* sectors 0..3 */
	}
	if (off < 0x20000U) {
		return 4U; /* sector 4 */
	}
	return 5U + (off - 0x20000U) / 0x20000U; /* sectors 5..11 */
}

static uint32_t f4_sector_number(const target_cfg_t *cfg, uint32_t addr)
{
	const uint32_t base = cfg->flash_regions[0].start;
	const uint32_t len = cfg->flash_regions[0].end - base;
	const uint32_t off = addr - base;

	if (len > 0x100000U && off >= 0x100000U) {
		return 12U + f4_sector_in_bank(off - 0x100000U);
	}
	return f4_sector_in_bank(off);
}

static int stm32f4_native_erase(const target_cfg_t *cfg, uint32_t addr)
{
	const uint32_t snb = f4_sector_number(cfg, addr);
	const uint32_t ctrl = F4_CTRL_SER | F4_CTRL_PSIZE32 | ((snb & 0x1FU) << 3U);
	int rc = f4_unlock();

	if (rc != 0) {
		return rc;
	}

	/* Clear any pending EOP before starting. */
	swd_write_word(F4_FPEC_STATUS, F4_STATUS_EOP);

	if (!swd_write_word(F4_FPEC_CTRL, ctrl)) {
		return -EIO;
	}
	if (!swd_write_word(F4_FPEC_CTRL, ctrl | F4_CTRL_STRT)) {
		return -EIO;
	}
	return f4_wait_bsy();
}

static int stm32f4_native_program(const target_cfg_t *cfg, uint32_t addr, const uint8_t *data, uint32_t size)
{
	int rc;

	(void)cfg;
	rc = f4_unlock();
	if (rc != 0) {
		return rc;
	}

	swd_write_word(F4_FPEC_STATUS, F4_STATUS_EOP);

	if (!swd_write_word(F4_FPEC_CTRL, F4_CTRL_PSIZE32 | F4_CTRL_PG)) {
		return -EIO;
	}

	/* x32 parallelism: program a 32-bit word at a time. */
	for (uint32_t off = 0; off < size; off += 4U) {
		uint32_t word = 0xFFFFFFFFU;
		uint32_t n = size - off;

		if (n > 4U) {
			n = 4U;
		}
		memcpy(&word, data + off, n);

		if (!swd_write_word(addr + off, word)) {
			return -EIO;
		}
	}
	return f4_wait_bsy();
}

static int stm32f4_native_mass_erase(const target_cfg_t *cfg)
{
	const uint32_t region_len = cfg->flash_regions[0].end - cfg->flash_regions[0].start;
	uint32_t ctrl = F4_CTRL_MER | F4_CTRL_PSIZE32;
	int rc = f4_unlock();

	if (rc != 0) {
		return rc;
	}

	/* Devices larger than 1 MiB are dual-bank; erase both banks. */
	if (region_len > 0x100000U) {
		ctrl |= F4_CTRL_MER1;
	}

	swd_write_word(F4_FPEC_STATUS, F4_STATUS_EOP);

	if (!swd_write_word(F4_FPEC_CTRL, ctrl)) {
		return -EIO;
	}
	if (!swd_write_word(F4_FPEC_CTRL, ctrl | F4_CTRL_STRT)) {
		return -EIO;
	}
	return f4_wait_bsy();
}

/* ---- Option bytes (STM32F4 style) ---- */
#define F4_FPEC_OPTKEYR (F4_FPEC_BASE + 0x08U)
#define F4_FPEC_OPTCR   (F4_FPEC_BASE + 0x14U)
#define F4_FPEC_OPTCR1  (F4_FPEC_BASE + 0x18U)
#define F4_OPTKEY1      0x08192A3BU
#define F4_OPTKEY2      0x4C5D6E7FU
#define F4_OPTCR_OPTLOCK   (1U << 0)
#define F4_OPTCR_OPTSTRT   (1U << 1)
#define F4_OPTCR_OBL_LAUNCH (1U << 31)

static uint32_t f4_option_reg(uint32_t idx)
{
	return (idx == 0U) ? F4_FPEC_OPTCR : F4_FPEC_OPTCR1;
}

static int stm32f4_option_read(const target_cfg_t *cfg, uint32_t idx, uint32_t *val)
{
	(void)cfg;
	return swd_read_word(f4_option_reg(idx), val) ? 0 : -EIO;
}

static int stm32f4_option_write(const target_cfg_t *cfg, uint32_t idx, uint32_t val)
{
	const uint32_t reg = f4_option_reg(idx);
	uint32_t v;
	int rc = f4_unlock();

	(void)cfg;
	if (rc != 0) {
		return rc;
	}

	/* Unlock option bytes */
	if (!swd_write_word(F4_FPEC_OPTKEYR, F4_OPTKEY1)) {
		return -EIO;
	}
	if (!swd_write_word(F4_FPEC_OPTKEYR, F4_OPTKEY2)) {
		return -EIO;
	}

	v = val & ~(F4_OPTCR_OPTLOCK | F4_OPTCR_OPTSTRT | F4_OPTCR_OBL_LAUNCH);
	if (!swd_write_word(reg, v)) {
		return -EIO;
	}
	if (!swd_write_word(reg, v | F4_OPTCR_OPTSTRT)) {
		return -EIO;
	}
	rc = f4_wait_bsy();
	if (rc != 0) {
		return rc;
	}
	/* Reload option bytes (OBL_LAUNCH triggers a system reset). */
	if (!swd_write_word(reg, v | F4_OPTCR_OBL_LAUNCH)) {
		return -EIO;
	}
	(void)f4_wait_bsy();
	return 0;
}

const flash_native_ops_t flash_native_stm32f4 = {
	.prepare = stm32_common_prepare,
	.erase_sector = stm32f4_native_erase,
	.program = stm32f4_native_program,
	.mass_erase = stm32f4_native_mass_erase,
	.option_read = stm32f4_option_read,
	.option_write = stm32f4_option_write,
};
