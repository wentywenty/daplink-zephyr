/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V target: GigaDevice GD32VF103.
 *
 * Detected by reading the STM32F1-compatible DBGMCU IDCODE over the RISC-V
 * system bus. Flash is programmed by reusing the STM32F1 register-level
 * driver (the GD32VF103 FMC is STM32F1-like).
 */

#include <zephyr/kernel.h>

#include "riscv_probe.h"

#if defined(CONFIG_DAPLINK_RISCV)

#include "riscv_dm.h"
#include "flash_mem.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(riscv_gd32vf1);

#define STM32F1_DBGMCU_IDCODE 0xE0042000U
#define GD32VF103_DEV_ID      0x410U

/* GD32VF103: 128 KB flash at 0x08000000, 1 KB pages, 32 KB RAM. */
static const sector_info_t gd32vf103_sectors[] = {
	{ 0x08000000, 0x400 },
};

static const target_cfg_t target_riscv_gd32vf103 = {
	.sectors_info       = gd32vf103_sectors,
	.sector_info_length = 1,
	.flash_regions[0]   = {
		.start      = 0x08000000,
		.end        = 0x08020000,
		.flags      = REGION_IS_DEFAULT,
		.flash_algo = NULL, /* register-level backend only */
	},
	.ram_regions[0]     = {
		.start = 0x20000000,
		.end   = 0x20008000,
	},
	.target_vendor      = "GigaDevice",
	.target_part_number = "GD32VF103",
	.native_ops         = &flash_native_stm32f1,
};

const target_cfg_t *gd32vf1_probe(void)
{
	uint32_t dbg = 0;

	if (!riscv_mem_read32(0U, STM32F1_DBGMCU_IDCODE, &dbg)) {
		return NULL;
	}
	if ((dbg & 0xFFFU) != GD32VF103_DEV_ID) {
		return NULL;
	}
	LOG_INF("RISC-V: GD32VF103 detected (DEV_ID 0x%03X)", dbg & 0xFFFU);
	return &target_riscv_gd32vf103;
}

#endif /* CONFIG_DAPLINK_RISCV */
