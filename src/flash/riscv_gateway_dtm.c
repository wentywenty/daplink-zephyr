/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V gateway DTM implementation (stage 5b).
 */

#include <zephyr/kernel.h>

#include "riscv_gateway_dtm.h"

#if defined(CONFIG_DAPLINK_RISCV)

#include "riscv_jtag_dtm.h"
#include "swd_host.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(riscv_gateway_dtm);

#define GW_AP_IDR_REG  0x00U
#define GW_AP_IDR_VALUE 0x74726976U /* ASCII "triv" */
#define GW_DMI_ADDR    0x04U
#define GW_DMI_DATA    0x08U
#define GW_ADDR_MAX    0x7FU /* 7-bit DMI word address */

static uint32_t gw_cached_addr;
static bool gw_cached_valid;

static void gw_select(uint32_t address)
{
	if (!gw_cached_valid || gw_cached_addr != address) {
		(void)swd_write_ap_reg(GW_DMI_ADDR, address);
		gw_cached_addr = address;
		gw_cached_valid = true;
	}
}

static bool gw_dmi_read(uint32_t address, uint32_t *value)
{
	if (address > GW_ADDR_MAX) {
		gw_cached_valid = false;
		return false;
	}
	gw_select(address);
	if (!swd_read_ap_reg(GW_DMI_DATA, value)) {
		gw_cached_valid = false;
		return false;
	}
	return true;
}

static bool gw_dmi_write(uint32_t address, uint32_t value)
{
	if (address > GW_ADDR_MAX) {
		gw_cached_valid = false;
		return false;
	}
	gw_select(address);
	if (!swd_write_ap_reg(GW_DMI_DATA, value)) {
		gw_cached_valid = false;
		return false;
	}
	return true;
}

int riscv_gateway_dtm_init(void)
{
	uint32_t idr = 0;

	if (swd_init_debug() != 0) {
		return -EIO;
	}
	if (!swd_read_ap_reg(GW_AP_IDR_REG, &idr) || idr != GW_AP_IDR_VALUE) {
		return -ENODEV;
	}

	gw_cached_valid = false;
	riscv_dmi_set_backend(gw_dmi_read, gw_dmi_write);
	LOG_INF("RISC-V: using gateway DTM (AP IDR 0x%08X)", idr);
	return 0;
}

#endif /* CONFIG_DAPLINK_RISCV */
