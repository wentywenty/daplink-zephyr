/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SFDP parsing (see sfdp.h).
 */

#include <string.h>

#include "sfdp.h"

#define SFDP_MAGIC          0x50444653U /* "SFDP" little-endian */
#define SFDP_BASIC_TABLE_ID 0xFF00U
#define SFDP_BFPT_MIN_LEN   44U
#define SFDP_ERASE_TYPES    4U

bool sfdp_parse(const uint8_t *data, uint32_t len, struct sfdp_params *out)
{
	if (len < 8U) {
		return false;
	}
	if (memcmp(data, "SFDP", 4) != 0) {
		return false;
	}

	const uint8_t major = data[5];
	const uint8_t minor = data[4];
	const uint8_t headers_count = data[6];

	memset(out, 0, sizeof(*out));

	for (uint8_t i = 0; i <= headers_count; i++) {
		const uint32_t th = 8U + 8U * i;

		if (th + 8U > len) {
			return false;
		}
		const uint8_t *h = &data[th];
		const uint16_t param_id = (uint16_t)(((uint16_t)h[7] << 8) | h[0]);

		if (param_id != SFDP_BASIC_TABLE_ID) {
			continue;
		}

		const uint32_t table_addr =
			((uint32_t)h[6] << 16) | ((uint32_t)h[5] << 8) | h[4];

		if (table_addr + SFDP_BFPT_MIN_LEN > len) {
			return false;
		}
		const uint8_t *b = &data[table_addr];

		/* Memory density (bits) at bytes 4..7, MSb = exponential flag. */
		const uint32_t density = (((b[7] & 0x7FU) << 24) | ((uint32_t)b[6] << 16) |
					  ((uint32_t)b[5] << 8) | b[4]);
		const uint32_t capacity_bits = (b[7] & 0x80U) ? (1U << density) : (density + 1U);

		out->capacity = capacity_bits >> 3U;
		out->sector_erase_opcode = b[1];
		for (uint32_t e = 0; e < SFDP_ERASE_TYPES; e++) {
			if (b[28U + e * 2U + 1U] == out->sector_erase_opcode) {
				out->sector_size = 1U << b[28U + e * 2U];
				break;
			}
		}

		/* Page-size dword is only valid from SFDP v1.5. */
		if (major > 1U || (major == 1U && minor >= 5U)) {
			out->page_size = 1U << (b[40] >> 4U);
		} else {
			out->page_size = 256U;
		}
		out->valid = true;
		return true;
	}
	return false;
}
