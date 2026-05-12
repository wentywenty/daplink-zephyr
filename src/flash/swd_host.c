/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SWD Host Interface - High-level SWD operations.
 * Uses SWD_Transfer() from DAP core for register-level access.
 * Ported from DAPLink source/daplink/interface/swd_host.c
 */

#include <zephyr/kernel.h>
#include "swd_host.h"
#include "DAP_config.h"
#include "DAP.h"
#include "flash_blob.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(swd_host);

/*
 * SWD register access macros.
 * Encode APnDP, RnW, and address bits into SWD_Transfer request format.
 *   Bit 0: APnDP (0=DP, 1=AP)
 *   Bit 1: RnW   (0=Write, 1=Read)
 *   Bits 2-3: Address bits [2:3]
 */
#define SWD_REG_DP(addr)  ((addr & 0x0CU))
#define SWD_REG_AP(addr)  ((1U << 0) | (addr & 0x0CU))
#define SWD_REG_W         (0U << 1)
#define SWD_REG_R         (1U << 1)

/* AP CSW flags for memory access */
#define CSW_RESERVED      (0U << 24)
#define CSW_HPROT         (1U << 25)
#define CSW_MSTRDBG       (1U << 29)
#define CSW_DBGSTAT       (1U << 31)
#define CSW_SADDRINC      (1U << 4)
#define CSW_SIZE32        (2U << 0)

#define CSW_VALUE         (CSW_RESERVED | CSW_MSTRDBG | CSW_HPROT | \
			   CSW_DBGSTAT | CSW_SADDRINC)

/* CoreSight debug register addresses */
#define DHCSR             0xE000EDF0U
#define DCRSR             0xE000EDF4U
#define DCRDR             0xE000EDF8U
#define DEMCR             0xE000EDFCU

/* DHCSR bits */
#define DHCSR_DBGKEY      (0xA05FU << 16)
#define DHCSR_C_DEBUGEN   (1U << 0)
#define DHCSR_C_HALT      (1U << 1)
#define DHCSR_S_HALT      (1U << 17)
#define DHCSR_S_REGRDY    (1U << 16)

/* DCRSR bits */
#define DCRSR_REGWnR      (1U << 16)

/* DEMCR bits */
#define DEMCR_VC_CORERESET (1U << 0)

/* DP CTRL/STAT bits */
#define CTRLSTAT_CSYSPWRUPACK (1U << 31)
#define CTRLSTAT_CDBGPWRUPACK (1U << 29)
#define CTRLSTAT_CSYSPWRUPREQ (1U << 30)
#define CTRLSTAT_CDBGPWRUPREQ (1U << 28)

/* Timeout for polling operations */
#define TIMEOUT_MS      2000U

/*
 * Perform a SWD read transaction.
 * Returns true on success, false on SWD protocol error.
 */
static bool swd_transfer(uint32_t request, uint32_t *data)
{
	uint8_t result = SWD_Transfer(request, data);
	return (result == DAP_TRANSFER_OK);
}

/*
 * Read a DP register.
 */
static bool swd_read_dp(uint32_t addr, uint32_t *val)
{
	return swd_transfer(SWD_REG_DP(addr) | SWD_REG_R, val);
}

/*
 * Write a DP register.
 */
static bool swd_write_dp(uint32_t addr, uint32_t val)
{
	return swd_transfer(SWD_REG_DP(addr) | SWD_REG_W, &val);
}

/*
 * Read an AP register (requires read buffer flush).
 */
static bool swd_read_ap(uint32_t addr, uint32_t *val)
{
	if (!swd_transfer(SWD_REG_AP(addr) | SWD_REG_R, val)) {
		return false;
	}
	/* AP reads are returned in the next transaction (RDBUFF) */
	return swd_read_dp(DP_RDBUFF, val);
}

/*
 * Write an AP register.
 */
static bool swd_write_ap(uint32_t addr, uint32_t val)
{
	return swd_transfer(SWD_REG_AP(addr) | SWD_REG_W, &val);
}

int swd_init_debug(void)
{
	uint32_t val;
	uint32_t retries = 100;

	/*
	 * SWJ-DP Switch from JTAG to SWD (if applicable).
	 * Send the JTAG-to-SWD switching sequence: >50 cycles of 1,
	 * then 0xE79E (16 bits), then >50 cycles of 1.
	 */
	{
		uint8_t swj_seq[18];
		int idx = 0;

		/* >50 cycles of 1 (7 bytes of 0xFF) */
		for (int i = 0; i < 7; i++) {
			swj_seq[idx++] = 0xFF;
		}

		/* 0xE79E, LSB first: 0x9E, 0xE7 */
		swj_seq[idx++] = 0x9E;
		swj_seq[idx++] = 0xE7;

		/* >50 cycles of 1 (7 bytes of 0xFF) */
		for (int i = 0; i < 7; i++) {
			swj_seq[idx++] = 0xFF;
		}

		/* Send the first 51+16+51 = 118+ bits */
		SWJ_Sequence(51, swj_seq);
		SWJ_Sequence(16, &swj_seq[7]);
		SWJ_Sequence(51, &swj_seq[9]);
	}

	/* Read DP IDCODE to verify connection */
	if (!swd_read_dp(DP_IDCODE, &val)) {
		LOG_ERR("SWD: failed to read DP IDCODE");
		return -EIO;
	}
	LOG_INF("SWD: DP IDCODE = 0x%08X", val);

	/* Clear any sticky errors by writing to DP ABORT */
	val = (1U << 1) | (1U << 2) | (1U << 3); /* WDERRCL | STKERRCL | STKCMPCL */
	swd_write_dp(DP_ABORT, val);

	/* Power up debug and system */
	val = CTRLSTAT_CDBGPWRUPREQ | CTRLSTAT_CSYSPWRUPREQ;
	if (!swd_write_dp(DP_CTRL_STAT, val)) {
		LOG_ERR("SWD: failed to write DP CTRL/STAT");
		return -EIO;
	}

	/* Wait for power-up acknowledgment */
	do {
		if (!swd_read_dp(DP_CTRL_STAT, &val)) {
			LOG_ERR("SWD: failed to read DP CTRL/STAT");
			return -EIO;
		}
		if ((val & (CTRLSTAT_CDBGPWRUPACK | CTRLSTAT_CSYSPWRUPACK)) ==
		    (CTRLSTAT_CDBGPWRUPACK | CTRLSTAT_CSYSPWRUPACK)) {
			break;
		}
		retries--;
	} while (retries > 0);

	if (retries == 0) {
		LOG_ERR("SWD: power-up timeout (CTRL/STAT=0x%08X)", val);
		return -ETIMEDOUT;
	}

	/* Select AP bank 0 */
	swd_write_dp(DP_SELECT, 0x00);

	/* Enable debug (halt on reset, etc.) */
	val = DHCSR_DBGKEY | DHCSR_C_DEBUGEN | DHCSR_C_HALT;
	if (!swd_write_word(DHCSR, val)) {
		LOG_ERR("SWD: failed to halt target");
		return -EIO;
	}

	/* Enable vector catch for reset */
	swd_write_word(DEMCR, DEMCR_VC_CORERESET);

	LOG_INF("SWD: debug connection established");
	return 0;
}

void swd_off(void)
{
	/* Release target: clear debug enable */
	swd_write_word(DHCSR, DHCSR_DBGKEY);
	swd_write_word(DEMCR, 0);

	/* Power down debug */
	swd_write_dp(DP_CTRL_STAT, 0);

	PORT_OFF();
}

bool swd_read_word(uint32_t addr, uint32_t *val)
{
	/* Set AP CSW for 32-bit access with auto-increment */
	if (!swd_write_ap(0x00, CSW_VALUE | CSW_SIZE32)) {
		return false;
	}
	/* Set target address */
	if (!swd_write_ap(0x04, addr)) {
		return false;
	}
	/* Read data from AP DRW */
	return swd_read_ap(0x0C, val);
}

bool swd_write_word(uint32_t addr, uint32_t val)
{
	/* Set AP CSW for 32-bit access with auto-increment */
	if (!swd_write_ap(0x00, CSW_VALUE | CSW_SIZE32)) {
		return false;
	}
	/* Set target address */
	if (!swd_write_ap(0x04, addr)) {
		return false;
	}
	/* Write data to AP DRW */
	return swd_write_ap(0x0C, val);
}

bool swd_read_memory(uint32_t addr, uint8_t *data, uint32_t size)
{
	uint32_t val;
	uint32_t offset = 0;

	/* Set AP CSW for 32-bit access with auto-increment */
	if (!swd_write_ap(0x00, CSW_VALUE | CSW_SIZE32)) {
		return false;
	}

	/* Set initial address */
	if (!swd_write_ap(0x04, addr)) {
		return false;
	}

	/* Read word by word */
	while (offset < size) {
		if (!swd_read_ap(0x0C, &val)) {
			LOG_ERR("SWD: read memory failed at 0x%08X", addr + offset);
			return false;
		}
		/* Copy bytes from word to output buffer */
		uint32_t remaining = size - offset;
		if (remaining >= 4) {
			data[offset + 0] = (uint8_t)(val >> 0);
			data[offset + 1] = (uint8_t)(val >> 8);
			data[offset + 2] = (uint8_t)(val >> 16);
			data[offset + 3] = (uint8_t)(val >> 24);
			offset += 4;
		} else {
			for (uint32_t i = 0; i < remaining; i++) {
				data[offset + i] = (uint8_t)(val >> (i * 8));
			}
			offset = size;
		}
	}

	return true;
}

bool swd_write_memory(uint32_t addr, const uint8_t *data, uint32_t size)
{
	uint32_t offset = 0;

	/* Set AP CSW for 32-bit access with auto-increment */
	if (!swd_write_ap(0x00, CSW_VALUE | CSW_SIZE32)) {
		return false;
	}

	/* Set initial address */
	if (!swd_write_ap(0x04, addr)) {
		return false;
	}

	/* Write word by word */
	while (offset < size) {
		uint32_t val = 0;
		uint32_t remaining = size - offset;

		if (remaining >= 4) {
			val = (uint32_t)data[offset + 0] |
			      ((uint32_t)data[offset + 1] << 8) |
			      ((uint32_t)data[offset + 2] << 16) |
			      ((uint32_t)data[offset + 3] << 24);
			offset += 4;
		} else {
			for (uint32_t i = 0; i < remaining; i++) {
				val |= (uint32_t)data[offset + i] << (i * 8);
			}
			offset = size;
		}

		if (!swd_write_ap(0x0C, val)) {
			LOG_ERR("SWD: write memory failed at 0x%08X", addr + offset);
			return false;
		}
	}

	return true;
}

bool swd_read_core_register(uint32_t n, uint32_t *val)
{
	uint32_t dhcsr;
	int32_t timeout = TIMEOUT_MS;

	/* Write register number to DCRSR, indicating read */
	if (!swd_write_word(DCRSR, n)) {
		return false;
	}

	/* Poll DHCSR for S_REGRDY */
	do {
		if (!swd_read_word(DHCSR, &dhcsr)) {
			return false;
		}
		if (dhcsr & DHCSR_S_REGRDY) {
			break;
		}
		k_msleep(1);
		timeout--;
	} while (timeout > 0);

	if (timeout <= 0) {
		LOG_ERR("SWD: core register read timeout (reg %u)", n);
		return false;
	}

	/* Read value from DCRDR */
	return swd_read_word(DCRDR, val);
}

bool swd_write_core_register(uint32_t n, uint32_t val)
{
	uint32_t dhcsr;
	int32_t timeout = TIMEOUT_MS;

	/* Write value to DCRDR first */
	if (!swd_write_word(DCRDR, val)) {
		return false;
	}

	/* Write register number to DCRSR, indicating write */
	if (!swd_write_word(DCRSR, n | DCRSR_REGWnR)) {
		return false;
	}

	/* Poll DHCSR for S_REGRDY */
	do {
		if (!swd_read_word(DHCSR, &dhcsr)) {
			return false;
		}
		if (dhcsr & DHCSR_S_REGRDY) {
			break;
		}
		k_msleep(1);
		timeout--;
	} while (timeout > 0);

	if (timeout <= 0) {
		LOG_ERR("SWD: core register write timeout (reg %u)", n);
		return false;
	}

	return true;
}

bool swd_flash_syscall_exec(const program_syscall_t *sys,
			    uint32_t entry, uint32_t arg1, uint32_t arg2)
{
	uint32_t dhcsr;
	int32_t timeout = TIMEOUT_MS;

	/* Set up target registers for algorithm call:
	 *   R0  = arg1
	 *   R1  = arg2
	 *   R9  = static base (R SB)
	 *   R13 = stack pointer (R SP)
	 *   R15 = entry point (PC)
	 *   xPSR = 0x01000000 (Thumb bit set)
	 */

	/* Write arguments */
	if (!swd_write_core_register(0, arg1)) {       /* R0 = arg1 */
		return false;
	}
	if (!swd_write_core_register(1, arg2)) {       /* R1 = arg2 */
		return false;
	}
	if (!swd_write_core_register(9, sys->static_base)) {  /* R9 = SB */
		return false;
	}
	if (!swd_write_core_register(13, sys->stack_pointer)) { /* R13 = SP */
		return false;
	}
	if (!swd_write_core_register(15, entry)) {     /* R15 = PC */
		return false;
	}
	if (!swd_write_core_register(16, 0x01000000)) { /* xPSR = Thumb */
		return false;
	}

	/* Resume target execution */
	if (!swd_write_word(DHCSR, DHCSR_DBGKEY | DHCSR_C_DEBUGEN)) {
		return false;
	}

	/* Wait for halt at breakpoint */
	do {
		if (!swd_read_word(DHCSR, &dhcsr)) {
			return false;
		}
		if ((dhcsr & DHCSR_S_HALT) &&
		    (dhcsr & DHCSR_C_DEBUGEN)) {
			break;
		}
		k_msleep(1);
		timeout--;
	} while (timeout > 0);

	if (timeout <= 0) {
		LOG_ERR("SWD: syscall exec timeout (entry=0x%08X)", entry);
		/* Try to halt the target */
		swd_write_word(DHCSR, DHCSR_DBGKEY | DHCSR_C_DEBUGEN | DHCSR_C_HALT);
		return false;
	}

	/* Check return value in R0 (0 = success) */
	uint32_t r0;

	if (!swd_read_core_register(0, &r0)) {
		return false;
	}

	if (r0 != 0) {
		LOG_WRN("SWD: syscall returned 0x%08X (entry=0x%08X)", r0, entry);
		return false;
	}

	return true;
}

bool swd_set_target_state(uint8_t state)
{
	uint32_t val;

	switch (state) {
	case TARGET_RESET_HOLD:
		/* Assert nRESET */
		PIN_nRESET_OUT(0);
		k_msleep(1);
		return true;

	case TARGET_RESET_RUN:
		/* De-assert nRESET and let target run */
		PIN_nRESET_OUT(1);
		k_msleep(1);
		return true;

	case TARGET_RESET_HALT:
		/* Halt target then reset, keeping debug enabled */
		swd_write_word(DHCSR, DHCSR_DBGKEY | DHCSR_C_DEBUGEN | DHCSR_C_HALT);
		swd_write_word(DEMCR, DEMCR_VC_CORERESET);

		/* Assert and release reset */
		PIN_nRESET_OUT(0);
		k_msleep(1);
		PIN_nRESET_OUT(1);
		k_msleep(1);

		/* Wait for halt */
		int32_t timeout = TIMEOUT_MS;

		do {
			if (!swd_read_word(DHCSR, &val)) {
				return false;
			}
			if (val & DHCSR_S_HALT) {
				break;
			}
			k_msleep(1);
			timeout--;
		} while (timeout > 0);

		if (timeout <= 0) {
			LOG_ERR("SWD: reset-halt timeout");
			return false;
		}

		/* Clear vector catch */
		swd_write_word(DEMCR, 0);
		return true;

	case TARGET_NO_RESET:
		return true;

	default:
		return false;
	}
}
