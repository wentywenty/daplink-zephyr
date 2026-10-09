/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V ADI (MEM-AP) DTM — stage 5.
 *
 * Some parts expose the RISC-V Debug Module through an ADIv5 MEM-AP rather
 * than a JTAG DTM (the DMI is memory-mapped into the AP's address space).
 * This backend drives such a DMI over SWD via the 32-bit AP memory access.
 */

#ifndef RISCV_ADI_DTM_H_
#define RISCV_ADI_DTM_H_

/* Connect over SWD and install the ADI DMI backend. Returns 0 on success. */
int riscv_adi_dtm_init(void);

#endif /* RISCV_ADI_DTM_H_ */
