/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SWD Host Interface - High-level SWD operations via DAP_ExecuteCommand()
 */

#ifndef SWD_HOST_H_
#define SWD_HOST_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include "flash_blob.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Global mutex serializing all access to the shared SWD bus. Both the HID
 * CMSIS-DAP command path and the drag-and-drop flash path must hold this
 * while driving SWD/JTAG pins or the shared DAP_Daten state.
 */
extern struct k_mutex swd_bus_mutex;

/**
 * Initialize SWD debug connection to target.
 * Connects via SWJ, powers up debug, halts target.
 * @return 0 on success, negative errno on failure
 */
int swd_init_debug(void);

/**
 * Disconnect SWD and release target.
 */
void swd_off(void);

/**
 * Read a 32-bit word from target memory.
 */
bool swd_read_word(uint32_t addr, uint32_t *val);

/**
 * Write a 32-bit word to target memory.
 */
bool swd_write_word(uint32_t addr, uint32_t val);

/**
 * Write a 16-bit halfword to target memory (used for STM32F1 flash).
 */
bool swd_write_halfword(uint32_t addr, uint16_t val);

/**
 * Read a 16-bit halfword from target memory.
 */
bool swd_read_halfword(uint32_t addr, uint16_t *val);

/**
 * Raw AP register access (with AP read-buffer flush), for DTMs that expose
 * registers rather than memory-mapped access (e.g. the RISC-V gateway DTM).
 */
bool swd_read_ap_reg(uint32_t addr, uint32_t *val);
bool swd_write_ap_reg(uint32_t addr, uint32_t val);

/**
 * Read a block of memory from target.
 * @param addr  Target address (must be word-aligned)
 * @param data  Buffer to read into
 * @param size  Number of bytes to read
 */
bool swd_read_memory(uint32_t addr, uint8_t *data, uint32_t size);

/**
 * Write a block of memory to target.
 * @param addr  Target address (must be word-aligned)
 * @param data  Data to write
 * @param size  Number of bytes to write
 */
bool swd_write_memory(uint32_t addr, const uint8_t *data, uint32_t size);

/**
 * Read a core register from target.
 * @param n    Register number (e.g., 0-15 for R0-R15, 16 for xPSR)
 * @param val  Pointer to store register value
 */
bool swd_read_core_register(uint32_t n, uint32_t *val);

/**
 * Write a core register on target.
 * @param n    Register number
 * @param val  Value to write
 */
bool swd_write_core_register(uint32_t n, uint32_t val);

/**
 * Execute a flash algorithm function on target.
 * Sets up registers, starts execution, waits for halt at breakpoint.
 *
 * @param sys   System call parameters (BKPT, RSB, RSP)
 * @param entry Function entry address
 * @param arg1  Argument 1 (R0)
 * @param arg2  Argument 2 (R1)
 * @return true on success (R0 == 0), false on failure or timeout
 */
bool swd_flash_syscall_exec(const program_syscall_t *sys,
			    uint32_t entry, uint32_t arg1, uint32_t arg2);

/**
 * Set target reset state.
 * @param state  TARGET_RESET_HOLD, TARGET_RESET_RUN, TARGET_RESET_HALT, etc.
 */
bool swd_set_target_state(uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* SWD_HOST_H_ */
