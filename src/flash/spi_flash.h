/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Probe-side SPI NOR flash driver (JEDEC + SFDP).
 *
 * Discovers geometry from SFDP (falling back to the JEDEC ID) and provides
 * read/erase/program primitives over a Zephyr SPI controller. Exposed to the
 * host through CMSIS-DAP vendor commands (see dap_vendor.c).
 */

#ifndef SPI_FLASH_H_
#define SPI_FLASH_H_

#include <stdint.h>
#include <stdbool.h>

struct spi_flash_info {
	uint32_t jedec_id;
	uint32_t capacity;      /* bytes */
	uint32_t sector_size;   /* erase sector size in bytes */
	uint32_t page_size;     /* program page size in bytes */
	uint8_t sector_erase_opcode;
	bool present;
};

/* Probe the flash (JEDEC + SFDP). Returns 0 on success. */
int spi_flash_init(void);

/* Describe the probed device. */
bool spi_flash_get_info(struct spi_flash_info *info);

/* Read len bytes from addr. */
bool spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);

/* Erase the sector containing addr. */
bool spi_flash_erase_sector(uint32_t addr);

/* Program len bytes at addr (erased). */
bool spi_flash_program(uint32_t addr, const uint8_t *data, uint32_t len);

/* Erase the whole chip. */
bool spi_flash_erase_chip(void);

#endif /* SPI_FLASH_H_ */
