/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V detection and target integration.
 *
 * Brings up a DTM (JTAG / ADI / gateway), the Debug Module and the system
 * bus, then dispatches to the vendor probe registry (riscv_probe.h). Full
 * per-chip support lives in the vendor probe files (e.g. target_riscv32.c).
 */

#include <zephyr/kernel.h>

#include "target_riscv.h"

#if defined(CONFIG_DAPLINK_RISCV)

#include "riscv_jtag_dtm.h"
#include "riscv_adi_dtm.h"
#include "riscv_gateway_dtm.h"
#include "riscv_dm.h"
#include "riscv_probe.h"
#include "flash_mem.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(target_riscv);

#define DM_DMSTATUS 0x11U
#define RV_CSR_MISA 0x301U

/* Vendor probes; uncompiled ones are NULL (weak) and skipped. */
static const riscv_target_probe_fn riscv_vendor_probes[] = {
	gd32vf1_probe,
};

/*
 * With a DTM active, bring up the Debug Module, halt hart 0 and switch the
 * flash memory backend to the system bus, then ask each vendor probe.
 */
static const target_cfg_t *riscv_identify(void)
{
	uint32_t status = 0;
	uint32_t misa = 0;

	if (riscv_dm_init() <= 0) {
		return NULL;
	}

	(void)riscv_hart_halt(0U);
	(void)riscv_reg_read(0U, RV_CSR_MISA, &misa);
	(void)riscv_dmi_read(DM_DMSTATUS, &status);
	LOG_INF("RISC-V: DMSTATUS 0x%08X misa 0x%08X", status, misa);

	/* Route flash memory access through the RISC-V system bus. */
	flash_mem_set_backend_sba(true, 0U);

	for (size_t i = 0; i < sizeof(riscv_vendor_probes) / sizeof(riscv_vendor_probes[0]); i++) {
		if (riscv_vendor_probes[i] == NULL) {
			continue;
		}
		const target_cfg_t *target = riscv_vendor_probes[i]();

		if (target != NULL) {
			return target;
		}
	}

	flash_mem_set_backend_sba(false, 0U);
	return NULL;
}

const target_cfg_t *riscv_detect(void)
{
	/* 1. Standard JTAG DTM. */
	if (riscv_jtag_dtm_init() == 0) {
		LOG_INF("RISC-V: JTAG IDCODE 0x%08X", riscv_jtag_idcode());
		const target_cfg_t *target = riscv_identify();

		if (target != NULL) {
			return target;
		}
	}

	/* 2. ADI (MEM-AP) DTM over SWD. */
	if (riscv_adi_dtm_init() == 0) {
		const target_cfg_t *target = riscv_identify();

		if (target != NULL) {
			return target;
		}
	}

	/* 3. Gateway DTM (custom AP exposing DMI_ADDR/DMI_DATA) over SWD. */
	if (riscv_gateway_dtm_init() == 0) {
		const target_cfg_t *target = riscv_identify();

		if (target != NULL) {
			return target;
		}
	}

	return NULL;
}

const target_cfg_t *riscv_probe(uint32_t dp_idcode)
{
	(void)dp_idcode;
	return riscv_detect();
}

#else

const target_cfg_t *riscv_detect(void)
{
	return NULL;
}

const target_cfg_t *riscv_probe(uint32_t dp_idcode)
{
	(void)dp_idcode;
	return NULL;
}

#endif
