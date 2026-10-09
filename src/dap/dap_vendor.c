/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * CMSIS-DAP vendor command handler (overrides the weak default).
 *
 * Command 0x80 (ID_DAP_Vendor0) sub-commands:
 *
 *   RTT (CONFIG_DAPLINK_RTT):
 *     [0]=0x80 [1]=0x00            INFO  -> status,u8 found,u32 num_up,u32 num_down,u32 cb
 *     [0]=0x80 [1]=0x01            SCAN  -> status
 *     [0]=0x80 [1]=0x02 chan max   READ  -> status,u8 count,data...
 *     [0]=0x80 [1]=0x03 chan len data... WRITE -> status,u8 written
 *
 *   Option bytes (CONFIG_DAPLINK_OPTION_BYTES):
 *     [0]=0x80 [1]=0x10            OPT_READ  -> status,u32 opt0,u32 opt1
 *     [0]=0x80 [1]=0x11 idx u32val OPT_WRITE -> status
 *
 * status: 0 = DAP_OK, 1 = error, 0xFF = unknown command.
 */

#include <zephyr/kernel.h>

#include "DAP.h"
#include "DAP_config.h"
#if CONFIG_DAPLINK_RTT
#include "rtt.h"
#endif
#if CONFIG_DAPLINK_OPTION_BYTES
#include "flash_manager.h"
#endif
#if CONFIG_DAPLINK_SPI_FLASH
#include "spi_flash.h"
#endif
#if CONFIG_DAPLINK_SWO_ITM
#include "swo_itm.h"
#endif
#if CONFIG_DAPLINK_VENDOR_REBOOT
#include <zephyr/sys/reboot.h>

static void vendor_reboot_work_fn(struct k_work *work)
{
	(void)work;
	sys_reboot(SYS_REBOOT_WARM);
}

static K_WORK_DELAYABLE_DEFINE(vendor_reboot_work, vendor_reboot_work_fn);
#endif

#define VENDOR_CMD 0x80U

static void put_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

uint32_t DAP_ProcessVendorCommand(const uint8_t *request, uint8_t *response)
{
	uint32_t req_len = 2U;
	uint32_t resp_len = 1U;

	if (request[0] != VENDOR_CMD) {
		response[0] = 0xFFU;
		return (req_len << 16) | resp_len;
	}

	switch (request[1]) {
#if CONFIG_DAPLINK_RTT
	case 0x00: { /* RTT INFO */
		bool found = false;
		uint32_t up = 0;
		uint32_t down = 0;
		uint32_t cb = 0;

		rtt_info(&found, &up, &down, &cb);
		response[0] = DAP_OK;
		response[1] = found ? 1U : 0U;
		put_u32(&response[2], up);
		put_u32(&response[6], down);
		put_u32(&response[10], cb);
		resp_len = 14U;
		break;
	}
	case 0x01: { /* RTT SCAN */
		response[0] = (rtt_scan() == 0) ? DAP_OK : 0x01U;
		break;
	}
	case 0x02: { /* RTT READ */
		uint32_t max = request[3];
		uint32_t n = 0;
		int rc;

		if (max > DAP_PACKET_SIZE - 2U) {
			max = DAP_PACKET_SIZE - 2U;
		}
		rc = rtt_read(request[2], &response[2], max, &n);
		response[0] = (rc == 0) ? DAP_OK : 0x01U;
		response[1] = (uint8_t)n;
		resp_len = 2U + n;
		req_len = 4U;
		break;
	}
	case 0x03: { /* RTT WRITE */
		uint32_t len = request[3];
		uint32_t n = 0;
		int rc;

		if (len > DAP_PACKET_SIZE - 4U) {
			len = DAP_PACKET_SIZE - 4U;
		}
		rc = rtt_write(request[2], &request[4], len, &n);
		response[0] = (rc == 0) ? DAP_OK : 0x01U;
		response[1] = (uint8_t)n;
		resp_len = 2U;
		req_len = 4U + len;
		break;
	}
#endif /* CONFIG_DAPLINK_RTT */
#if CONFIG_DAPLINK_OPTION_BYTES
	case 0x10: { /* OPTION READ */
		uint32_t opt0 = 0;
		uint32_t opt1 = 0;
		int rc0 = flash_manager_option_read(0, &opt0);
		int rc1 = flash_manager_option_read(1, &opt1);

		response[0] = (rc0 == 0) ? DAP_OK : 0x01U;
		put_u32(&response[1], opt0);
		put_u32(&response[5], opt1);
		resp_len = 9U;
		req_len = 2U;
		break;
	}
	case 0x11: { /* OPTION WRITE: idx, u32 value */
		const uint32_t idx = request[2];
		const uint32_t val = (uint32_t)request[3] | ((uint32_t)request[4] << 8) |
				     ((uint32_t)request[5] << 16) | ((uint32_t)request[6] << 24);
		int rc = flash_manager_option_write(idx, val);

		response[0] = (rc == 0) ? DAP_OK : 0x01U;
		resp_len = 1U;
		req_len = 7U;
		break;
	}
#endif /* CONFIG_DAPLINK_OPTION_BYTES */
#if CONFIG_DAPLINK_VENDOR_REBOOT
	case 0x20: { /* REBOOT the probe */
		response[0] = DAP_OK;
		resp_len = 1U;
		req_len = 2U;
		/* Defer the reset so this response can be transmitted first. */
		k_work_schedule(&vendor_reboot_work, K_MSEC(100));
		break;
	}
#endif /* CONFIG_DAPLINK_VENDOR_REBOOT */
#if CONFIG_DAPLINK_SPI_FLASH
	case 0x30: { /* SPI FLASH INFO */
		struct spi_flash_info fi;

		if (spi_flash_init() != 0 || !spi_flash_get_info(&fi)) {
			response[0] = 0x01U;
			resp_len = 1U;
			break;
		}
		response[0] = DAP_OK;
		response[1] = fi.present ? 1U : 0U;
		put_u32(&response[2], fi.jedec_id);
		put_u32(&response[6], fi.capacity);
		put_u32(&response[10], fi.sector_size);
		put_u32(&response[14], fi.page_size);
		resp_len = 18U;
		break;
	}
	case 0x31: { /* SPI FLASH READ: u24 addr, u8 len */
		const uint32_t addr = ((uint32_t)request[2] << 16) | ((uint32_t)request[3] << 8) | request[4];
		uint32_t len = request[5];
		const uint32_t max = DAP_PACKET_SIZE - 2U;

		if (len > max) {
			len = max;
		}
		if (spi_flash_init() == 0 && spi_flash_read(addr, &response[2], len)) {
			response[0] = DAP_OK;
			response[1] = (uint8_t)len;
			resp_len = 2U + len;
		} else {
			response[0] = 0x01U;
			resp_len = 1U;
		}
		req_len = 6U;
		break;
	}
	case 0x32: { /* SPI FLASH ERASE SECTOR: u24 addr */
		const uint32_t addr = ((uint32_t)request[2] << 16) | ((uint32_t)request[3] << 8) | request[4];

		response[0] = (spi_flash_init() == 0 && spi_flash_erase_sector(addr)) ? DAP_OK : 0x01U;
		resp_len = 1U;
		req_len = 5U;
		break;
	}
	case 0x33: { /* SPI FLASH PROGRAM: u24 addr, u8 len, data... */
		const uint32_t addr = ((uint32_t)request[2] << 16) | ((uint32_t)request[3] << 8) | request[4];
		uint32_t len = request[5];
		const uint32_t max = DAP_PACKET_SIZE - 6U;

		if (len > max) {
			len = max;
		}
		response[0] = (spi_flash_init() == 0 && spi_flash_program(addr, &request[6], len)) ? DAP_OK : 0x01U;
		response[1] = (uint8_t)len;
		resp_len = 2U;
		req_len = 6U + len;
		break;
	}
	case 0x34: { /* SPI FLASH CHIP ERASE */
		response[0] = (spi_flash_init() == 0 && spi_flash_erase_chip()) ? DAP_OK : 0x01U;
		resp_len = 1U;
		req_len = 2U;
		break;
	}
#endif /* CONFIG_DAPLINK_SPI_FLASH */
#if CONFIG_DAPLINK_SWO_ITM
	case 0x40: { /* ITM SET MASK: u32 mask */
		const uint32_t mask = (uint32_t)request[2] | ((uint32_t)request[3] << 8) |
				      ((uint32_t)request[4] << 16) | ((uint32_t)request[5] << 24);

		swo_itm_set_mask(mask);
		response[0] = DAP_OK;
		resp_len = 1U;
		req_len = 6U;
		break;
	}
	case 0x41: { /* ITM READ decoded data */
		uint32_t count = swo_itm_count();
		const uint32_t max = DAP_PACKET_SIZE - 2U;

		if (count > max) {
			count = max;
		}
		count = swo_itm_read(&response[2], count);
		response[0] = DAP_OK;
		response[1] = (uint8_t)count;
		resp_len = 2U + count;
		req_len = 2U;
		break;
	}
	case 0x42: { /* ITM RESET */
		swo_itm_reset();
		response[0] = DAP_OK;
		resp_len = 1U;
		req_len = 2U;
		break;
	}
#endif /* CONFIG_DAPLINK_SWO_ITM */
	default:
		response[0] = 0xFFU;
		break;
	}

	return (req_len << 16) | resp_len;
}
