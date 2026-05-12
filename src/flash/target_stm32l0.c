/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32L0 (STM32L082CZ) target configuration and flash algorithm.
 * Ported from DAPLink source/family/st/stm32l082cz/
 */

#include "flash_blob.h"

/* Flash programming algorithm binary blob (ARM Thumb code) */
static const uint32_t stm32l0_flash_prog_blob[] = {
	0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040,
	0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
	0x4603b510, 0x69804851, 0x0224240f, 0x4c4f4320,
	0x484f61a0, 0x484f60e0, 0x484f60e0, 0x484f6120,
	0x46206120, 0x240169c0, 0x40200524, 0xd1062800,
	0x4c4c484b, 0x20066020, 0x484b6060, 0x484b60a0,
	0x200060a0, 0x4601bd10, 0x68404841, 0x43102202,
	0x60504a3f, 0x68404610, 0x43102201, 0x60504a3c,
	0x47702000, 0x483a4601, 0x22016840, 0x43100252,
	0x60504a37, 0x68404610, 0x43102208, 0x60504a34,
	0x60082000, 0x483ae002, 0x60104a37, 0x69804830,
	0x0fc007c0, 0xd1f62800, 0x6840482d, 0x02522201,
	0x4a2b4390, 0x46106050, 0x22086840, 0x4a284390,
	0x20006050, 0x46034770, 0x47702001, 0x4603b570,
	0x303f4608, 0x01890981, 0xe03b2500, 0x68404820,
	0x02b62601, 0x4e1e4330, 0x46306070, 0x26086840,
	0x4e1b4330, 0x24406070, 0x6810e004, 0x1d1b6018,
	0x1f241d12, 0xd1f82c00, 0x481de002, 0x60304e1a,
	0x69804813, 0x0fc007c0, 0xd1f62800, 0x69804810,
	0x0236260f, 0x28004030, 0x480dd006, 0x43306980,
	0x61b04e0b, 0xbd702001, 0x68404809, 0x02b62601,
	0x4e0743b0, 0x46306070, 0x26086840, 0x4e0443b0,
	0x1c6d6070, 0x42a80988, 0x2000d8c0, 0x0000e7eb,
	0x40022000, 0x89abcdef, 0x02030405, 0x8c9daebf,
	0x13141516, 0x00005555, 0x40003000, 0x00000fff,
	0x0000aaaa, 0x00000000,
};

/* All sectors are 128B (0x80) starting from 0x08000000 */
static const sector_info_t stm32l0_sectors_info[] = {
	{ 0x08000000, 0x80 },
};

/* Flash algorithm descriptor */
static const program_target_t stm32l0_flash = {
	0x20000021,     /* Init */
	0x20000063,     /* UnInit */
	0x00000000,     /* EraseChip (not supported) */
	0x20000081,     /* EraseSector */
	0x200000d9,     /* ProgramPage */
	0x0,            /* Verify (not supported) */

	/* System call: BKPT, RSB, RSP */
	{
		0x20000001,         /* BKPT */
		0x20000190,         /* RSB */
		0x20000800,         /* RSP */
	},

	0x20000000 + 0x00000A00,             /* Program buffer in target RAM */
	0x20000000,                          /* Algorithm load address */
	sizeof(stm32l0_flash_prog_blob),     /* Algorithm size */
	stm32l0_flash_prog_blob,             /* Algorithm blob */
	0x00000080,                          /* Page size: 128B */
	0,                                   /* Flags */
};

/* STM32L082CZ target configuration */
const target_cfg_t target_stm32l0 = {
	.sectors_info       = stm32l0_sectors_info,
	.sector_info_length = sizeof(stm32l0_sectors_info) / sizeof(sector_info_t),
	.flash_regions[0]   = {
		.start      = 0x08000000,
		.end        = 0x08030000,  /* 192KB Flash */
		.flags      = REGION_IS_DEFAULT,
		.flash_algo = &stm32l0_flash,
	},
	.ram_regions[0]     = {
		.start = 0x20000000,
		.end   = 0x20005000,  /* 20KB RAM */
	},
	.target_vendor     = "STMicroelectronics",
	.target_part_number = "STM32L082CZ",
};
