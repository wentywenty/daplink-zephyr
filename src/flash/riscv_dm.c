/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V Debug Module (DM) implementation — stage 2.
 */

#include <zephyr/kernel.h>

#include "riscv_dm.h"

#if defined(CONFIG_DAPLINK_RISCV) && defined(CONFIG_DAPLINK_JTAG)

#include "riscv_jtag_dtm.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(riscv_dm);

/* DM register offsets from the DM base address */
#define DM_DATA0   0x04U
#define DM_CONTROL 0x10U
#define DM_STATUS  0x11U
#define DM_HARTINFO 0x12U

/* dmcontrol fields */
#define DM_CTRL_ACTIVE         (1U << 0U)
#define DM_CTRL_HARTSEL_MASK   0x03FFFFC0U
#define DM_CTRL_HARTSELLO_MASK 0x03FF0000U
#define DM_CTRL_HARTSELHI_MASK 0x0000FFC0U
#define DM_CTRL_HARTSELLO_SHIFT 16U
#define DM_CTRL_HARTSELHI_SHIFT 4U
#define DM_CTRL_HART_RESET     (1U << 29U)
#define DM_CTRL_RESUME_REQ     (1U << 30U)
#define DM_CTRL_HALT_REQ       (1U << 31U)

/* dmstatus fields */
#define DM_STAT_ALL_HALTED     (1U << 9U)
#define DM_STAT_UNAVAILABLE    (1U << 12U)
#define DM_STAT_NON_EXISTENT   (1U << 14U)
#define DM_STAT_ALL_RESUME_ACK (1U << 17U)
#define DM_STAT_ALL_RESET      (1U << 19U)

#define RISCV_MAX_HARTS 8U
#define DM_POLL_LIMIT   100000U

static uint32_t dm_base = 0U;
static uint32_t hart_count;

struct riscv_hart {
	uint32_t hartsel; /* dmcontrol value with this hart selected + active */
	uint32_t hart_idx;
};

static struct riscv_hart harts[RISCV_MAX_HARTS];

static bool dm_write(uint32_t reg, uint32_t val)
{
	return riscv_dmi_write(dm_base + reg, val);
}

static bool dm_read(uint32_t reg, uint32_t *val)
{
	return riscv_dmi_read(dm_base + reg, val);
}

static uint32_t make_hartsel(uint32_t idx)
{
	return ((idx << DM_CTRL_HARTSELLO_SHIFT) & DM_CTRL_HARTSELLO_MASK) |
	       ((idx >> 10U) << 6U) | DM_CTRL_ACTIVE;
}

static int dm_wait_status(uint32_t mask)
{
	for (uint32_t i = 0; i < DM_POLL_LIMIT; i++) {
		uint32_t status = 0;

		if (!dm_read(DM_STATUS, &status)) {
			return -EIO;
		}
		if (status & mask) {
			return 0;
		}
	}
	return -ETIMEDOUT;
}

int riscv_dm_init(void)
{
	uint32_t control;

	dm_base = 0U;
	hart_count = 0U;

	/* Activate the DM */
	if (!dm_write(DM_CONTROL, DM_CTRL_ACTIVE)) {
		return -EIO;
	}

	/* Discover how many hartsel bits are implemented */
	control = DM_CTRL_ACTIVE | DM_CTRL_HARTSEL_MASK;
	if (!dm_write(DM_CONTROL, control) || !dm_read(DM_CONTROL, &control)) {
		return -EIO;
	}
	const uint32_t harts_max =
		((control & DM_CTRL_HARTSELLO_MASK) >> DM_CTRL_HARTSELLO_SHIFT) |
		((control & DM_CTRL_HARTSELHI_MASK) << 4U);

	for (uint32_t idx = 0; idx <= harts_max && hart_count < RISCV_MAX_HARTS; idx++) {
		const uint32_t sel = make_hartsel(idx);
		uint32_t status = 0;

		if (!dm_write(DM_CONTROL, sel) || !dm_read(DM_STATUS, &status)) {
			return -EIO;
		}
		/* The spec says stop scanning at the first non-existent hart */
		if (status & DM_STAT_NON_EXISTENT) {
			break;
		}
		if (status & DM_STAT_UNAVAILABLE) {
			continue;
		}

		harts[hart_count].hartsel = sel;
		harts[hart_count].hart_idx = idx;
		hart_count++;
		LOG_INF("RISC-V hart %u available (status 0x%08X)", idx, status);
	}

	if (hart_count == 0U) {
		return -ENODEV;
	}
	LOG_INF("RISC-V DM: %u hart(s) enumerated", hart_count);
	return (int)hart_count;
}

uint32_t riscv_dm_hart_count(void)
{
	return hart_count;
}

int riscv_hart_halt(uint32_t index)
{
	if (index >= hart_count) {
		return -EINVAL;
	}
	if (!dm_write(DM_CONTROL, harts[index].hartsel | DM_CTRL_HALT_REQ)) {
		return -EIO;
	}
	return dm_wait_status(DM_STAT_ALL_HALTED);
}

int riscv_hart_resume(uint32_t index)
{
	if (index >= hart_count) {
		return -EINVAL;
	}
	if (!dm_write(DM_CONTROL, harts[index].hartsel | DM_CTRL_RESUME_REQ)) {
		return -EIO;
	}
	return dm_wait_status(DM_STAT_ALL_RESUME_ACK);
}

int riscv_hart_reset(uint32_t index)
{
	if (index >= hart_count) {
		return -EINVAL;
	}
	/* Assert then release hart reset, waiting for the reset to take effect */
	if (!dm_write(DM_CONTROL, harts[index].hartsel | DM_CTRL_HART_RESET)) {
		return -EIO;
	}
	int rc = dm_wait_status(DM_STAT_ALL_RESET);

	if (!dm_write(DM_CONTROL, harts[index].hartsel)) {
		return -EIO;
	}
	return rc;
}

/* ---- Abstract commands (stage 3) ---- */

#define DM_ABSTRACTCS   0x16U
#define DM_COMMAND      0x17U
#define ABST_CS_BUSY    (1U << 12U)
#define ABST_CS_CMDERR  0x00000700U
#define ABST_AARSIZE32  (2U << 20U)
#define ABST_WRITE      (1U << 16U)

/* Select the hart and wait for the abstract command engine to go idle. */
static bool abst_ready(uint32_t index)
{
	uint32_t cs = 0;

	if (index >= hart_count) {
		return false;
	}
	if (!dm_write(DM_CONTROL, harts[index].hartsel)) {
		return false;
	}
	for (uint32_t i = 0; i < DM_POLL_LIMIT; i++) {
		if (!dm_read(DM_ABSTRACTCS, &cs)) {
			return false;
		}
		if (!(cs & ABST_CS_BUSY)) {
			/* Clear any sticky command error and report failure if set. */
			if (cs & ABST_CS_CMDERR) {
				(void)dm_write(DM_ABSTRACTCS, cs & ABST_CS_CMDERR);
				return false;
			}
			return true;
		}
	}
	return false;
}

bool riscv_reg_read(uint32_t index, uint16_t regno, uint32_t *val)
{
	if (!abst_ready(index)) {
		return false;
	}
	if (!dm_write(DM_COMMAND, ABST_AARSIZE32 | (regno & 0xFFFFU))) {
		return false;
	}
	if (!abst_ready(index)) {
		return false;
	}
	return dm_read(DM_DATA0, val);
}

bool riscv_reg_write(uint32_t index, uint16_t regno, uint32_t val)
{
	if (!abst_ready(index)) {
		return false;
	}
	if (!dm_write(DM_DATA0, val)) {
		return false;
	}
	if (!dm_write(DM_COMMAND, ABST_AARSIZE32 | ABST_WRITE | (regno & 0xFFFFU))) {
		return false;
	}
	return abst_ready(index);
}

/* ---- System Bus Access (stage 3) ---- */

#define DM_SBCS       0x38U
#define DM_SBADDRESS0 0x39U
#define DM_SBDATA0    0x3CU
#define SBCS_ACCESS16 (1U << 17U)
#define SBCS_ACCESS32 (2U << 17U)
#define SBCS_AUTOINC  (1U << 16U)
#define SBCS_SBERROR  0x00007000U
#define SBCS_SBBUSY   (1U << 21U)

static bool sba_configured;

static bool sba_wait(void)
{
	uint32_t sbcs = 0;

	for (uint32_t i = 0; i < DM_POLL_LIMIT; i++) {
		if (!dm_read(DM_SBCS, &sbcs)) {
			return false;
		}
		if (!(sbcs & SBCS_SBBUSY)) {
			if (sbcs & SBCS_SBERROR) {
				(void)dm_write(DM_SBCS, sbcs & SBCS_SBERROR);
				return false;
			}
			return true;
		}
	}
	return false;
}

static bool sba_init(void)
{
	uint32_t sbcs = 0;

	if (sba_configured) {
		return true;
	}
	if (!dm_read(DM_SBCS, &sbcs)) {
		return false;
	}
	/* sbversion must be non-zero for a valid system bus. */
	if ((sbcs & 0x7U) == 0U) {
		return false;
	}
	sba_configured = true;
	return true;
}

/* One system bus access at the given width (SBCS_ACCESS16/32). */
static bool sba_access(uint32_t index, uint32_t addr, uint32_t width, uint32_t *data, bool write)
{
	if (index >= hart_count || !sba_init()) {
		return false;
	}
	if (!dm_write(DM_CONTROL, harts[index].hartsel)) {
		return false;
	}
	if (!sba_wait()) {
		return false;
	}
	if (!dm_write(DM_SBCS, width | SBCS_SBERROR)) {
		return false;
	}
	if (!sba_wait()) {
		return false;
	}
	if (!dm_write(DM_SBADDRESS0, addr)) {
		return false;
	}
	if (write) {
		if (!dm_write(DM_SBDATA0, *data)) {
			return false;
		}
	} else if (!dm_read(DM_SBDATA0, data)) {
		return false;
	}
	return sba_wait();
}

bool riscv_mem_read32(uint32_t index, uint32_t addr, uint32_t *val)
{
	return sba_access(index, addr, SBCS_ACCESS32, val, false);
}

bool riscv_mem_write32(uint32_t index, uint32_t addr, uint32_t val)
{
	return sba_access(index, addr, SBCS_ACCESS32, &val, true);
}

bool riscv_mem_read16(uint32_t index, uint32_t addr, uint16_t *val)
{
	uint32_t data = 0;

	if (!sba_access(index, addr, SBCS_ACCESS16, &data, false)) {
		return false;
	}
	*val = (uint16_t)data;
	return true;
}

bool riscv_mem_write16(uint32_t index, uint32_t addr, uint16_t val)
{
	uint32_t data = val;

	return sba_access(index, addr, SBCS_ACCESS16, &data, true);
}

#endif /* CONFIG_DAPLINK_RISCV && CONFIG_DAPLINK_JTAG */
