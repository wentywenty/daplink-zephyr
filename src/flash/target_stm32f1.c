/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32F1 (STM32F103xB) target configuration and flash algorithm.
 * Ported from DAPLink source/family/st/stm32f103rb/
 */

#include "flash_blob.h"

/* Flash programming algorithm binary blob (ARM Thumb code) */
static const uint32_t stm32f1_flash_prog_blob[] = {
	0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040,
	0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
	0x4603b510, 0x4c442000, 0x48446020, 0x48446060,
	0x46206060, 0xf01069c0, 0xd1080f04, 0x5055f245,
	0x60204c40, 0x60602006, 0x70fff640, 0x200060a0,
	0x4601bd10, 0x69004838, 0x0080f040, 0x61104a36,
	0x47702000, 0x69004834, 0x0004f040, 0x61084932,
	0x69004608, 0x0040f040, 0xe0036108, 0x20aaf64a,
	0x60084930, 0x68c0482c, 0x0f01f010, 0x482ad1f6,
	0xf0206900, 0x49280004, 0x20006108, 0x46014770,
	0x69004825, 0x0002f040, 0x61104a23, 0x61414610,
	0xf0406900, 0x61100040, 0xf64ae003, 0x4a2120aa,
	0x481d6010, 0xf01068c0, 0xd1f60f01, 0x6900481a,
	0x0002f020, 0x61104a18, 0x47702000, 0x4603b510,
	0xf0201c48, 0xe0220101, 0x69004813, 0x0001f040,
	0x61204c11, 0x80188810, 0x480fbf00, 0xf01068c0,
	0xd1fa0f01, 0x6900480c, 0x0001f020, 0x61204c0a,
	0x68c04620, 0x0f14f010, 0x4620d006, 0xf04068c0,
	0x60e00014, 0xbd102001, 0x1c921c9b, 0x29001e89,
	0x2000d1da, 0x0000e7f7, 0x40022000, 0x45670123,
	0xcdef89ab, 0x40003000, 0x00000000,
};

/* All sectors are 1KB (0x400) starting from 0x08000000 */
static const sector_info_t stm32f1_sectors_info[] = {
	{ 0x08000000, 0x400 },
};

/* Flash algorithm descriptor */
static const program_target_t stm32f1_flash = {
	0x20000021,     /* Init */
	0x20000053,     /* UnInit */
	0x20000065,     /* EraseChip */
	0x2000009f,     /* EraseSector */
	0x200000dd,     /* ProgramPage */
	0x0,            /* Verify (not supported) */

	/* System call: BKPT, RSB, RSP */
	{
		0x20000001,         /* BKPT: start of blob + 1 */
		0x20000148,         /* RSB: blob start + header + rw data offset */
		0x20000800,         /* RSP: stack pointer */
	},

	0x20000000 + 0x00000A00,             /* Program buffer in target RAM */
	0x20000000,                          /* Algorithm load address */
	sizeof(stm32f1_flash_prog_blob),     /* Algorithm size */
	stm32f1_flash_prog_blob,             /* Algorithm blob */
	0x00000400,                          /* Page size: 1KB */
	0,                                   /* Flags */
};

/* STM32F103xB target configuration */
const target_cfg_t target_stm32f1 = {
	.sectors_info       = stm32f1_sectors_info,
	.sector_info_length = sizeof(stm32f1_sectors_info) / sizeof(sector_info_t),
	.flash_regions[0]   = {
		.start      = 0x08000000,
		.end        = 0x08020000,  /* 128KB Flash */
		.flags      = REGION_IS_DEFAULT,
		.flash_algo = &stm32f1_flash,
	},
	.ram_regions[0]     = {
		.start = 0x20000000,
		.end   = 0x20005000,  /* 20KB RAM */
	},
	.target_vendor     = "STMicroelectronics",
	.target_part_number = "STM32F103xB",
	.native_ops        = &flash_native_stm32f1,
};
