/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SWO ITM (Instrumentation Trace Macrocell) software decoder.
 *
 * Decodes the ITM/SWIT packet stream carried on the SWO line (UART/async
 * encoded) into per-channel data, so the probe can present decoded printf
 * output without host-side decoding. Ported from the Black Magic Debug
 * swo_itm_decode.c.
 */

#ifndef SWO_ITM_H_
#define SWO_ITM_H_

#include <stdint.h>

/* Decode len captured SWO bytes, appending selected channels to the buffer. */
void swo_itm_feed(const uint8_t *data, uint16_t len);

/* Bitmask of ITM stimulus channels to keep. */
void swo_itm_set_mask(uint32_t mask);
uint32_t swo_itm_get_mask(void);

/* Discard decoder state and buffered decoded data. */
void swo_itm_reset(void);

/* Read decoded data into buf (up to max bytes); returns bytes read. */
uint32_t swo_itm_read(uint8_t *buf, uint32_t max);

/* Number of decoded bytes buffered. */
uint32_t swo_itm_count(void);

#endif /* SWO_ITM_H_ */
