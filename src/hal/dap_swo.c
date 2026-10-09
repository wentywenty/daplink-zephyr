/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * CMSIS-DAP SWO capture backend.
 *
 * Implements the ID_DAP_SWO_* command handlers by capturing the target's
 * SWO (ITM-as-UART / NRZ) output on a Zephyr UART whose RX pin is wired to
 * the target SWO line. The captured bytes are buffered and streamed to the
 * host through DAP_SWO_Data.
 *
 * Modeled on the Black Magic Debug SWO-over-UART capture capability, but
 * exposed through the standard CMSIS-DAP SWO command set so pyOCD/OpenOCD
 * can consume it without any custom protocol.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>

#include "DAP_config.h"
#include "DAP.h"
#if defined(CONFIG_DAPLINK_SWO_ITM)
#include "swo_itm.h"
#endif

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dap_swo);

#if CONFIG_DAPLINK_SWO

/* The board must provide a daplink,swo node labelled swo0. */
#define SWO_NODE DT_NODELABEL(swo0)

#if !DT_NODE_EXISTS(SWO_NODE)
#error "CONFIG_DAPLINK_SWO requires a `swo0` node compatible with \"daplink,swo\""
#endif

#define SWO_RING_SIZE 4096U

static uint8_t swo_ring_data[SWO_RING_SIZE];
static struct ring_buf swo_ring;
static const struct device *swo_uart;
static volatile uint32_t swo_status;
static uint32_t swo_baudrate;
static bool swo_init_done;

static void swo_uart_isr(const struct device *dev, void *user_data)
{
	uint8_t buf[32];

	ARG_UNUSED(user_data);

	while (uart_irq_rx_ready(dev)) {
		const int n = uart_fifo_read(dev, buf, sizeof(buf));

		if (n <= 0) {
			break;
		}
		if (ring_buf_put(&swo_ring, buf, (uint32_t)n) < (uint32_t)n) {
			swo_status |= DAP_SWO_BUFFER_OVERRUN;
		}
#if defined(CONFIG_DAPLINK_SWO_ITM)
		/* Also run the captured bytes through the ITM decoder. */
		swo_itm_feed(buf, (uint16_t)n);
#endif
	}
}

static int swo_ensure_init(void)
{
	if (swo_init_done) {
		return 0;
	}

	swo_uart = DEVICE_DT_GET(DT_PHANDLE(SWO_NODE, uart));
	if (!device_is_ready(swo_uart)) {
		LOG_ERR("SWO UART not ready");
		return -ENODEV;
	}

	ring_buf_init(&swo_ring, sizeof(swo_ring_data), swo_ring_data);
	uart_irq_callback_user_data_set(swo_uart, swo_uart_isr, NULL);
	swo_init_done = true;
	return 0;
}

static int swo_apply_baudrate(void)
{
	struct uart_config cfg = {
		.baudrate = swo_baudrate,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	return uart_configure(swo_uart, &cfg);
}

uint32_t SWO_Transport(const uint8_t *request, uint8_t *response)
{
	/* Only the UART transport (1) is supported. */
	if (request[0] == DAP_SWO_UART) {
		response[0] = DAP_OK;
	} else {
		response[0] = DAP_ERROR;
	}
	return 1;
}

uint32_t SWO_Mode(const uint8_t *request, uint8_t *response)
{
	if (swo_ensure_init() != 0) {
		response[0] = DAP_ERROR;
		return 1;
	}

	if (request[0]) {
		if (swo_baudrate != 0U && swo_apply_baudrate() != 0) {
			response[0] = DAP_ERROR;
			return 1;
		}
		ring_buf_reset(&swo_ring);
		swo_status = DAP_SWO_CAPTURE_ACTIVE;
		uart_irq_rx_enable(swo_uart);
	} else {
		uart_irq_rx_disable(swo_uart);
		swo_status &= ~DAP_SWO_CAPTURE_ACTIVE;
	}

	response[0] = DAP_OK;
	return 1;
}

uint32_t SWO_Baudrate(const uint8_t *request, uint8_t *response)
{
	swo_baudrate = (uint32_t)request[0] | ((uint32_t)request[1] << 8U) |
		       ((uint32_t)request[2] << 16U) | ((uint32_t)request[3] << 24U);

	if (swo_ensure_init() != 0) {
		response[0] = DAP_ERROR;
		return 1;
	}

	/* Only reprogram the UART while capture is not running. */
	if (!(swo_status & DAP_SWO_CAPTURE_ACTIVE) && swo_baudrate != 0U) {
		if (swo_apply_baudrate() != 0) {
			response[0] = DAP_ERROR;
			return 1;
		}
	}

	response[0] = DAP_OK;
	return 1;
}

uint32_t SWO_Control(const uint8_t *request, uint8_t *response)
{
	if (swo_ensure_init() != 0) {
		response[0] = DAP_ERROR;
		return 1;
	}

	if (request[0] & 1U) {
		ring_buf_reset(&swo_ring);
		swo_status = (swo_status & ~DAP_SWO_CAPTURE_PAUSED) | DAP_SWO_CAPTURE_ACTIVE;
		uart_irq_rx_enable(swo_uart);
	} else {
		uart_irq_rx_disable(swo_uart);
		swo_status = (swo_status & ~DAP_SWO_CAPTURE_ACTIVE) | DAP_SWO_CAPTURE_PAUSED;
	}

	response[0] = DAP_OK;
	return 1;
}

uint32_t SWO_Status(uint8_t *response)
{
	const uint32_t count = ring_buf_size_get(&swo_ring);

	response[0] = (uint8_t)swo_status;
	response[1] = (uint8_t)(count >> 0);
	response[2] = (uint8_t)(count >> 8);
	response[3] = (uint8_t)(count >> 16);
	response[4] = (uint8_t)(count >> 24);
	return 5;
}

uint32_t SWO_ExtendedStatus(const uint8_t *request, uint8_t *response)
{
	ARG_UNUSED(request);
	return SWO_Status(response);
}

uint32_t SWO_Data(const uint8_t *request, uint8_t *response)
{
	if (request[0] == 1U) {
		/* Flush request: discard buffered data. */
		ring_buf_reset(&swo_ring);
		swo_status &= ~DAP_SWO_BUFFER_OVERRUN;
		return 0;
	}

	uint32_t count = ring_buf_size_get(&swo_ring);

	if (count > SWO_BUFFER_SIZE) {
		count = SWO_BUFFER_SIZE;
	}
	if (count != 0U) {
		count = ring_buf_get(&swo_ring, response, count);
	}
	return count;
}

#endif /* CONFIG_DAPLINK_SWO */
