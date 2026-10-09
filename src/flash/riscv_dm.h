/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V Debug Module (DM) — stage 2.
 *
 * Enumerates harts and provides halt/resume/reset over the DMI transport
 * (riscv_jtag_dtm.c). Ported from the Black Magic Debug riscv_debug.c DM
 * logic. Abstract commands / memory access (stage 3) are not implemented.
 */

#ifndef RISCV_DM_H_
#define RISCV_DM_H_

#include <stdint.h>
#include <stdbool.h>

/* Activate the DM and enumerate harts. Returns the hart count or negative. */
int riscv_dm_init(void);

uint32_t riscv_dm_hart_count(void);

/* Per-hart control (index into the enumerated hart list). */
int riscv_hart_halt(uint32_t index);
int riscv_hart_resume(uint32_t index);
int riscv_hart_reset(uint32_t index);

/* Abstract command register access (regno: GPR = 0x1000+n, CSR = csr number). */
bool riscv_reg_read(uint32_t index, uint16_t regno, uint32_t *val);
bool riscv_reg_write(uint32_t index, uint16_t regno, uint32_t val);

/* System Bus Access memory access. */
bool riscv_mem_read32(uint32_t index, uint32_t addr, uint32_t *val);
bool riscv_mem_write32(uint32_t index, uint32_t addr, uint32_t val);
bool riscv_mem_read16(uint32_t index, uint32_t addr, uint16_t *val);
bool riscv_mem_write16(uint32_t index, uint32_t addr, uint16_t val);

#endif /* RISCV_DM_H_ */
