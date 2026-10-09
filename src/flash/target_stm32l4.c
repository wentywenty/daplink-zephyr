/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32L4 (STM32L476RG) target configuration and flash algorithm.
 * Ported from DAPLink source/family/st/stm32l476rg/
 */

#include "flash_blob.h"

/* Flash programming algorithm binary blob (ARM Thumb code) */
static const uint32_t stm32l4_flash_prog_blob[] = {
	0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040,
	0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
	0x6a094953, 0xd5040289, 0x42884952, 0x2001d301,
	0x20004770, 0x484e4770, 0x6081494f, 0x6081494f,
	0x60012100, 0x31faf244, 0x6a006101, 0xd40803c0,
	0xf245484b, 0x60015155, 0x60412106, 0x71fff640,
	0x20006081, 0x48424770, 0xf0416941, 0x61414100,
	0x47702000, 0x47702001, 0xf64ab510, 0x494022aa,
	0xe000483b, 0x6903600a, 0xd4fb03db, 0x34faf244,
	0xf2486104, 0x61430304, 0xf4436943, 0x61433380,
	0x600ae000, 0x03db6903, 0x2100d4fb, 0x69016141,
	0xd0024221, 0x20016104, 0x2000bd10, 0xb570bd10,
	0xf7ff4602, 0xf3c2ffad, 0xf64a25c7, 0x4a2c23aa,
	0xe0004927, 0x690c6013, 0xd4fb03e4, 0x34faf244,
	0x2602610c, 0x05c5eb06, 0x20c0ea45, 0x69486148,
	0x3080f440, 0xe0006148, 0x69086013, 0xd4fb03c0,
	0x61482000, 0x40206908, 0x610cd001, 0xbd702001,
	0x1dc9b5f0, 0x0107f021, 0x25aaf64a, 0x4c144e18,
	0x6035e000, 0x03db6923, 0xf244d4fb, 0x612737fa,
	0x0e01f04f, 0x0c00f04f, 0xf8c4e015, 0x6813e014,
	0x68536003, 0xe0006043, 0x69236035, 0xd4fb03db,
	0xc014f8c4, 0x423b6923, 0x6127d002, 0xbdf02001,
	0x39083008, 0x29003208, 0x2000d1e7, 0x0000bdf0,
	0x40022000, 0x08080000, 0x45670123, 0xcdef89ab,
	0x40003000, 0x00000000,
};

/* All sectors are 2KB (0x800) starting from 0x08000000 */
static const sector_info_t stm32l4_sectors_info[] = {
	{ 0x08000000, 0x800 },
};

/* Flash algorithm descriptor */
static const program_target_t stm32l4_flash = {
	0x20000037,     /* Init */
	0x20000067,     /* UnInit */
	0x20000079,     /* EraseChip */
	0x200000bf,     /* EraseSector */
	0x20000111,     /* ProgramPage */
	0x0,            /* Verify (not supported) */

	/* System call: BKPT, RSB, RSP */
	{
		0x20000001,         /* BKPT */
		0x20000184,         /* RSB */
		0x20000800,         /* RSP */
	},

	0x20000000 + 0x00000A00,             /* Program buffer in target RAM */
	0x20000000,                          /* Algorithm load address */
	sizeof(stm32l4_flash_prog_blob),     /* Algorithm size */
	stm32l4_flash_prog_blob,             /* Algorithm blob */
	0x00000400,                          /* Page size: 1KB */
	0,                                   /* Flags */
};

/* STM32L476RG target configuration */
const target_cfg_t target_stm32l4 = {
	.sectors_info       = stm32l4_sectors_info,
	.sector_info_length = sizeof(stm32l4_sectors_info) / sizeof(sector_info_t),
	.flash_regions[0]   = {
		.start      = 0x08000000,
		.end        = 0x08100000,  /* 1MB Flash */
		.flags      = REGION_IS_DEFAULT,
		.flash_algo = &stm32l4_flash,
	},
	.ram_regions[0]     = {
		.start = 0x20000000,
		.end   = 0x20018000,  /* 96KB RAM */
	},
	.target_vendor     = "STMicroelectronics",
	.target_part_number = "STM32L476RGTx",
	.native_ops        = &flash_native_stm32l4,
};
