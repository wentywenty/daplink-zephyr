/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flash memory-access backend abstraction (see flash_mem.h).
 */

#include <zephyr/kernel.h>

#include "flash_mem.h"
#include "swd_host.h"

#if defined(CONFIG_DAPLINK_RISCV) && defined(CONFIG_DAPLINK_JTAG)
#include "riscv_dm.h"
#endif

static bool use_sba;
static uint32_t sba_hart;

void flash_mem_set_backend_sba(bool enable, uint32_t hart)
{
	use_sba = enable;
	sba_hart = hart;
}

bool flash_mem_read32(uint32_t addr, uint32_t *val)
{
#if defined(CONFIG_DAPLINK_RISCV) && defined(CONFIG_DAPLINK_JTAG)
	if (use_sba) {
		return riscv_mem_read32(sba_hart, addr, val);
	}
#endif
	return swd_read_word(addr, val);
}

bool flash_mem_write32(uint32_t addr, uint32_t val)
{
#if defined(CONFIG_DAPLINK_RISCV) && defined(CONFIG_DAPLINK_JTAG)
	if (use_sba) {
		return riscv_mem_write32(sba_hart, addr, val);
	}
#endif
	return swd_write_word(addr, val);
}

bool flash_mem_read16(uint32_t addr, uint16_t *val)
{
#if defined(CONFIG_DAPLINK_RISCV) && defined(CONFIG_DAPLINK_JTAG)
	if (use_sba) {
		return riscv_mem_read16(sba_hart, addr, val);
	}
#endif
	return swd_read_halfword(addr, val);
}

bool flash_mem_write16(uint32_t addr, uint16_t val)
{
#if defined(CONFIG_DAPLINK_RISCV) && defined(CONFIG_DAPLINK_JTAG)
	if (use_sba) {
		return riscv_mem_write16(sba_hart, addr, val);
	}
#endif
	return swd_write_halfword(addr, val);
}

bool flash_mem_read(uint32_t addr, uint8_t *data, uint32_t size)
{
#if defined(CONFIG_DAPLINK_RISCV) && defined(CONFIG_DAPLINK_JTAG)
	if (use_sba) {
		uint32_t off = 0;

		while (off < size) {
			uint32_t word = 0;
			uint32_t n = size - off;

			if (!riscv_mem_read32(sba_hart, addr + off, &word)) {
				return false;
			}
			if (n > 4U) {
				n = 4U;
			}
			for (uint32_t i = 0; i < n; i++) {
				data[off + i] = (uint8_t)(word >> (8U * i));
			}
			off += 4U;
		}
		return true;
	}
#endif
	return swd_read_memory(addr, data, size);
}
