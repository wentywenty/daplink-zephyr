/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Intel HEX format parser for drag-and-drop firmware flashing.
 * Supports IHEX record types: Data (00), EOF (01), Extended Linear Address (04).
 * Simplified port from DAPLink source/daplink/drag-n-drop/intelhex.c.
 */

#ifndef INTELHEX_H_
#define INTELHEX_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IHEX parse result */
typedef enum {
	HEX_PARSE_OK       = 0,
	HEX_PARSE_EOF      = 1,
	HEX_PARSE_CKSUM_FAIL = -1,
	HEX_PARSE_UNALIGNED  = -2,
	HEX_PARSE_LINE_ERR   = -3,
} hex_parse_result_t;

/*
 * Callback invoked for each decoded data chunk from the HEX file.
 *
 * @param addr  Target address of this data chunk (absolute)
 * @param data  Pointer to decoded binary data
 * @param size  Number of bytes in this chunk
 * @param ctx   User-provided context pointer
 * @return 0 to continue parsing, non-zero to abort
 */
typedef int (*hex_data_cb_t)(uint32_t addr, const uint8_t *data,
			     uint32_t size, void *ctx);

/**
 * Parse an Intel HEX file from a memory buffer.
 * Calls the callback for each contiguous data block decoded.
 *
 * @param buf    HEX file content (null-terminated ASCII)
 * @param len    Length of buf in bytes
 * @param cb     Callback for decoded data blocks
 * @param ctx    User context passed to callback
 * @return HEX_PARSE_OK on success, HEX_PARSE_EOF if normal end,
 *         negative on error
 */
hex_parse_result_t intelhex_parse(const uint8_t *buf, uint32_t len,
				  hex_data_cb_t cb, void *ctx);

/**
 * Determine if a file extension indicates an Intel HEX file.
 *
 * @param filename  Filename to check (case-insensitive)
 * @return true if the filename ends with .hex
 */
bool intelhex_is_hex_file(const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* INTELHEX_H_ */
