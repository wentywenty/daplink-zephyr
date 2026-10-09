/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32F429ZITx target configuration and flash algorithm.
 * Auto-ported from DAPLink ../daplink/source/family/st/stm32f429zi by tools/port_family.py
 */

#include "flash_blob.h"

static const uint32_t STM32F429ZI_flash_prog_blob[] = {
    0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040, 0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
    0x03004601, 0x28200e00, 0x0940d302, 0xe0051d00, 0xd3022810, 0x1cc00900, 0x0880e000, 0xd50102c9,
    0x43082110, 0x48464770, 0x60414944, 0x60414945, 0x60012100, 0x22f068c1, 0x60c14311, 0x06806940,
    0x4842d406, 0x60014940, 0x60412106, 0x60814940, 0x47702000, 0x6901483a, 0x43110542, 0x20006101,
    0xb5304770, 0x69014836, 0x43212404, 0x69016101, 0x43290365, 0x69016101, 0x431103a2, 0x49356101,
    0xe0004a32, 0x68c36011, 0xd4fb03db, 0x43a16901, 0x69016101, 0x610143a9, 0xbd302000, 0xf7ffb530,
    0x4927ffaf, 0x23f068ca, 0x60ca431a, 0x610c2402, 0x06c0690a, 0x43020e00, 0x6908610a, 0x431003e2,
    0x48246108, 0xe0004a21, 0x68cd6010, 0xd4fb03ed, 0x43a06908, 0x68c86108, 0x0f000600, 0x68c8d003,
    0x60c84318, 0xbd302001, 0x4d15b570, 0x08891cc9, 0x008968eb, 0x433326f0, 0x230060eb, 0x4b16612b,
    0x692ce017, 0x612c431c, 0x60046814, 0x03e468ec, 0x692cd4fc, 0x00640864, 0x68ec612c, 0x0f240624,
    0x68e8d004, 0x60e84330, 0xbd702001, 0x1d121d00, 0x29001f09, 0x2000d1e5, 0x0000bd70, 0x45670123,
    0x40023c00, 0xcdef89ab, 0x00005555, 0x40003000, 0x00000fff, 0x0000aaaa, 0x00000201, 0x00000000
};

static const sector_info_t stm32f429_sectors_info[] = {
    { 0x08000000, 0x00004000 }, // 4 x 16KB
    { 0x08010000, 0x00010000 }, // 1 x 64KB
    { 0x08020000, 0x00020000 }, // 7 x 128KB
};

static const program_target_t stm32f429_flash = {
    0x20000047, // Init
    0x20000075, // UnInit
    0x20000083, // EraseChip
    0x200000bd, // EraseSector
    0x20000109, // ProgramPage
    0x0,        // Verify
    
    // BKPT : start of blob + 1
    // RSB  : blob start + header + rw data offset
    // RSP  : stack pointer
    {
        0x20000001,
        0x2000017c,
        0x20000800
    },

    0x20000000 + 0x00000A00,  // mem buffer location
    0x20000000,               // location to write prog_blob in target RAM
    sizeof(STM32F429ZI_flash_prog_blob),   // prog_blob size
    STM32F429ZI_flash_prog_blob,           // address of prog_blob
    0x00000400       // ram_to_flash_bytes_to_be_written
};

const target_cfg_t target_stm32f429 = {
	.sectors_info       = stm32f429_sectors_info,
	.sector_info_length = sizeof(stm32f429_sectors_info) / sizeof(sector_info_t),
	.flash_regions[0]   = {
		.start      = 0x08000000,
		.end        = 0x08200000,
		.flags      = REGION_IS_DEFAULT,
		.flash_algo = &stm32f429_flash,
	},
	.ram_regions[0]     = { .start = 0x20000000, .end = 0x20030000 },
	.target_vendor     = "STMicroelectronics",
	.target_part_number = "STM32F429ZITx",
	.native_ops        = &flash_native_stm32f4,
};
