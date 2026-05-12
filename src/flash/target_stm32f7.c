/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32F7 (STM32F746ZG) target configuration and flash algorithm.
 * Ported from DAPLink source/family/st/stm32f746zg/
 */

#include "flash_blob.h"

/* Flash programming algorithm binary blob (ARM Thumb code) */
static const uint32_t stm32f7_flash_prog_blob[] = {
	0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040,
	0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
	0x8f4ff3bf, 0x03004770, 0x28400e00, 0x0980d302,
	0x47701d00, 0x477008c0, 0x49474848, 0x49486041,
	0x21006041, 0x68c16001, 0x431122f0, 0x694060c1,
	0xd4060680, 0x49434844, 0x21066001, 0x49436041,
	0x20006081, 0x483d4770, 0x05426901, 0x61014311,
	0x47702000, 0x4839b510, 0x24046901, 0x61014321,
	0x03a26901, 0x61014311, 0x4a374939, 0x6011e000,
	0x03db68c3, 0x6901d4fb, 0x610143a1, 0xbd102000,
	0xf7ffb530, 0x492dffc0, 0x23f068ca, 0x60ca431a,
	0x610c2402, 0x0700690a, 0x43020e40, 0x6908610a,
	0x431003e2, 0x482a6108, 0xe0004a27, 0x68cd6010,
	0xd4fb03ed, 0x43a06908, 0x68c86108, 0x0f000600,
	0x68c8d003, 0x60c84318, 0xbd302001, 0x4c1bb5f0,
	0x08891cc9, 0x008968e5, 0x431d23f0, 0x230060e5,
	0x27016123, 0x4d1a06ff, 0x6923e021, 0x43334e19,
	0x03066123, 0x19f60b36, 0x60336813, 0x8f4ff3bf,
	0xe0004e11, 0x68e36035, 0xd4fb03db, 0x085b6923,
	0x6123005b, 0x061b68e3, 0xd0050f1b, 0x21f068e0,
	0x60e04308, 0xbdf02001, 0x1f091d00, 0x29001d12,
	0x2000d1db, 0x0000bdf0, 0x45670123, 0x40023c00,
	0xcdef89ab, 0x00005555, 0x40003000, 0x00000fff,
	0x0000aaaa, 0x00000201, 0x00000000,
};

/* All sectors are 32KB (0x8000) starting from 0x08000000 */
static const sector_info_t stm32f7_sectors_info[] = {
	{ 0x08000000, 0x8000 },
};

/* Flash algorithm descriptor */
static const program_target_t stm32f7_flash = {
	0x20000039,     /* Init */
	0x20000067,     /* UnInit */
	0x20000075,     /* EraseChip */
	0x200000a1,     /* EraseSector */
	0x200000ed,     /* ProgramPage */
	0x0,            /* Verify (not supported) */

	/* System call: BKPT, RSB, RSP */
	{
		0x20000001,         /* BKPT */
		0x20000178,         /* RSB */
		0x20000800,         /* RSP */
	},

	0x20000000 + 0x00000A00,             /* Program buffer in target RAM */
	0x20000000,                          /* Algorithm load address */
	sizeof(stm32f7_flash_prog_blob),     /* Algorithm size */
	stm32f7_flash_prog_blob,             /* Algorithm blob */
	0x00000400,                          /* Page size: 1KB */
	0,                                   /* Flags */
};

/* STM32F746ZG target configuration */
const target_cfg_t target_stm32f7 = {
	.sectors_info       = stm32f7_sectors_info,
	.sector_info_length = sizeof(stm32f7_sectors_info) / sizeof(sector_info_t),
	.flash_regions[0]   = {
		.start      = 0x08000000,
		.end        = 0x08100000,  /* 1MB Flash */
		.flags      = REGION_IS_DEFAULT,
		.flash_algo = &stm32f7_flash,
	},
	.ram_regions[0]     = {
		.start = 0x20000000,
		.end   = 0x20050000,  /* 320KB RAM */
	},
	.target_vendor     = "STMicroelectronics",
	.target_part_number = "STM32F746ZGTx",
};
