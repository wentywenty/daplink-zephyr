/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SFDP (JEDEC JESD216) parsing — platform-independent.
 *
 * Parses a raw SFDP image (as read from a SPI NOR flash starting at address
 * 0) into capacity / erase-sector / page-size parameters. Kept free of any
 * hardware dependency so it can be unit tested on a host or emulator.
 */

#ifndef SFDP_H_
#define SFDP_H_

#include <stdint.h>
#include <stdbool.h>

struct sfdp_params {
	uint32_t capacity;            /* bytes */
	uint32_t sector_size;         /* erase sector size in bytes */
	uint32_t page_size;           /* program page size in bytes */
	uint8_t sector_erase_opcode;
	bool valid;
};

/* Parse an SFDP image of `len` bytes. Returns true on success. */
bool sfdp_parse(const uint8_t *data, uint32_t len, struct sfdp_params *out);

#endif /* SFDP_H_ */
