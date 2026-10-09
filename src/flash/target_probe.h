/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Target probe dispatch framework.
 *
 * Each vendor/target family provides a probe function that is offered the
 * CoreSight DP IDCODE and returns its target_cfg_t (or NULL if it does not
 * recognise the part). target_detect() walks the registry in order.
 *
 * Probes are declared __attribute__((weak)): a driver that is not compiled
 * in resolves to a NULL pointer and is skipped, so the registry does not need
 * per-driver #if guards. This mirrors the Black Magic Debug probe dispatch.
 */

#ifndef TARGET_PROBE_H_
#define TARGET_PROBE_H_

#include <stdint.h>

#include "flash_blob.h"

typedef const target_cfg_t *(*target_probe_fn)(uint32_t dp_idcode);

/* STM32 family (DBGMCU DEV_ID based). Defined by target_stm32_detect.c. */
extern const target_cfg_t *stm32_probe(uint32_t dp_idcode) __attribute__((weak));

/* nRF / LPC (DP IDCODE based). Defined by target_simple.c. */
extern const target_cfg_t *nrf_probe(uint32_t dp_idcode) __attribute__((weak));
extern const target_cfg_t *lpc_probe(uint32_t dp_idcode) __attribute__((weak));

/* RISC-V (JTAG/ADI/gateway DTM). Defined by target_riscv.c. */
extern const target_cfg_t *riscv_probe(uint32_t dp_idcode) __attribute__((weak));

#endif /* TARGET_PROBE_H_ */
