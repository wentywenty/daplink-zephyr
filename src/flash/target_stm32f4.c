/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32F4 (STM32F407VE) target configuration and flash algorithm.
 * Ported from DAPLink source/family/st/stm32f407ve/
 */

#include "flash_blob.h"

/* Flash programming algorithm binary blob (ARM Thumb code) */
static const uint32_t stm32f4_flash_prog_blob[] = {
	0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040,
	0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
	0xf3c04601, 0x28203007, 0x2204bf24, 0x1050eb02,
	0x2810d205, 0x2203bf26, 0x1010eb02, 0xf4110880,
	0xbf181f80, 0x0010f040, 0x486b4770, 0x60014969,
	0x6001496a, 0x6801486a, 0x01f0f041, 0x48696001,
	0xf0106800, 0xd1080f20, 0xf2454867, 0x60015155,
	0x60412106, 0x71fff640, 0x20006081, 0x49634770,
	0xf4206808, 0x600a52f8, 0x48616008, 0xf0416801,
	0x60014100, 0x47702000, 0xc174f8df, 0x0000f8dc,
	0x0004f040, 0x0000f8cc, 0x0000f8dc, 0x3080f440,
	0x0000f8cc, 0x0004f1ac, 0xf4116801, 0xbf1c3f80,
	0x21aaf64a, 0xd0044a50, 0x68036011, 0x3f80f413,
	0xf8dcd1fa, 0xf0200000, 0xf8cc0004, 0x20000000,
	0xf3c04770, 0x29203107, 0x2204bf24, 0x1151eb02,
	0x2910d205, 0x2203bf26, 0x1111eb02, 0xf4100889,
	0xbf181f80, 0x0110f041, 0x6802483d, 0x02f0f042,
	0xf1006002, 0x22020c04, 0x2000f8cc, 0x2000f8dc,
	0xea0323f8, 0x431101c1, 0x1000f8cc, 0x1000f8dc,
	0x3180f441, 0x1000f8cc, 0xf4116801, 0xbf1c3f80,
	0x21aaf64a, 0xd0044a30, 0x68036011, 0x3f80f413,
	0xf8dcd1fa, 0xf0211000, 0xf8cc0102, 0x68011000,
	0x0ff0f011, 0x2000bf04, 0x68014770, 0x01f0f041,
	0x20016001, 0x4b224770, 0x1cc9b430, 0xc000f8d3,
	0x0103f031, 0x0cf0f04c, 0xc000f8c3, 0x0404f103,
	0x0c00f04f, 0xc000f8c4, 0xf240bf18, 0xd0252501,
	0xc000f8d4, 0x0c05ea4c, 0xc000f8c4, 0xc000f8d2,
	0xc000f8c0, 0xc000f8d3, 0x3f80f41c, 0xf8d4d1fa,
	0xf02cc000, 0xf8c40c01, 0xf8d3c000, 0xf01cc000,
	0xd0060ff0, 0xf0406818, 0x601800f0, 0x2001bc30,
	0x1d004770, 0xf1021f09, 0xd1d90204, 0x2000bc30,
	0x00004770, 0x45670123, 0x40023c04, 0xcdef89ab,
	0x40023c0c, 0x40023c14, 0x40003000, 0x40023c00,
	0x40023c10, 0x00000000,
};

/*
 * Variable-size sectors for STM32F407:
 *   0x08000000 - 0x0800FFFF: 4 × 16KB sectors
 *   0x08010000 - 0x0801FFFF: 1 × 64KB sector
 *   0x08020000 - 0x0807FFFF: 3 × 128KB sectors
 */
static const sector_info_t stm32f4_sectors_info[] = {
	{ 0x08000000, 0x00004000 },   /* 16KB */
	{ 0x08010000, 0x00010000 },   /* 64KB */
	{ 0x08020000, 0x00020000 },   /* 128KB */
};

/* Flash algorithm descriptor */
static const program_target_t stm32f4_flash = {
	0x2000004b,     /* Init */
	0x2000007f,     /* UnInit */
	0x20000099,     /* EraseChip */
	0x200000e3,     /* EraseSector */
	0x20000177,     /* ProgramPage */
	0x0,            /* Verify (not supported) */

	/* System call: BKPT, RSB, RSP */
	{
		0x20000001,         /* BKPT */
		0x20000214,         /* RSB */
		0x20002000,         /* RSP */
	},

	0x20000000 + 0x00000A00,             /* Program buffer in target RAM */
	0x20000000,                          /* Algorithm load address */
	sizeof(stm32f4_flash_prog_blob),     /* Algorithm size */
	stm32f4_flash_prog_blob,             /* Algorithm blob */
	0x00001000,                          /* Page size: 4KB (smallest common denominator) */
	0,                                   /* Flags */
};

/* STM32F407VE target configuration */
const target_cfg_t target_stm32f4 = {
	.sectors_info       = stm32f4_sectors_info,
	.sector_info_length = sizeof(stm32f4_sectors_info) / sizeof(sector_info_t),
	.flash_regions[0]   = {
		.start      = 0x08000000,
		.end        = 0x08080000,  /* 512KB Flash */
		.flags      = REGION_IS_DEFAULT,
		.flash_algo = &stm32f4_flash,
	},
	.ram_regions[0]     = {
		.start = 0x20000000,
		.end   = 0x20020000,  /* 128KB RAM */
	},
	.target_vendor     = "STMicroelectronics",
	.target_part_number = "STM32F407VETx",
};
