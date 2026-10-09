/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RISC-V "gateway" DTM — stage 5b.
 *
 * The SpinalHDL SWD DTM used by VexRiscv/VexiiRiscv exposes the DMI through
 * a custom ADIv5 AP: AP IDR = 0x74726976 ("triv"), DMI_ADDR at 0x04 and
 * DMI_DATA at 0x08 (bank 0, APBANKSEL ignored). Modeled on the Black Magic
 * Debug riscv_gateway_dtm.c.
 */

#ifndef RISCV_GATEWAY_DTM_H_
#define RISCV_GATEWAY_DTM_H_

/* Detect the gateway AP over SWD and install it as the DMI backend. */
int riscv_gateway_dtm_init(void);

#endif /* RISCV_GATEWAY_DTM_H_ */
