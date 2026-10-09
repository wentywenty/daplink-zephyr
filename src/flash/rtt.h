/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SEGGER RTT (Real Time Transfer) support for the CMSIS-DAP probe.
 *
 * Discovers the target's RTT control block in RAM over SWD and exposes its
 * up/down ring buffers through CMSIS-DAP vendor commands (see dap_vendor.c).
 * The discovery and buffer access model follows the Black Magic Debug RTT
 * implementation (src/rtt.c), adapted to the probe-side swd_host API.
 */

#ifndef RTT_H_
#define RTT_H_

#include <stdint.h>
#include <stdbool.h>

/* Discover (or re-discover) the RTT control block. Returns 0 on success. */
int rtt_scan(void);

/* Fill discovered state; cb==NULL clears. Returns 0 if found. */
int rtt_info(bool *found, uint32_t *num_up, uint32_t *num_down, uint32_t *cb);

/* Read from an RTT up (target->host) channel. */
int rtt_read(uint32_t chan, uint8_t *out, uint32_t max, uint32_t *nread);

/* Write to an RTT down (host->target) channel. */
int rtt_write(uint32_t chan, const uint8_t *in, uint32_t len, uint32_t *nwritten);

#endif /* RTT_H_ */
