/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flash memory-access backend abstraction.
 *
 * The native flash drivers need 32-bit target memory access. On ARM Cortex-M
 * this goes through the SWD MEM-AP; on RISC-V it goes through the Debug
 * Module's System Bus Access. This layer lets the same flash drivers run on
 * either transport.
 */

#ifndef FLASH_MEM_H_
#define FLASH_MEM_H_

#include <stdint.h>
#include <stdbool.h>

/* Select the memory backend. enable=false -> SWD (default). */
void flash_mem_set_backend_sba(bool enable, uint32_t hart);

bool flash_mem_read32(uint32_t addr, uint32_t *val);
bool flash_mem_write32(uint32_t addr, uint32_t val);
bool flash_mem_read16(uint32_t addr, uint16_t *val);
bool flash_mem_write16(uint32_t addr, uint16_t val);

/* Read a block of target memory into data (word-aligned). */
bool flash_mem_read(uint32_t addr, uint8_t *data, uint32_t size);

#endif /* FLASH_MEM_H_ */
