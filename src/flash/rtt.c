/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SEGGER RTT control block discovery and ring-buffer access over SWD.
 */

#include <zephyr/kernel.h>
#include <string.h>

#include "rtt.h"
#include "swd_host.h"
#include "target_detect.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(rtt);

#define RTT_MAGIC      "SEGGER RTT"
#define RTT_MAGIC_LEN  16U
#define RTT_SCAN_CHUNK 256U

/* SEGGER_RTT_CB header: acID[16], MaxNumUpBuffers, MaxNumDownBuffers */
#define RTT_CB_HEADER_SIZE 24U
/* SEGGER_RTT_BUFFER: sName, pBuffer, Size, WrOff, RdOff, Flags */
#define RTT_BUFFER_ENTRY_SIZE 24U
#define RTT_UP_OFFSET(cb, i)   (cb + RTT_CB_HEADER_SIZE + (i) * RTT_BUFFER_ENTRY_SIZE)
#define RTT_DOWN_OFFSET(cb, i, up) \
	(cb + RTT_CB_HEADER_SIZE + (up) * RTT_BUFFER_ENTRY_SIZE + (i) * RTT_BUFFER_ENTRY_SIZE)
#define RTT_DESC_SIZE_OFF   8U
#define RTT_DESC_WROFF_OFF  12U
#define RTT_DESC_RDOFF_OFF  16U

static uint32_t rtt_cb;
static bool rtt_found;
static uint32_t rtt_num_up;
static uint32_t rtt_num_down;

static int rtt_read_u32(uint32_t addr, uint32_t *val)
{
	return swd_read_word(addr, val) ? 0 : -1;
}

static int rtt_write_u32(uint32_t addr, uint32_t val)
{
	return swd_write_word(addr, val) ? 0 : -1;
}

/* Return true if "SEGGER RTT" starts at addr. */
static bool rtt_has_magic(uint32_t addr)
{
	uint8_t buf[RTT_MAGIC_LEN];

	if (!swd_read_memory(addr, buf, RTT_MAGIC_LEN)) {
		return false;
	}
	return memcmp(buf, RTT_MAGIC, sizeof(RTT_MAGIC) - 1U) == 0;
}

static bool rtt_scan_region(uint32_t start, uint32_t end)
{
	uint8_t buf[RTT_SCAN_CHUNK];

	if (end <= start || end - start < RTT_MAGIC_LEN) {
		return false;
	}

	for (uint32_t addr = start; addr + RTT_MAGIC_LEN <= end; addr += RTT_SCAN_CHUNK - RTT_MAGIC_LEN) {
		uint32_t chunk = end - addr;

		if (chunk > sizeof(buf)) {
			chunk = sizeof(buf);
		}
		if (!swd_read_memory(addr, buf, chunk)) {
			return false;
		}

		for (uint32_t i = 0; i + RTT_MAGIC_LEN <= chunk; i += 4U) {
			if (memcmp(&buf[i], RTT_MAGIC, sizeof(RTT_MAGIC) - 1U) == 0) {
				if (rtt_has_magic(addr + i)) {
					rtt_cb = addr + i;
					return true;
				}
			}
		}
	}
	return false;
}

int rtt_scan(void)
{
	const target_cfg_t *target;

	rtt_found = false;
	rtt_cb = 0;

	if (swd_init_debug() != 0) {
		LOG_ERR("RTT: SWD init failed");
		return -EIO;
	}

	target = target_detect();
	if (target == NULL) {
		LOG_ERR("RTT: no target detected");
		return -ENODEV;
	}

	for (int i = 0; i < TARGET_MAX_RAM_REGIONS; i++) {
		const uint32_t start = target->ram_regions[i].start;
		const uint32_t end = target->ram_regions[i].end;

		if (end <= start) {
			continue;
		}
		if (rtt_scan_region(start, end)) {
			uint32_t nup = 0;
			uint32_t ndown = 0;

			rtt_read_u32(rtt_cb + 16U, &nup);
			rtt_read_u32(rtt_cb + 20U, &ndown);
			rtt_num_up = nup;
			rtt_num_down = ndown;
			rtt_found = true;
			LOG_INF("RTT: control block @0x%08X (%u up, %u down)", rtt_cb,
				rtt_num_up, rtt_num_down);
			return 0;
		}
	}

	LOG_WRN("RTT: control block not found");
	return -ENOENT;
}

int rtt_info(bool *found, uint32_t *num_up, uint32_t *num_down, uint32_t *cb)
{
	if (found != NULL) {
		*found = rtt_found;
	}
	if (num_up != NULL) {
		*num_up = rtt_num_up;
	}
	if (num_down != NULL) {
		*num_down = rtt_num_down;
	}
	if (cb != NULL) {
		*cb = rtt_cb;
	}
	return rtt_found ? 0 : -ENOENT;
}

int rtt_read(uint32_t chan, uint8_t *out, uint32_t max, uint32_t *nread)
{
	uint32_t desc = RTT_UP_OFFSET(rtt_cb, chan);
	uint32_t pbuf, size, wroff, rdoff;

	*nread = 0;
	if (!rtt_found || chan >= rtt_num_up) {
		return -EINVAL;
	}

	if (rtt_read_u32(desc + 4U, &pbuf) != 0 ||
	    rtt_read_u32(desc + RTT_DESC_SIZE_OFF, &size) != 0 ||
	    rtt_read_u32(desc + RTT_DESC_WROFF_OFF, &wroff) != 0 ||
	    rtt_read_u32(desc + RTT_DESC_RDOFF_OFF, &rdoff) != 0) {
		return -EIO;
	}
	if (size == 0U || wroff == rdoff) {
		return 0;
	}

	uint32_t avail = (wroff > rdoff) ? (wroff - rdoff) : (size - rdoff);

	if (avail > max) {
		avail = max;
	}
	if (avail == 0U) {
		return 0;
	}
	if (!swd_read_memory(pbuf + rdoff, out, avail)) {
		return -EIO;
	}

	rdoff = (rdoff + avail) % size;
	if (rtt_write_u32(desc + RTT_DESC_RDOFF_OFF, rdoff) != 0) {
		return -EIO;
	}
	*nread = avail;
	return 0;
}

int rtt_write(uint32_t chan, const uint8_t *in, uint32_t len, uint32_t *nwritten)
{
	uint32_t desc = RTT_DOWN_OFFSET(rtt_cb, chan, rtt_num_up);
	uint32_t pbuf, size, wroff, rdoff;

	*nwritten = 0;
	if (!rtt_found || chan >= rtt_num_down) {
		return -EINVAL;
	}

	if (rtt_read_u32(desc + 4U, &pbuf) != 0 ||
	    rtt_read_u32(desc + RTT_DESC_SIZE_OFF, &size) != 0 ||
	    rtt_read_u32(desc + RTT_DESC_WROFF_OFF, &wroff) != 0 ||
	    rtt_read_u32(desc + RTT_DESC_RDOFF_OFF, &rdoff) != 0) {
		return -EIO;
	}
	if (size == 0U) {
		return -EINVAL;
	}

	uint32_t free = (wroff >= rdoff) ? (size - 1U - (wroff - rdoff)) : (rdoff - wroff - 1U);

	if (free > len) {
		free = len;
	}
	if (free == 0U) {
		return 0;
	}

	/* Write up to the end of the ring, then wrap. */
	uint32_t first = size - wroff;

	if (first > free) {
		first = free;
	}
	if (!swd_write_memory(pbuf + wroff, in, first)) {
		return -EIO;
	}
	if (free > first) {
		if (!swd_write_memory(pbuf, in + first, free - first)) {
			return -EIO;
		}
	}

	wroff = (wroff + free) % size;
	if (rtt_write_u32(desc + RTT_DESC_WROFF_OFF, wroff) != 0) {
		return -EIO;
	}
	*nwritten = free;
	return 0;
}
