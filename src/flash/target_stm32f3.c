/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32F3 (STM32F334R8) target configuration and flash algorithm.
 * Ported from DAPLink source/family/st/stm32f334r8/
 */

#include "flash_blob.h"

/* Flash programming algorithm binary blob (ARM Thumb code) */
static const uint32_t stm32f3_flash_prog_blob[] = {
	0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040,
	0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
	0x49364837, 0x49376041, 0x21006041, 0x68c16001,
	0x43112214, 0x69c060c1, 0xd40605c0, 0x49324833,
	0x21066001, 0x49326041, 0x20006081, 0x482c4770,
	0x22806901, 0x61014311, 0x47702000, 0x4828b510,
	0x24046901, 0x61014321, 0x22406901, 0x61014311,
	0x4a264928, 0x6011e000, 0x07db68c3, 0x6901d1fb,
	0x610143a1, 0xbd102000, 0x491db510, 0x2402690a,
	0x610a4322, 0x69086148, 0x43102240, 0x481d6108,
	0xe0004a1a, 0x68cb6010, 0xd1fb07db, 0x43a06908,
	0x20006108, 0xb570bd10, 0x08491c49, 0x26140049,
	0x23014d0f, 0x692ce016, 0x612c431c, 0x80048814,
	0x07e468ec, 0x692cd1fc, 0x00640864, 0x68ec612c,
	0xd0044234, 0x433068e8, 0x200160e8, 0x1c80bd70,
	0x1e891c92, 0xd1e62900, 0xbd702000, 0x45670123,
	0x40022000, 0xcdef89ab, 0x00005555, 0x40003000,
	0x00000fff, 0x0000aaaa, 0x00000000,
};

/* All sectors are 2KB (0x800) starting from 0x08000000 */
static const sector_info_t stm32f3_sectors_info[] = {
	{ 0x08000000, 0x800 },
};

/* Flash algorithm descriptor */
static const program_target_t stm32f3_flash = {
	0x20000021,     /* Init */
	0x2000004f,     /* UnInit */
	0x2000005d,     /* EraseChip */
	0x20000089,     /* EraseSector */
	0x200000b7,     /* ProgramPage */
	0x0,            /* Verify (not supported) */

	/* System call: BKPT, RSB, RSP */
	{
		0x20000001,         /* BKPT */
		0x20000118,         /* RSB */
		0x20000800,         /* RSP */
	},

	0x20000000 + 0x00000A00,             /* Program buffer in target RAM */
	0x20000000,                          /* Algorithm load address */
	sizeof(stm32f3_flash_prog_blob),     /* Algorithm size */
	stm32f3_flash_prog_blob,             /* Algorithm blob */
	0x00000400,                          /* Page size: 1KB */
	0,                                   /* Flags */
};

/* STM32F334R8 target configuration */
const target_cfg_t target_stm32f3 = {
	.sectors_info       = stm32f3_sectors_info,
	.sector_info_length = sizeof(stm32f3_sectors_info) / sizeof(sector_info_t),
	.flash_regions[0]   = {
		.start      = 0x08000000,
		.end        = 0x08010000,  /* 64KB Flash */
		.flags      = REGION_IS_DEFAULT,
		.flash_algo = &stm32f3_flash,
	},
	.ram_regions[0]     = {
		.start = 0x20000000,
		.end   = 0x20003000,  /* 12KB RAM */
	},
	.target_vendor     = "STMicroelectronics",
	.target_part_number = "STM32F334R8Tx",
};
