/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native (register-level) Nordic nRF51/nRF52 flash programming.
 *
 * Programs the NVMC controller directly over SWD. Ported from the Black
 * Magic Debug nRF51 driver (src/target/nrf51.c).
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <errno.h>

#include "flash_blob.h"
#include "swd_host.h"
#include "flash_mem.h"

#define swd_read_word  flash_mem_read32
#define swd_write_word flash_mem_write32

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(flash_native_nrf);

#define NRF_POLL_LIMIT 2000000U

#define NRF_NVMC_BASE     0x4001E000U
#define NRF_NVMC_READY    (NRF_NVMC_BASE + 0x400U)
#define NRF_NVMC_CONFIG   (NRF_NVMC_BASE + 0x504U)
#define NRF_NVMC_ERASEPAGE (NRF_NVMC_BASE + 0x508U)
#define NRF_NVMC_ERASEALL (NRF_NVMC_BASE + 0x50CU)

#define NRF_NVMC_CONFIG_REN 0x0U
#define NRF_NVMC_CONFIG_WEN 0x1U
#define NRF_NVMC_CONFIG_EEN 0x2U

static int nrf_wait_ready(void)
{
	uint32_t ready = 0;

	for (uint32_t i = 0; i < NRF_POLL_LIMIT; i++) {
		if (!swd_read_word(NRF_NVMC_READY, &ready)) {
			return -EIO;
		}
		if (ready != 0U) {
			return 0;
		}
	}
	return -ETIMEDOUT;
}

static int nrf_set_config(uint32_t config)
{
	if (!swd_write_word(NRF_NVMC_CONFIG, config)) {
		return -EIO;
	}
	return nrf_wait_ready();
}

static int nrf52_native_erase(const target_cfg_t *cfg, uint32_t addr)
{
	int rc;

	(void)cfg;
	rc = nrf_set_config(NRF_NVMC_CONFIG_EEN);
	if (rc != 0) {
		return rc;
	}
	if (!swd_write_word(NRF_NVMC_ERASEPAGE, addr)) {
		(void)nrf_set_config(NRF_NVMC_CONFIG_REN);
		return -EIO;
	}
	rc = nrf_wait_ready();
	(void)nrf_set_config(NRF_NVMC_CONFIG_REN);
	return rc;
}

static int nrf52_native_program(const target_cfg_t *cfg, uint32_t addr, const uint8_t *data, uint32_t size)
{
	int rc;

	(void)cfg;
	rc = nrf_set_config(NRF_NVMC_CONFIG_WEN);
	if (rc != 0) {
		return rc;
	}

	/* NVMC programs whole 32-bit words; pad the tail with the erased value. */
	for (uint32_t off = 0; off < size; off += 4U) {
		uint32_t word = 0xFFFFFFFFU;
		uint32_t n = size - off;

		if (n > 4U) {
			n = 4U;
		}
		memcpy(&word, data + off, n);

		if (!swd_write_word(addr + off, word)) {
			(void)nrf_set_config(NRF_NVMC_CONFIG_REN);
			return -EIO;
		}
	}

	rc = nrf_wait_ready();
	(void)nrf_set_config(NRF_NVMC_CONFIG_REN);
	return rc;
}

static int nrf52_native_mass_erase(const target_cfg_t *cfg)
{
	int rc;

	(void)cfg;
	rc = nrf_set_config(NRF_NVMC_CONFIG_EEN);
	if (rc != 0) {
		return rc;
	}
	if (!swd_write_word(NRF_NVMC_ERASEALL, 1U)) {
		(void)nrf_set_config(NRF_NVMC_CONFIG_REN);
		return -EIO;
	}
	rc = nrf_wait_ready();
	(void)nrf_set_config(NRF_NVMC_CONFIG_REN);
	return rc;
}

const flash_native_ops_t flash_native_nrf52 = {
	.prepare = NULL,
	.erase_sector = nrf52_native_erase,
	.program = nrf52_native_program,
	.mass_erase = nrf52_native_mass_erase,
};
