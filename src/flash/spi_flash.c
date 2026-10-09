/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Probe-side SPI NOR flash driver (JEDEC + SFDP).
 */

#include <zephyr/kernel.h>
#include <string.h>

#include "spi_flash.h"
#include "sfdp.h"

#if defined(CONFIG_DAPLINK_SPI_FLASH)

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_flash);

/* JEDEC command set */
#define CMD_WREN   0x06U
#define CMD_RDSR1  0x05U
#define CMD_READ   0x03U
#define CMD_PP     0x02U
#define CMD_SE     0x20U
#define CMD_CE     0xC7U
#define CMD_RDID   0x9FU
#define CMD_SFDP   0x5AU

#define SFDP_MAGIC 0x50444653U /* "SFDP" little-endian */

#define SPIFLASH_MAX_XFER 512U
#define SFDP_IMAGE_MAX    512U

static const struct spi_dt_spec spiflash =
	SPI_DT_SPEC_GET(DT_NODELABEL(spiflash), SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);

static struct spi_flash_info info;
static bool initialised;

static bool tx(const uint8_t *buf, uint32_t len)
{
	struct spi_buf txb = { .buf = (void *)buf, .len = len };
	struct spi_buf_set set = { .buffers = &txb, .count = 1 };

	return spi_write(spiflash.bus, &spiflash.config, &set) == 0;
}

static bool transfer(const uint8_t *buf_tx, uint8_t *buf_rx, uint32_t len)
{
	struct spi_buf txb = { .buf = (void *)buf_tx, .len = len };
	struct spi_buf rxb = { .buf = buf_rx, .len = len };
	struct spi_buf_set txs = { .buffers = &txb, .count = 1 };
	struct spi_buf_set rxs = { .buffers = &rxb, .count = 1 };

	return spi_transceive(spiflash.bus, &spiflash.config, &txs, &rxs) == 0;
}

static bool wait_ready(void)
{
	uint8_t cmd = CMD_RDSR1;
	uint8_t status = 0;

	for (uint32_t i = 0; i < 1000000U; i++) {
		if (!transfer(&cmd, &status, 1U)) {
			return false;
		}
		if (!(status & 0x01U)) {
			return true;
		}
		k_msleep(1);
	}
	return false;
}

static bool write_enable(void)
{
	uint8_t cmd = CMD_WREN;

	return tx(&cmd, 1U);
}

static bool read_jedec(uint32_t *id)
{
	uint8_t buf_tx[4] = { CMD_RDID, 0, 0, 0 };
	uint8_t buf_rx[4] = { 0 };

	if (!transfer(buf_tx, buf_rx, 4U)) {
		return false;
	}
	*id = ((uint32_t)buf_rx[1] << 16) | ((uint32_t)buf_rx[2] << 8) | buf_rx[3];
	return true;
}

static bool read_sfdp(uint32_t addr, uint8_t *buf, uint32_t len)
{
	uint8_t buf_tx[5U + SPIFLASH_MAX_XFER];
	uint8_t buf_rx[5U + SPIFLASH_MAX_XFER];

	if (len > SPIFLASH_MAX_XFER) {
		len = SPIFLASH_MAX_XFER;
	}
	buf_tx[0] = CMD_SFDP;
	buf_tx[1] = (uint8_t)(addr >> 16);
	buf_tx[2] = (uint8_t)(addr >> 8);
	buf_tx[3] = (uint8_t)addr;
	buf_tx[4] = 0; /* dummy */
	memset(&buf_tx[5], 0, len);

	if (!transfer(buf_tx, buf_rx, 5U + len)) {
		return false;
	}
	memcpy(buf, &buf_rx[5], len);
	return true;
}

static bool parse_sfdp(void)
{
	static uint8_t image[SFDP_IMAGE_MAX];
	struct sfdp_params params;

	if (!read_sfdp(0U, image, sizeof(image))) {
		return false;
	}
	if (!sfdp_parse(image, sizeof(image), &params)) {
		return false;
	}
	info.capacity = params.capacity;
	info.sector_size = params.sector_size;
	info.page_size = params.page_size;
	info.sector_erase_opcode = params.sector_erase_opcode;
	return true;
}

int spi_flash_init(void)
{
	if (initialised) {
		return 0;
	}
	if (!spi_is_ready_dt(&spiflash)) {
		LOG_ERR("SPI flash bus not ready");
		return -ENODEV;
	}

	memset(&info, 0, sizeof(info));
	if (!read_jedec(&info.jedec_id)) {
		LOG_ERR("JEDEC ID read failed");
		return -EIO;
	}
	LOG_INF("SPI flash JEDEC ID 0x%06X", info.jedec_id);

	/* Sensible defaults, overridden by SFDP when available. */
	info.sector_size = 0x1000U;
	info.page_size = 256U;
	info.sector_erase_opcode = CMD_SE;

	if (parse_sfdp()) {
		LOG_INF("SFDP: capacity %u bytes, sector %u, page %u, SE opcode 0x%02X",
			info.capacity, info.sector_size, info.page_size, info.sector_erase_opcode);
	} else {
		LOG_WRN("SFDP unavailable; using defaults");
	}
	info.present = true;
	initialised = true;
	return 0;
}

bool spi_flash_get_info(struct spi_flash_info *out)
{
	if (!initialised) {
		return false;
	}
	*out = info;
	return true;
}

bool spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	while (len != 0U) {
		uint32_t chunk = len;

		if (chunk > SPIFLASH_MAX_XFER) {
			chunk = SPIFLASH_MAX_XFER;
		}

		uint8_t buf_tx[4U + SPIFLASH_MAX_XFER];
		uint8_t buf_rx[4U + SPIFLASH_MAX_XFER];

		buf_tx[0] = CMD_READ;
		buf_tx[1] = (uint8_t)(addr >> 16);
		buf_tx[2] = (uint8_t)(addr >> 8);
		buf_tx[3] = (uint8_t)addr;
		memset(&buf_tx[4], 0, chunk);

		if (!transfer(buf_tx, buf_rx, 4U + chunk)) {
			return false;
		}
		memcpy(buf, &buf_rx[4], chunk);
		buf += chunk;
		addr += chunk;
		len -= chunk;
	}
	return true;
}

bool spi_flash_erase_sector(uint32_t addr)
{
	uint8_t buf[4];

	if (!write_enable() || !wait_ready()) {
		return false;
	}
	buf[0] = info.sector_erase_opcode;
	buf[1] = (uint8_t)(addr >> 16);
	buf[2] = (uint8_t)(addr >> 8);
	buf[3] = (uint8_t)addr;
	return tx(buf, 4U) && wait_ready();
}

bool spi_flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
	while (len != 0U) {
		uint32_t chunk = info.page_size - (addr % info.page_size);

		if (chunk > len) {
			chunk = len;
		}

		uint8_t buf[4U + SPIFLASH_MAX_XFER];

		buf[0] = CMD_PP;
		buf[1] = (uint8_t)(addr >> 16);
		buf[2] = (uint8_t)(addr >> 8);
		buf[3] = (uint8_t)addr;
		memcpy(&buf[4], data, chunk);

		if (!write_enable() || !tx(buf, 4U + chunk) || !wait_ready()) {
			return false;
		}
		addr += chunk;
		data += chunk;
		len -= chunk;
	}
	return true;
}

bool spi_flash_erase_chip(void)
{
	uint8_t cmd = CMD_CE;

	return write_enable() && tx(&cmd, 1U) && wait_ready();
}

#endif /* CONFIG_DAPLINK_SPI_FLASH */
