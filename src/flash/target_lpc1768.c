/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * NXP LPC1768 target configuration and flash algorithm.
 * Ported from DAPLink source/family/nxp/lpc1768/
 *
 * Note: Flash starts at 0x00000000, RAM at 0x10000000 (AHB SRAM).
 *       Algorithm is loaded to 0x10000000, not the usual 0x20000000.
 */

#include "flash_blob.h"

/* Flash programming algorithm binary blob (ARM Thumb code) */
static const uint32_t lpc1768_flash_prog_blob[] = {
	0xe00abe00, 0x062d780d, 0x24084068, 0xd3000040,
	0x1e644058, 0x1c49d1fa, 0x2a001e52, 0x4770d1f2,
	0x28100b00, 0x210ed302, 0xd0eb01,   0x494f4770,
	0x607af44f, 0x60084449, 0x2100484d, 0x21aa7001,
	0x21557301, 0x21017301, 0x1c40f800, 0x47702000,
	0x47702000, 0x41f0e92d, 0x20324c46, 0x2500444c,
	0xe884261d, 0xf1040061, 0x4f430114, 0x46204688,
	0x696047b8, 0x2034b960, 0x61e884,   0x4641483b,
	0x68004448, 0x462060e0, 0x696047b8, 0xd0002800,
	0xe8bd2001, 0xe92d81f0, 0xf7ff41f0, 0x4d35ffc1,
	0x444d4604, 0xe9c52032, 0xf1050400, 0x4e320114,
	0x4628460f, 0x47b060ac, 0xb9686968, 0xe9c52034,
	0x482a0400, 0x444860ac, 0x68004639, 0x462860e8,
	0x696847b0, 0xd0dc2800, 0xe7da2001, 0x41f0e92d,
	0x64614,    0x4825d11d, 0x12fcf8d4, 0xd03a4281,
	0x42814823, 0x4823d037, 0xd0344281, 0x4030ea4f,
	0xd0304281, 0x100e9d4,  0xe9d44408, 0x44111202,
	0x69214408, 0x69614408, 0x69a14408, 0x42404408,
	0x463061e0, 0xff7cf7ff, 0x21324d12, 0x4f12444d,
	0x1000e9c5, 0x114f105,  0x468860a8, 0x47b84628,
	0xb9806968, 0xe9c52033, 0xf44f0600, 0xe9c57000,
	0x48064002, 0x44484641, 0x61286800, 0x47b84628,
	0x28006968, 0x2001d095, 0xe793,     0x4,
	0x400fc080, 0x8,        0x1fff1ff1, 0x4e697370,
	0x12345678, 0x87654321, 0x0,        0x0,
};

/* All sectors are 4KB (0x1000) starting from 0x00000000 */
static const sector_info_t lpc1768_sectors_info[] = {
	{ 0x00000000, 0x1000 },
};

/* Flash algorithm descriptor */
static const program_target_t lpc1768_flash = {
	0x1000002f,     /* Init */
	0x10000051,     /* UnInit */
	0x10000055,     /* EraseChip */
	0x10000097,     /* EraseSector */
	0x100000dd,     /* ProgramPage */
	0x0,            /* Verify (not supported) */

	/* System call: BKPT, RSB, RSP */
	{
		0x10000001,         /* BKPT */
		0x10000214,         /* RSB */
		0x10001000,         /* RSP */
	},

	0x1000023c,                          /* Program buffer in target RAM */
	0x10000000,                          /* Algorithm load address */
	sizeof(lpc1768_flash_prog_blob),      /* Algorithm size */
	lpc1768_flash_prog_blob,              /* Algorithm blob */
	0x00000200,                          /* Page size: 512B */
	0,                                   /* Flags */
};

/* LPC1768 target configuration */
const target_cfg_t target_lpc1768 = {
	.sectors_info       = lpc1768_sectors_info,
	.sector_info_length = sizeof(lpc1768_sectors_info) / sizeof(sector_info_t),
	.flash_regions[0]   = {
		.start      = 0x00000000,
		.end        = 0x00080000,  /* 512KB Flash */
		.flags      = REGION_IS_DEFAULT,
		.flash_algo = &lpc1768_flash,
	},
	.ram_regions[0]     = {
		.start = 0x10000000,
		.end   = 0x10008000,  /* 32KB RAM (AHB SRAM) */
	},
	.target_vendor     = "NXP",
	.target_part_number = "LPC1768",
};
