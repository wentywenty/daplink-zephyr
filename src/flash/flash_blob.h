/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flash algorithm data structures for target programming.
 * Ported from DAPLink flash_blob.h / target_config.h.
 */

#ifndef FLASH_BLOB_H_
#define FLASH_BLOB_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * System call parameters for flash algorithm execution on target.
 * The algorithm runs on the target CPU under debug control.
 */
typedef struct __attribute__((packed)) {
	uint32_t breakpoint;    /* BKPT: address where algorithm stops */
	uint32_t static_base;   /* RSB: static base register value */
	uint32_t stack_pointer;  /* RSP: stack pointer for algorithm */
} program_syscall_t;

/*
 * Flash algorithm descriptor.
 * Describes a flash programming algorithm loaded into target RAM.
 */
typedef struct __attribute__((packed)) {
	uint32_t init;            /* Init function offset */
	uint32_t uninit;          /* UnInit function offset */
	uint32_t erase_chip;      /* EraseChip function offset */
	uint32_t erase_sector;    /* EraseSector function offset */
	uint32_t program_page;    /* ProgramPage function offset */
	uint32_t verify;          /* Verify function offset (0 = none) */

	program_syscall_t sys_call_s;  /* System call parameters */

	uint32_t program_buffer;       /* Data buffer address in target RAM */
	uint32_t algo_start;           /* Where to load algorithm in target RAM */
	uint32_t algo_size;            /* Size of algorithm blob in bytes */
	const uint32_t *algo_blob;     /* Pointer to algorithm blob data */
	uint32_t program_buffer_size;  /* Size of program buffer (page size) */
	uint32_t algo_flags;           /* Algorithm flags (0 = default) */
} program_target_t;

/*
 * Sector size info: sectors starting at 'start' have size 'size'.
 * The size applies to all sectors from 'start' until the next entry.
 */
typedef struct {
	uint32_t start;
	uint32_t size;
} sector_info_t;

/* Maximum flash/RAM regions per target */
#define TARGET_MAX_FLASH_REGIONS  2
#define TARGET_MAX_RAM_REGIONS    2

/* Region flags */
#define REGION_IS_DEFAULT  0x01U

/*
 * Flash region descriptor.
 */
typedef struct {
	uint32_t start;
	uint32_t end;
	uint32_t flags;
	const program_target_t *flash_algo;
} flash_region_t;

/*
 * RAM region descriptor.
 */
typedef struct {
	uint32_t start;
	uint32_t end;
} ram_region_t;

/*
 * Target chip configuration.
 * Describes a target MCU for flash programming.
 */
typedef struct {
	const sector_info_t *sectors_info;
	uint32_t sector_info_length;
	flash_region_t flash_regions[TARGET_MAX_FLASH_REGIONS];
	ram_region_t ram_regions[TARGET_MAX_RAM_REGIONS];
	const char *target_vendor;
	const char *target_part_number;
} target_cfg_t;

/* Target state constants */
enum {
	TARGET_RESET_HOLD   = 0,
	TARGET_RESET_RUN    = 1,
	TARGET_RESET_HALT   = 2,
	TARGET_NO_RESET     = 3,
};

#ifdef __cplusplus
}
#endif

#endif /* FLASH_BLOB_H_ */
