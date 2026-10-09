/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V JTAG DTM / DMI implementation (stage 1).
 * See riscv_jtag_dtm.h. Modeled on blackmagic riscv_jtag_dtm.c.
 */

#include <zephyr/kernel.h>

#include "riscv_jtag_dtm.h"

#if defined(CONFIG_DAPLINK_RISCV) && defined(CONFIG_DAPLINK_JTAG)

#include "DAP_config.h"
#include "DAP.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(riscv_jtag_dtm);

/* RISC-V JTAG DTM instruction register values (5-bit IR). */
#define IR_IDCODE 0x01U
#define IR_DTMCS  0x10U
#define IR_DMI    0x11U
#define IR_LENGTH 5U

/* DTMCS fields */
#define DTMCS_VERSION_MASK  0x0000000FU
#define DTMCS_ABITS_MASK    0x000003F0U
#define DTMCS_ABITS_SHIFT   4U
#define DTMCS_IDLE_MASK     0x00007000U
#define DTMCS_IDLE_SHIFT    12U
#define DTMCS_DMI_RESET     0x00010000U

/* DMI operation codes */
#define DMI_OP_NOOP  0U
#define DMI_OP_READ  1U
#define DMI_OP_WRITE 2U

/* DMI status (returned in the low 2 bits of the previous transfer) */
#define DMI_STATUS_SUCCESS 0U

static uint32_t dmi_abits = 7U;
static uint32_t dmi_idle_cycles;
static bool dmi_ready;
static uint32_t jtag_idcode;

static bool riscv_jtag_dmi_read(uint32_t addr, uint32_t *val);
static bool riscv_jtag_dmi_write(uint32_t addr, uint32_t val);

/* Backend dispatch so ADI/gateway DTMs can install their own accessors. */
static riscv_dmi_read_fn dmi_read_fn;
static riscv_dmi_write_fn dmi_write_fn;

void riscv_dmi_set_backend(riscv_dmi_read_fn read_fn, riscv_dmi_write_fn write_fn)
{
	dmi_read_fn = read_fn;
	dmi_write_fn = write_fn;
}

bool riscv_dmi_read(uint32_t addr, uint32_t *val)
{
	return dmi_read_fn ? dmi_read_fn(addr, val) : false;
}

bool riscv_dmi_write(uint32_t addr, uint32_t val)
{
	return dmi_write_fn ? dmi_write_fn(addr, val) : false;
}

/* Emit 'count' TMS bits (LSB first) with TDI low. */
static void jtag_tms(uint32_t bits, uint32_t count)
{
	uint8_t zero = 0U;

	for (uint32_t i = 0; i < count; i++) {
		const uint32_t tms = (bits >> i) & 1U;
		JTAG_Sequence(1U | (tms ? JTAG_SEQUENCE_TMS : 0U), &zero, NULL);
	}
}

static void tap_reset(void)
{
	jtag_tms(0x1FU, 5U); /* 5 x TMS=1 -> Test-Logic-Reset */
	jtag_tms(0x0U, 1U);  /* TMS=0 -> Run-Test/Idle */
}

static void tap_to_shift_ir(void)
{
	jtag_tms(0x3U, 2U); /* Select-DR-Scan, Select-IR-Scan */
	jtag_tms(0x0U, 2U); /* Capture-IR, Shift-IR */
}

static void tap_to_shift_dr(void)
{
	jtag_tms(0x1U, 1U); /* Select-DR-Scan */
	jtag_tms(0x0U, 2U); /* Capture-DR, Shift-DR */
}

static void tap_exit_to_idle(void)
{
	jtag_tms(0x3U, 2U);                 /* Exit1, Update */
	jtag_tms(0x0U, 1U + dmi_idle_cycles); /* Idle (+ DTM idle cycles) */
}

static void jtag_shift_ir(uint32_t ir)
{
	uint8_t tdi[1] = { (uint8_t)ir };

	tap_to_shift_ir();
	JTAG_Sequence(IR_LENGTH, tdi, NULL);
	tap_exit_to_idle();
}

static void jtag_shift_dr(const uint8_t *tdi, uint8_t *tdo, uint32_t bits)
{
	tap_to_shift_dr();
	JTAG_Sequence(bits | JTAG_SEQUENCE_TDO, tdi, tdo);
	tap_exit_to_idle();
}

static uint32_t riscv_shift_dtmcs(uint32_t control)
{
	uint8_t tdi[4] = {
		(uint8_t)control, (uint8_t)(control >> 8),
		(uint8_t)(control >> 16), (uint8_t)(control >> 24)
	};
	uint8_t tdo[4] = { 0 };

	jtag_shift_ir(IR_DTMCS);
	jtag_shift_dr(tdi, tdo, 32U);
	return (uint32_t)tdo[0] | ((uint32_t)tdo[1] << 8) |
	       ((uint32_t)tdo[2] << 16) | ((uint32_t)tdo[3] << 24);
}

/* Shift one DMI transaction: op(2) | data(32) | addr(abits). */
static uint8_t riscv_shift_dmi(uint8_t op, uint32_t addr, uint32_t data_in, uint32_t *data_out)
{
	uint64_t tdi_val = (uint64_t)(op & 0x3U) |
			   ((uint64_t)data_in << 2U) |
			   ((uint64_t)addr << 34U);
	uint8_t tdi[8];
	uint8_t tdo[8] = { 0 };
	const uint32_t bits = 2U + 32U + dmi_abits;

	for (uint32_t i = 0; i < sizeof(tdi); i++) {
		tdi[i] = (uint8_t)(tdi_val >> (8U * i));
	}

	jtag_shift_dr(tdi, tdo, bits);

	uint64_t tdo_val = 0;
	for (uint32_t i = 0; i < sizeof(tdo); i++) {
		tdo_val |= (uint64_t)tdo[i] << (8U * i);
	}
	if (data_out != NULL) {
		*data_out = (uint32_t)((tdo_val >> 2U) & 0xFFFFFFFFU);
	}
	return (uint8_t)(tdo_val & 0x3U);
}

uint32_t riscv_jtag_idcode(void)
{
	return jtag_idcode;
}

int riscv_jtag_dtm_init(void)
{
	uint32_t dtmcs;

	DAP_Data.debug_port = DAP_PORT_JTAG;
	PORT_JTAG_SETUP();
	tap_reset();

	/* After a TAP reset the IR defaults to IDCODE, so read it here. */
	jtag_idcode = JTAG_ReadIDCode();

	dtmcs = riscv_shift_dtmcs(0U);
	dmi_abits = (dtmcs & DTMCS_ABITS_MASK) >> DTMCS_ABITS_SHIFT;
	if (dmi_abits == 0U || dmi_abits > 7U) {
		dmi_abits = 7U;
	}
	dmi_idle_cycles = (dtmcs & DTMCS_IDLE_MASK) >> DTMCS_IDLE_SHIFT;

	LOG_INF("RISC-V DTMCS 0x%08X (ver %u, abits %u, idle %u)", dtmcs,
		dtmcs & DTMCS_VERSION_MASK, dmi_abits, dmi_idle_cycles);

	/* Reset DMI and switch the TAP into DMI access for speed. */
	(void)riscv_shift_dtmcs(DTMCS_DMI_RESET);
	jtag_shift_ir(IR_DMI);

	/* Install the JTAG DTM as the active DMI backend. */
	riscv_dmi_set_backend(riscv_jtag_dmi_read, riscv_jtag_dmi_write);

	dmi_ready = true;
	return 0;
}

static bool riscv_jtag_dmi_read(uint32_t addr, uint32_t *val)
{
	if (!dmi_ready) {
		return false;
	}
	if (riscv_shift_dmi(DMI_OP_READ, addr, 0U, NULL) != DMI_STATUS_SUCCESS) {
		return false;
	}
	return riscv_shift_dmi(DMI_OP_NOOP, 0U, 0U, val) == DMI_STATUS_SUCCESS;
}

static bool riscv_jtag_dmi_write(uint32_t addr, uint32_t val)
{
	if (!dmi_ready) {
		return false;
	}
	if (riscv_shift_dmi(DMI_OP_WRITE, addr, val, NULL) != DMI_STATUS_SUCCESS) {
		return false;
	}
	return riscv_shift_dmi(DMI_OP_NOOP, 0U, 0U, NULL) == DMI_STATUS_SUCCESS;
}

#endif /* CONFIG_DAPLINK_RISCV && CONFIG_DAPLINK_JTAG */
