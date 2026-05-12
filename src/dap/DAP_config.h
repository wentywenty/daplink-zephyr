/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink DAP_config.h - Zephyr HAL adaptation
 *
 * Hardware abstraction layer for DAP.c and SW_DP.c.
 * All pin operations are mapped to Zephyr GPIO API.
 */

#ifndef DAP_CONFIG_H_
#define DAP_CONFIG_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/arch/arch_interface.h>

#include "dap_gpio.h"
#include "dap_delay.h"

#ifdef __cplusplus
extern "C" {
#endif

//**************************************************************************************************
/** Debug Unit Information */

/// Processor Clock of the Cortex-M MCU used in the Debug Unit.
#define CPU_CLOCK                CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC

/// Number of processor cycles for I/O port write operations.
#define IO_PORT_WRITE_CYCLES     2U

/// Indicate that SWD is available.
#define DAP_SWD                  1

/// Indicate that JTAG is available.
#define DAP_JTAG                 0

/// Maximum JTAG device count.
#define DAP_JTAG_DEV_CNT         0

/// Default communication port (1=SWD, 2=JTAG).
#define DAP_DEFAULT_PORT         1

/// Default SWD/JTAG clock frequency in Hz.
#define DAP_DEFAULT_SWJ_CLOCK    1000000U

/// Maximum Package Size (Full-Speed USB: 64).
#define DAP_PACKET_SIZE          64

/// Maximum Package Count.
#define DAP_PACKET_COUNT         1

/// SWO UART not available.
#define SWO_UART                 0
#define SWO_BUFFER_SIZE          0
#define SWO_MANCHESTER           0

/// DAP UART not available (Phase 1).
#define DAP_UART                 0
#define DAP_UART_USB_COM_PORT    0

/// Firmware version string.
#define DAP_FW_VER               "v1.0.0"

/// Timestamp clock (CPU_CLOCK for Cycle Counter).
#define TIMESTAMP_CLOCK           CPU_CLOCK

/// Timestamp get (use k_cycle_get_32 for approximate cycle counter).
#define TIMESTAMP_GET()           k_cycle_get_32()

/// SWO Streaming not available.
#define SWO_STREAM                0

//**************************************************************************************************
/** SWD/JTAG Pin I/O */

/// SWCLK/TCK: read input.
__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN(void)
{
	return gpio_pin_get_raw(swclk_spec.port, swclk_spec.pin);
}

/// SWCLK/TCK: set high.
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_SET(void)
{
	gpio_pin_set_raw(swclk_spec.port, swclk_spec.pin, 1);
}

/// SWCLK/TCK: set low.
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_CLR(void)
{
	gpio_pin_set_raw(swclk_spec.port, swclk_spec.pin, 0);
}

/// SWDIO/TMS: read input.
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN(void)
{
	if (swdio_in_present) {
		return gpio_pin_get_raw(swdio_in_spec.port, swdio_in_spec.pin);
	}
	return gpio_pin_get_raw(swdio_spec.port, swdio_spec.pin);
}

/// SWDIO/TMS: set high.
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_SET(void)
{
	gpio_pin_set_raw(swdio_spec.port, swdio_spec.pin, 1);
}

/// SWDIO/TMS: set low.
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_CLR(void)
{
	gpio_pin_set_raw(swdio_spec.port, swdio_spec.pin, 0);
}

/// SWDIO: read input (SWD mode).
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN(void)
{
	if (swdio_in_present) {
		return gpio_pin_get_raw(swdio_in_spec.port, swdio_in_spec.pin);
	}
	return gpio_pin_get_raw(swdio_spec.port, swdio_spec.pin);
}

/// SWDIO: write output.
__STATIC_FORCEINLINE void PIN_SWDIO_OUT(uint32_t bit)
{
	gpio_pin_set_raw(swdio_spec.port, swdio_spec.pin, bit);
}

/// SWDIO: switch to output mode.
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_ENABLE(void)
{
	gpio_pin_configure(swdio_spec.port, swdio_spec.pin, GPIO_OUTPUT);
}

/// SWDIO: switch to input mode.
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_DISABLE(void)
{
	if (swdio_in_present) {
		gpio_pin_configure(swdio_spec.port, swdio_spec.pin, GPIO_OUTPUT);
	} else {
		gpio_pin_configure(swdio_spec.port, swdio_spec.pin, GPIO_INPUT);
	}
}

//**************************************************************************************************
/** nRESET Pin I/O */

__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN(void)
{
	if (nreset_spec.port == NULL) {
		return 1;
	}
	return gpio_pin_get_raw(nreset_spec.port, nreset_spec.pin);
}

__STATIC_FORCEINLINE void PIN_nRESET_OUT(uint32_t bit)
{
	if (nreset_spec.port == NULL) {
		return;
	}
	gpio_pin_set_dt(&nreset_spec, bit);
}

//**************************************************************************************************
/** Port setup functions */

/// Setup SWD port pins.
__STATIC_FORCEINLINE void PORT_SWD_SETUP(void)
{
	/* Pins already configured by dap_gpio_init() */
}

#if (DAP_JTAG != 0)
__STATIC_FORCEINLINE void PORT_JTAG_SETUP(void)
{
	/* Not implemented for Phase 1 */
}
#endif

/// Disable all debug port pins (high-impedance).
__STATIC_FORCEINLINE void PORT_OFF(void)
{
	if (swclk_spec.port != NULL) {
		gpio_pin_configure(swclk_spec.port, swclk_spec.pin, GPIO_INPUT);
	}
	if (swdio_spec.port != NULL) {
		gpio_pin_configure(swdio_spec.port, swdio_spec.pin, GPIO_INPUT);
	}
	if (nreset_spec.port != NULL) {
		gpio_pin_configure(nreset_spec.port, nreset_spec.pin, GPIO_INPUT);
	}
}

//**************************************************************************************************
/** LED stubs */

__STATIC_FORCEINLINE void LED_CONNECTED_OUT(uint32_t bit) { (void)bit; }
__STATIC_FORCEINLINE void LED_RUNNING_OUT(uint32_t bit) { (void)bit; }

//**************************************************************************************************
/** JTAG pin stubs (DAP_JTAG = 0, but SWJ_Sequence references some) */

#define PIN_TDI_OUT(bit)   ((void)(bit))
#define PIN_TDI_IN()       0
#define PIN_TDO_IN()       0
#define PIN_nTRST_OUT(bit) ((void)(bit))
#define PIN_nTRST_IN()     0

//**************************************************************************************************
/** Delay functions */

/// Millisecond delay - DAP.c has been modified to use k_busy_wait directly.
/// No macro override needed since we modified the function body.

/// Slow delay for SWJ operations.
#define PIN_DELAY_SLOW(n) delay_ns((n) * 10U)

/// Fast delay for SWD clock generation.
#define PIN_DELAY_FAST() delay_ns(dap_delay_get_fast_count())

//**************************************************************************************************
/** Clock and device setup */

__STATIC_FORCEINLINE void DAP_SETUP(void)
{
	/* Nothing extra needed, GPIO already initialized */
}

__STATIC_FORCEINLINE uint32_t RESET_TARGET(void)
{
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* DAP_CONFIG_H_ */
