/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Intel HEX format parser.
 * Decodes IHEX records and delivers binary data via callback.
 * Simplified port from DAPLink source/daplink/drag-n-drop/intelhex.c.
 */

#include <string.h>
#include "intelhex.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(intelhex);

/* IHEX record types */
#define IHEX_DATA_RECORD            0x00
#define IHEX_EOF_RECORD             0x01
#define IHEX_EXTENDED_LINEAR_ADDR   0x04

/* Maximum bytes of data per IHEX record (standard limit) */
#define IHEX_MAX_DATA_BYTES         255

/*
 * Convert a single hex character to its 4-bit value.
 * Returns -1 on invalid input.
 */
static int8_t hex_char(uint8_t c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return -1;
}

/*
 * Parse two hex characters into a byte value.
 * Returns true on success, false on invalid input.
 */
static bool hex_byte(const uint8_t *p, uint8_t *out)
{
	int8_t hi = hex_char(p[0]);
	int8_t lo = hex_char(p[1]);

	if (hi < 0 || lo < 0) {
		return false;
	}
	*out = (uint8_t)((hi << 4) | lo);
	return true;
}

/*
 * Parse a single IHEX record line.
 *
 * IHEX record format:
 *   :LLAAAATT[DD...]CC
 *   LL   = byte count
 *   AAAA = 16-bit address
 *   TT   = record type
 *   DD   = data bytes (LL of them)
 *   CC   = checksum
 */
static hex_parse_result_t parse_record(const uint8_t *line, uint32_t len,
				       uint32_t *base_addr,
				       hex_data_cb_t cb, void *ctx)
{
	uint8_t byte_count;
	uint16_t addr16;
	uint8_t record_type;
	uint8_t checksum_calc = 0;
	uint8_t data_buf[IHEX_MAX_DATA_BYTES];
	const uint8_t *p;
	uint8_t checksum_file;

	/* Find the start code ':' */
	p = NULL;
	for (uint32_t i = 0; i < len; i++) {
		if (line[i] == ':') {
			p = &line[i + 1];
			break;
		}
		if (line[i] == '\n' || line[i] == '\r') {
			/* Empty line, skip */
			return HEX_PARSE_OK;
		}
	}
	if (p == NULL) {
		return HEX_PARSE_OK; /* Skip lines without start code */
	}

	/* Need at least: LL(2) + AAAA(4) + TT(2) + CC(2) = 10 hex chars */
	uint32_t remaining = len - (uint32_t)(p - line);

	if (remaining < 10) {
		return HEX_PARSE_LINE_ERR;
	}

	/* Parse byte count */
	if (!hex_byte(p, &byte_count)) {
		return HEX_PARSE_LINE_ERR;
	}
	checksum_calc += byte_count;
	p += 2;

	/* Parse 16-bit address */
	{
		uint8_t addr_hi, addr_lo;

		if (!hex_byte(p, &addr_hi) || !hex_byte(p + 2, &addr_lo)) {
			return HEX_PARSE_LINE_ERR;
		}
		addr16 = ((uint16_t)addr_hi << 8) | addr_lo;
		checksum_calc += addr_hi + addr_lo;
		p += 4;
	}

	/* Parse record type */
	if (!hex_byte(p, &record_type)) {
		return HEX_PARSE_LINE_ERR;
	}
	checksum_calc += record_type;
	p += 2;

	/* Parse data bytes */
	if (byte_count > IHEX_MAX_DATA_BYTES) {
		return HEX_PARSE_LINE_ERR;
	}

	remaining = len - (uint32_t)(p - line);
	if (remaining < (uint32_t)(byte_count * 2 + 2)) {
		return HEX_PARSE_LINE_ERR;
	}

	for (uint8_t i = 0; i < byte_count; i++) {
		if (!hex_byte(p, &data_buf[i])) {
			return HEX_PARSE_LINE_ERR;
		}
		checksum_calc += data_buf[i];
		p += 2;
	}

	/* Parse and verify checksum */
	if (!hex_byte(p, &checksum_file)) {
		return HEX_PARSE_LINE_ERR;
	}

	checksum_calc = (uint8_t)(~checksum_calc + 1); /* Two's complement */
	if (checksum_calc != checksum_file) {
		LOG_ERR("IHEX: checksum mismatch (calc=0x%02X, file=0x%02X)",
			checksum_calc, checksum_file);
		return HEX_PARSE_CKSUM_FAIL;
	}

	/* Process based on record type */
	switch (record_type) {
	case IHEX_DATA_RECORD: {
		uint32_t abs_addr = *base_addr + addr16;
		int rc = cb(abs_addr, data_buf, byte_count, ctx);

		if (rc != 0) {
			return HEX_PARSE_OK; /* Callback aborted */
		}
		break;
	}

	case IHEX_EOF_RECORD:
		return HEX_PARSE_EOF;

	case IHEX_EXTENDED_LINEAR_ADDR: {
		/* Upper 16 bits of address */
		uint16_t upper;

		if (byte_count < 2) {
			return HEX_PARSE_LINE_ERR;
		}
		upper = ((uint16_t)data_buf[0] << 8) | data_buf[1];
		*base_addr = (uint32_t)upper << 16;
		break;
	}

	default:
		/* Ignore unsupported record types */
		break;
	}

	return HEX_PARSE_OK;
}

hex_parse_result_t intelhex_parse(const uint8_t *buf, uint32_t len,
				  hex_data_cb_t cb, void *ctx)
{
	uint32_t base_addr = 0;
	uint32_t line_start = 0;
	hex_parse_result_t result = HEX_PARSE_OK;

	for (uint32_t i = 0; i < len; i++) {
		/* Find line boundaries */
		if (buf[i] == '\n' || buf[i] == '\r') {
			uint32_t line_len = i - line_start;

			if (line_len > 0) {
				result = parse_record(&buf[line_start], line_len,
						      &base_addr, cb, ctx);
				if (result != HEX_PARSE_OK) {
					return result;
				}
			}
			/* Skip consecutive line terminators */
			while (i < len && (buf[i] == '\n' || buf[i] == '\r')) {
				i++;
			}
			line_start = i;
			i--; /* For loop will increment */
		}
	}

	/* Handle last line if no trailing newline */
	if (line_start < len) {
		result = parse_record(&buf[line_start], len - line_start,
				      &base_addr, cb, ctx);
	}

	return result;
}

bool intelhex_is_hex_file(const char *filename)
{
	size_t len = strlen(filename);

	if (len < 4) {
		return false;
	}
	const char *ext = &filename[len - 4];

	return (ext[0] == '.' &&
		(ext[1] == 'h' || ext[1] == 'H') &&
		(ext[2] == 'e' || ext[2] == 'E') &&
		(ext[3] == 'x' || ext[3] == 'X'));
}
