/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SWO ITM software decoder (see swo_itm.h).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include "swo_itm.h"

#if defined(CONFIG_DAPLINK_SWO_ITM)

RING_BUF_DECLARE(swo_itm_ring, 2048U);

static uint32_t itm_mask;
static uint8_t itm_packet_length;
static bool itm_decode_packet;

void swo_itm_feed(const uint8_t *data, uint16_t len)
{
	for (uint16_t idx = 0; idx < len; idx++) {
		if (itm_packet_length == 0U) {
			/* A valid ITM header has bit2 clear and a non-zero size field. */
			if ((data[idx] & 0x04U) == 0U && (data[idx] & 0x03U) != 0U) {
				const uint8_t stream = data[idx] >> 3U;

				itm_packet_length = (uint8_t)(1U << ((data[idx] & 0x03U) - 1U));
				itm_decode_packet = (itm_mask & (1U << stream)) != 0U;
			} else {
				itm_decode_packet = false;
			}
		} else {
			if (itm_decode_packet) {
				(void)ring_buf_put(&swo_itm_ring, &data[idx], 1U);
			}
			itm_packet_length--;
		}
	}
}

void swo_itm_set_mask(uint32_t mask)
{
	itm_mask = mask;
}

uint32_t swo_itm_get_mask(void)
{
	return itm_mask;
}

void swo_itm_reset(void)
{
	ring_buf_reset(&swo_itm_ring);
	itm_packet_length = 0U;
	itm_decode_packet = false;
}

uint32_t swo_itm_read(uint8_t *buf, uint32_t max)
{
	return ring_buf_get(&swo_itm_ring, buf, max);
}

uint32_t swo_itm_count(void)
{
	return ring_buf_size_get(&swo_itm_ring);
}

#endif /* CONFIG_DAPLINK_SWO_ITM */
