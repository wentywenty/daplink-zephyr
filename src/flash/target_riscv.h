/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V debug/detect integration.
 *
 * Uses the JTAG DTM/DMI + Debug Module layers to identify a RISC-V target
 * and hand back a target_cfg_t for the flash manager. Currently supports the
 * GD32VF103 (STM32F1-compatible flash controller reached over the RISC-V
 * system bus).
 */

#ifndef TARGET_RISCV_H_
#define TARGET_RISCV_H_

#include <stdint.h>
#include <stdbool.h>

#include "flash_blob.h"

/*
 * Detect a RISC-V target over JTAG and return its configuration, or NULL if
 * none is recognised. On success the flash memory backend is switched to the
 * RISC-V system bus.
 */
const target_cfg_t *riscv_detect(void);

#endif /* TARGET_RISCV_H_ */
