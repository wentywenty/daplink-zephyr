/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Target chip auto-detection via SWD IDCODE.
 * Reads DP IDCODE and STM32 DBGMCU_IDCODE to identify the connected MCU
 * and return the matching target_cfg_t.
 */

#ifndef TARGET_DETECT_H_
#define TARGET_DETECT_H_

#include "flash_blob.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Auto-detect connected target MCU via SWD.
 * Establishes SWD debug connection, reads chip identification registers,
 * and returns the matching target configuration.
 *
 * Returns: pointer to target_cfg_t on success, NULL on failure.
 *          Caller must NOT free the returned pointer.
 */
const target_cfg_t *target_detect(void);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_DETECT_H_ */
