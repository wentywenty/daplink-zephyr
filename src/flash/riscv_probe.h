/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V vendor probe dispatch.
 *
 * target_riscv.c brings up a DTM (JTAG / ADI / gateway), the Debug Module and
 * the system bus, then offers control to the vendor probes below. Each vendor
 * probe reads its chip ID (typically via the system bus) and returns a
 * target_cfg_t or NULL. Probes are weak so uncompiled ones are skipped.
 */

#ifndef RISCV_PROBE_H_
#define RISCV_PROBE_H_

#include <stdint.h>

#include "flash_blob.h"

typedef const target_cfg_t *(*riscv_target_probe_fn)(void);

/* GigaDevice GD32VF103 (STM32F1-compatible flash controller). */
extern const target_cfg_t *gd32vf1_probe(void) __attribute__((weak));

#endif /* RISCV_PROBE_H_ */
