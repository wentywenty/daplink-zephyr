/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Nordic nRF52832 target configuration and flash algorithm.
 * Ported from DAPLink source/family/nordic/nrf52/
 *
 * Note: Flash starts at 0x00000000 (not 0x08000000 like STM32).
 *       Sectors are 4KB. Uses 64KB RAM variant.
 */

#include "flash_blob.h"

/* Flash programming algorithm binary blob (ARM Thumb code) */
static const uint32_t nrf52832_flash_prog_blob[] = {
	0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040,
	0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
	0x47702000, 0x47702000, 0x4c2bb570, 0x60202002,
	0x20014929, 0x60083108, 0x68284d28, 0xd00207c0,
	0x60202000, 0xf000bd70, 0xe7f6f833, 0x4c22b570,
	0x60212102, 0x2f10f1b0, 0x491fd303, 0x31102001,
	0x491de001, 0x60081d09, 0xf0004d1c, 0x6828f821,
	0xd0fa07c0, 0x60202000, 0xe92dbd70, 0xf8df41f0,
	0x088e8058, 0x46142101, 0xf8c84605, 0x4f131000,
	0xc501cc01, 0x07c06838, 0x1e76d007, 0x2100d1f8,
	0x1000f8c8, 0xe8bd4608, 0xf00081f0, 0xe7f1f801,
	0x6800480b, 0x00fff010, 0x490ad00c, 0x29006809,
	0x4908d008, 0x31fc4a08, 0xd00007c3, 0x1d09600a,
	0xd1f90840, 0x00004770, 0x4001e504, 0x4001e400,
	0x40010404, 0x40010504, 0x6e524635, 0x00000000,
};

/* All sectors are 4KB (0x1000) starting from 0x00000000 */
static const sector_info_t nrf52_sectors_info[] = {
	{ 0x00000000, 0x1000 },
};

/* Flash algorithm descriptor */
static const program_target_t nrf52_flash = {
	0x20000021,     /* Init */
	0x20000025,     /* UnInit */
	0x20000029,     /* EraseChip */
	0x2000004d,     /* EraseSector */
	0x2000007b,     /* ProgramPage */
	0x0,            /* Verify (not supported) */

	/* System call: BKPT, RSB, RSP */
	{
		0x20000001,         /* BKPT */
		0x20000170,         /* RSB */
		0x20001000,         /* RSP */
	},

	0x20000200,                          /* Program buffer in target RAM */
	0x20000000,                          /* Algorithm load address */
	sizeof(nrf52832_flash_prog_blob),     /* Algorithm size */
	nrf52832_flash_prog_blob,             /* Algorithm blob */
	0x00000200,                          /* Page size: 512B */
	0,                                   /* Flags */
};

/* nRF52832 (64KB RAM variant) target configuration */
const target_cfg_t target_nrf52832 = {
	.sectors_info       = nrf52_sectors_info,
	.sector_info_length = sizeof(nrf52_sectors_info) / sizeof(sector_info_t),
	.flash_regions[0]   = {
		.start      = 0x00000000,
		.end        = 0x00080000,  /* 512KB Flash */
		.flags      = REGION_IS_DEFAULT,
		.flash_algo = &nrf52_flash,
	},
	.ram_regions[0]     = {
		.start = 0x20000000,
		.end   = 0x20010000,  /* 64KB RAM */
	},
	.target_vendor     = "NordicSemiconductor",
	.target_part_number = "nRF52832_xxAA",
	.native_ops        = &flash_native_nrf52,
};
