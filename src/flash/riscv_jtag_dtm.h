/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V JTAG Debug Transport Module (DTM / DMI) — stage 1.
 *
 * Implements the RISC-V JTAG DTM over the probe's JTAG bit-bang primitives:
 * TAP navigation, the DTMCS register and DMI transfers. Ported from the
 * Black Magic Debug riscv_jtag_dtm.c, reduced to a single TAP on the chain.
 *
 * This provides the transport only; the Debug Module and harts (stage 2+)
 * are not implemented here.
 */

#ifndef RISCV_JTAG_DTM_H_
#define RISCV_JTAG_DTM_H_

#include <stdint.h>
#include <stdbool.h>

/* Reset the TAP, discover the DTM and switch to DMI access. */
int riscv_jtag_dtm_init(void);

/* Read the JTAG chain IDCODE (the TAP must be reset first). */
uint32_t riscv_jtag_idcode(void);

/* DMI (Debug Module Interface) 32-bit register access. */
bool riscv_dmi_read(uint32_t addr, uint32_t *val);
bool riscv_dmi_write(uint32_t addr, uint32_t val);

/* DMI backend selection (JTAG DTM by default; ADI/gateway DTMs install their
 * own accessors). */
typedef bool (*riscv_dmi_read_fn)(uint32_t addr, uint32_t *val);
typedef bool (*riscv_dmi_write_fn)(uint32_t addr, uint32_t val);
void riscv_dmi_set_backend(riscv_dmi_read_fn read_fn, riscv_dmi_write_fn write_fn);

#endif /* RISCV_JTAG_DTM_H_ */
