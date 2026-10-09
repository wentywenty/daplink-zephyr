/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V ADI (MEM-AP) DTM implementation (stage 5).
 */

#include <zephyr/kernel.h>

#include "riscv_adi_dtm.h"

#if defined(CONFIG_DAPLINK_RISCV)

#include "riscv_jtag_dtm.h"
#include "swd_host.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(riscv_adi_dtm);

/* The DMI is memory mapped at (address * 4) in the AP's address space. */
static bool riscv_adi_dmi_read(uint32_t address, uint32_t *value)
{
	return swd_read_word(address << 2U, value);
}

static bool riscv_adi_dmi_write(uint32_t address, uint32_t value)
{
	return swd_write_word(address << 2U, value);
}

int riscv_adi_dtm_init(void)
{
	/* Bring up SWD and select the (default) access port. */
	if (swd_init_debug() != 0) {
		return -EIO;
	}
	riscv_dmi_set_backend(riscv_adi_dmi_read, riscv_adi_dmi_write);
	LOG_INF("RISC-V: using ADI (MEM-AP) DTM over SWD");
	return 0;
}

#endif /* CONFIG_DAPLINK_RISCV */
