# DAPLink Zephyr 移植 - 第一阶段：HID 调试接口

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Zephyr RTOS 上实现 CMSIS-DAP HID 调试探针，使 pyOCD/OpenOCD 能通过 USB HID 连接并执行 SWD 读写操作。

**Architecture:** 保留 DAPLink 核心（DAP.c、SW_DP.c）不变，通过重写 DAP_config.h 将硬件操作映射到 Zephyr GPIO API。USB 适配层使用 Zephyr 新栈 HID 类，通过工作队列模式处理 DAP 命令。

**Tech Stack:** Zephyr RTOS (v4.4+)、Zephyr USB 新栈 (USBD)、Zephyr GPIO API、CMSIS-DAP v2 协议

**设计文档：** `docs/superpowers/specs/2026-05-12-daplink-zephyr-port-design.md`

---

## 文件结构映射

| 文件 | 职责 | 状态 |
|------|------|------|
| `CMakeLists.txt` | Zephyr 应用构建配置 | 新建 |
| `prj.conf` | Kconfig 配置 | 新建 |
| `boards/<board>.overlay` | 设备树（SWD 引脚、HID 节点） | 新建 |
| `src/main.c` | 入口：初始化 HAL、DAP、USB | 新建 |
| `src/dap/DAP.h` | DAP 接口定义 | 从 DAPLink 引入 |
| `src/dap/DAP.c` | CMSIS-DAP 协议核心 | 从 DAPLink 引入 |
| `src/dap/SW_DP.c` | SWD 协议实现 | 从 DAPLink 引入 |
| `src/dap/DAP_config.h` | 硬件抽象（重写） | 重写 |
| `src/dap/cmsis_compiler.h` | CMSIS 宏兼容层 | 新建（桩文件） |
| `src/dap/dap_strings_compat.h` | DAP 字符串函数桩实现 | 新建 |
| `src/hal/dap_gpio.h` | GPIO 接口声明 | 新建 |
| `src/hal/dap_gpio.c` | 设备树→GPIO 初始化 | 新建 |
| `src/hal/dap_delay.h` | 延时接口声明 | 新建 |
| `src/hal/dap_delay.c` | k_busy_wait + arch_nop 延时 | 新建 |
| `src/usb/usbd_context.h` | USB 设备上下文接口 | 新建 |
| `src/usb/usbd_context.c` | USB 描述符注册和初始化 | 新建 |
| `src/usb/hid_dap.h` | HID-DAP 适配器接口 | 新建 |
| `src/usb/hid_dap.c` | HID→DAP 工作队列适配 | 新建 |

---

## Task 1: 项目骨架

创建能编译通过的 Zephyr 应用空壳。

**Files:**
- Create: `CMakeLists.txt`
- Create: `prj.conf`
- Create: `src/main.c`

- [ ] **Step 1: 创建 CMakeLists.txt**

```cmake
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(daplink-zephyr)

target_include_directories(app PRIVATE
    src
    src/dap
    src/usb
    src/hal
)

target_sources(app PRIVATE
    src/main.c
)
```

- [ ] **Step 2: 创建 prj.conf**

```ini
# USB 新栈
CONFIG_USB_DEVICE_STACK_NEXT=y
CONFIG_UDC_DRIVER=y

# HID (CMSIS-DAP)
CONFIG_USBD_HID_SUPPORT=y

# 基础功能
CONFIG_GPIO=y
CONFIG_SERIAL=y

# 日志
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=2

# 栈和堆
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_HEAP_MEM_POOL_SIZE=4096
```

- [ ] **Step 3: 创建空 main.c**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink Zephyr Port - CMSIS-DAP Debug Probe
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(daplink, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("DAPLink Zephyr starting...");

	while (true) {
		k_sleep(K_MSEC(1000));
	}

	return 0;
}
```

- [ ] **Step 4: 验证编译**

Run: `west build -b <board> . 2>&1 | tail -20`

选择一个有 USB 支持的开发板（如 nucleo_f103rb、nrf52840dk_nrf52840）。如果无具体板子，用 `-b native_sim` 验证编译框架正确。

Expected: `Memory region` 输出和 `Build complete` 消息。

- [ ] **Step 5: 提交**

```bash
git add CMakeLists.txt prj.conf src/main.c
git commit -m "feat: initialize Zephyr application skeleton"
```

---

## Task 2: HAL 层 - GPIO

实现从设备树获取 SWD 引脚配置并初始化 GPIO 的模块。

**Files:**
- Create: `src/hal/dap_gpio.h`
- Create: `src/hal/dap_gpio.c`

- [ ] **Step 1: 创建 dap_gpio.h**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink GPIO HAL - SWD/JTAG pin control via Zephyr GPIO API
 */

#ifndef DAP_GPIO_H_
#define DAP_GPIO_H_

#include <zephyr/drivers/gpio.h>

/*
 * SWD pin device specifiers, initialized from device tree.
 * Used by DAP_config.h inline functions.
 */
extern const struct gpio_dt_spec swclk_spec;
extern const struct gpio_dt_spec swdio_spec;
extern const struct gpio_dt_spec swdio_in_spec;
extern const struct gpio_dt_spec nreset_spec;

/* Indicates whether a separate SWDIO input pin is configured */
extern bool swdio_in_present;

/**
 * Initialize SWD/JTAG GPIO pins from device tree configuration.
 * Configures SWCLK as output, SWDIO as output (initially),
 * and nRESET as open-drain output with pull-up.
 *
 * @return 0 on success, negative errno on failure
 */
int dap_gpio_init(void);

#endif /* DAP_GPIO_H_ */
```

- [ ] **Step 2: 创建 dap_gpio.c**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink GPIO HAL implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "dap_gpio.h"

LOG_MODULE_REGISTER(dap_gpio, LOG_LEVEL_DBG);

/*
 * Device tree specifiers for SWD pins.
 * Uses DT_NODELABEL for direct node access instead of driver model macros.
 * The board overlay must define: daplink: daplink { compatible = "daplink,gpio"; ... };
 */
#define DAPLINK_GPIO_NODE DT_NODELABEL(daplink)

#if DT_NODE_HAS_STATUS(DAPLINK_GPIO_NODE, okay)
const struct gpio_dt_spec swclk_spec =
	GPIO_DT_SPEC_GET(DAPLINK_GPIO_NODE, swclk_gpios);
const struct gpio_dt_spec swdio_spec =
	GPIO_DT_SPEC_GET(DAPLINK_GPIO_NODE, swdio_gpios);

#define HAS_SWDIO_IN DT_NODE_HAS_PROP(DAPLINK_GPIO_NODE, swdio_in_gpios)
const struct gpio_dt_spec swdio_in_spec =
	COND_CODE_1(HAS_SWDIO_IN,
		(GPIO_DT_SPEC_GET(DAPLINK_GPIO_NODE, swdio_in_gpios)),
		({0}));
const struct gpio_dt_spec nreset_spec =
	GPIO_DT_SPEC_GET_OR(DAPLINK_GPIO_NODE, nreset_gpios, {0});
bool swdio_in_present = HAS_SWDIO_IN;

#else
/* Fallback: no device tree node */
const struct gpio_dt_spec swclk_spec = {0};
const struct gpio_dt_spec swdio_spec = {0};
const struct gpio_dt_spec swdio_in_spec = {0};
const struct gpio_dt_spec nreset_spec = {0};
bool swdio_in_present = false;

#warning "No daplink,gpio device tree node found - SWD pins not configured"
#endif

int dap_gpio_init(void)
{
	int ret;

	/* SWCLK: push-pull output, initially low */
	if (swclk_spec.port != NULL) {
		if (!gpio_is_ready_dt(&swclk_spec)) {
			LOG_ERR("SWCLK GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&swclk_spec, GPIO_OUTPUT_LOW);
		if (ret != 0) {
			LOG_ERR("Failed to configure SWCLK: %d", ret);
			return ret;
		}
	}

	/* SWDIO: push-pull output, initially low */
	if (swdio_spec.port != NULL) {
		if (!gpio_is_ready_dt(&swdio_spec)) {
			LOG_ERR("SWDIO GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&swdio_spec, GPIO_OUTPUT_LOW);
		if (ret != 0) {
			LOG_ERR("Failed to configure SWDIO: %d", ret);
			return ret;
		}
	}

	/* SWDIO input (optional separate pin) */
	if (swdio_in_present && swdio_in_spec.port != NULL) {
		if (!gpio_is_ready_dt(&swdio_in_spec)) {
			LOG_ERR("SWDIO-IN GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&swdio_in_spec, GPIO_INPUT);
		if (ret != 0) {
			LOG_ERR("Failed to configure SWDIO-IN: %d", ret);
			return ret;
		}
	}

	/* nRESET: open-drain output with pull-up, initially high (inactive) */
	if (nreset_spec.port != NULL) {
		if (!gpio_is_ready_dt(&nreset_spec)) {
			LOG_ERR("nRESET GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&nreset_spec,
			GPIO_OUTPUT_INACTIVE | GPIO_OPEN_DRAIN);
		if (ret != 0) {
			/* Fallback: try without open-drain if not supported */
			ret = gpio_pin_configure_dt(&nreset_spec,
				GPIO_OUTPUT_INACTIVE);
			if (ret != 0) {
				LOG_ERR("Failed to configure nRESET: %d", ret);
				return ret;
			}
		}
	}

	LOG_INF("DAP GPIO initialized");
	return 0;
}
```

- [ ] **Step 3: 更新 CMakeLists.txt 添加 HAL 源文件**

在 `target_sources(app PRIVATE` 块中添加：

```cmake
target_sources(app PRIVATE
    src/main.c
    src/hal/dap_gpio.c
)
```

- [ ] **Step 4: 验证编译**

Run: `west build -b <board> . 2>&1 | tail -20`

Expected: 编译通过，dap_gpio.c 被编译。

- [ ] **Step 5: 提交**

```bash
git add src/hal/dap_gpio.h src/hal/dap_gpio.c CMakeLists.txt
git commit -m "feat(hal): add GPIO initialization for SWD/JTAG pins"
```

---

## Task 3: HAL 层 - 精确延时

实现 SWD 时序所需的微秒级和纳秒级延时。

**Files:**
- Create: `src/hal/dap_delay.h`
- Create: `src/hal/dap_delay.c`

- [ ] **Step 1: 创建 dap_delay.h**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink Delay HAL - Precise timing for SWD/JTAG
 */

#ifndef DAP_DELAY_H_
#define DAP_DELAY_H_

#include <stdint.h>
#include <zephyr/kernel.h>

/**
 * Initialize delay subsystem. Calculates clock delay parameters
 * based on system clock frequency.
 */
void dap_delay_init(void);

/**
 * Microsecond-level busy wait.
 * Uses k_busy_wait() for accurate microsecond timing.
 */
static inline void delay_us(uint32_t us)
{
	k_busy_wait(us);
}

/**
 * Nanosecond-level busy wait using NOP loop.
 * Used for SWD/JTAG clock generation.
 * Actual delay = n * (cycles_per_nop / CPU_CLOCK_HZ) seconds.
 *
 * @param n  Number of NOP iterations
 */
static inline void delay_ns(uint32_t n)
{
	while (n--) {
		arch_nop();
	}
}

/**
 * Calculate and store the PIN_DELAY_FAST loop count
 * for a given target SWD clock frequency.
 *
 * @param clock_hz  Target SWD/JTAG clock frequency in Hz
 */
void dap_delay_calibrate(uint32_t clock_hz);

/**
 * Get the current fast delay loop count.
 * Used by DAP_config.h PIN_DELAY_FAST macro.
 *
 * @return  NOP loop iterations per SWD clock half-period
 */
uint32_t dap_delay_get_fast_count(void);

#endif /* DAP_DELAY_H_ */
```

- [ ] **Step 2: 创建 dap_delay.c**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink Delay HAL implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/arch_interface.h>
#include <zephyr/logging/log.h>
#include "dap_delay.h"

LOG_MODULE_REGISTER(dap_delay, LOG_LEVEL_DBG);

/* Fast delay loop count for SWD clock generation.
 * Calibrated based on target clock frequency and CPU speed.
 * Each loop iteration = ~1/CPU_CLOCK seconds.
 */
static uint32_t fast_delay_count;

void dap_delay_init(void)
{
	/* Start with a conservative default (1MHz SWD clock) */
	dap_delay_calibrate(1000000);
	LOG_INF("Delay subsystem initialized");
}

void dap_delay_calibrate(uint32_t clock_hz)
{
	uint32_t cpu_hz = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;

	if (clock_hz == 0) {
		clock_hz = 1;
	}

	/* Each SWD clock cycle needs two half-periods (SET + CLR).
	 * Half-period = 1 / (2 * clock_hz) seconds.
	 * NOP loop takes ~1/cpu_hz per iteration (plus loop overhead ~2 cycles).
	 * fast_delay_count = cpu_hz / (2 * clock_hz * cycles_per_nop)
	 * Using 3 cycles per NOP loop iteration as conservative estimate.
	 */
	uint32_t cycles_per_loop = 3;
	uint32_t half_period_cycles = cpu_hz / (2 * clock_hz);
	fast_delay_count = half_period_cycles / cycles_per_loop;

	if (fast_delay_count == 0) {
		fast_delay_count = 1;
	}

	LOG_DBG("SWD clock calibration: %u Hz, delay count: %u",
		clock_hz, fast_delay_count);
}

uint32_t dap_delay_get_fast_count(void)
{
	return fast_delay_count;
}
```

- [ ] **Step 3: 更新 CMakeLists.txt 添加延时源文件**

```cmake
target_sources(app PRIVATE
    src/main.c
    src/hal/dap_gpio.c
    src/hal/dap_delay.c
)
```

- [ ] **Step 4: 验证编译**

Run: `west build -b <board> . 2>&1 | tail -20`

Expected: 编译通过。

- [ ] **Step 5: 提交**

```bash
git add src/hal/dap_delay.h src/hal/dap_delay.c CMakeLists.txt
git commit -m "feat(hal): add precise delay for SWD/JTAG timing"
```

---

## Task 4: DAP_config.h - 硬件抽象适配

重写 DAP_config.h，将 DAPLink 引脚操作映射到 Zephyr GPIO API。这是移植的关键文件。

**Files:**
- Create: `src/dap/DAP_config.h`

**依赖：** DAP_gpio.h 和 DAP_delay.h 提供的接口。

- [ ] **Step 1: 创建 DAP_config.h**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink DAP_config.h - Zephyr HAL adaptation
 *
 * This file provides the hardware abstraction layer that DAP.c and SW_DP.c
 * require. All pin operations are mapped to Zephyr GPIO API.
 */

#ifndef DAP_CONFIG_H_
#define DAP_CONFIG_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/arch_interface.h>
#include <string.h>

#include "dap_gpio.h"
#include "dap_delay.h"

#ifdef __cplusplus
extern "C" {
#endif

//**************************************************************************************************
/**
 * @brief Debug Unit Information
 *
 * DAP_GetProductString(), DAP_GetVendorString(), DAP_GetSerialNumberString()
 * are weak functions that can be overridden. Default implementations return NULL.
 */

/// Processor Clock of the Cortex-M MCU used in the Debug Unit.
#define CPU_CLOCK                CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC

/// Number of processor cycles for I/O port write operations.
#define IO_PORT_WRITE_CYCLES     2U

/// Indicate that SWD is available (1=available, 0=not available).
#define DAP_SWD                  1

/// Indicate that JTAG is available (1=available, 0=not available).
#define DAP_JTAG                 0

/// Maximum JTAG device count (only relevant if DAP_JTAG != 0).
#define DAP_JTAG_DEV_CNT         0

/// Default communication port (1=SWD, 2=JTAG).
#define DAP_DEFAULT_PORT         1

/// Default SWD/JTAG clock frequency in Hz.
#define DAP_DEFAULT_SWJ_CLOCK    1000000U

/// Maximum Package Size for Command and Response data.
/// Full-Speed USB: 64 bytes, High-Speed USB: 512 bytes.
#define DAP_PACKET_SIZE          64

/// Maximum Package Count for Command and Response data.
#define DAP_PACKET_COUNT         1

/// Indicate that SWO UART is available (1=available, 0=not available).
#define SWO_UART                 0

/// SWO UART buffer size in bytes.
#define SWO_BUFFER_SIZE          0

/// Indicate that SWO Manchester is available (1=available, 0=not available).
#define SWO_MANCHESTER           0

/// Indicate that DAP UART is available (1=available, 0=not available).
/// Disable for Phase 1.
#define DAP_UART                 0

/// Indicate that UART USB COM port is available (1=available, 0=not available).
#define DAP_UART_USB_COM_PORT    0

//**************************************************************************************************
/**
 * @brief SWD/JTAG Pin I/O Macros
 *
 * These macros are used directly by DAP.c and SW_DP.c for bit-banging
 * the SWD/JTAG protocol. All operations use raw GPIO to ensure
 * physical-level control.
 */

/// SWCLK/TCK pin: read input (not typically needed, but required by DAP).
__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN(void)
{
	return gpio_pin_get_raw(swclk_spec.port, swclk_spec.pin);
}

/// SWCLK/TCK pin: set output high.
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_SET(void)
{
	gpio_pin_set_raw(swclk_spec.port, swclk_spec.pin, 1);
}

/// SWCLK/TCK pin: set output low.
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_CLR(void)
{
	gpio_pin_set_raw(swclk_spec.port, swclk_spec.pin, 0);
}

/// SWDIO/TMS pin: read input.
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN(void)
{
	if (swdio_in_present) {
		return gpio_pin_get_raw(swdio_in_spec.port, swdio_in_spec.pin);
	}
	return gpio_pin_get_raw(swdio_spec.port, swdio_spec.pin);
}

/// SWDIO/TMS pin: set output high.
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_SET(void)
{
	gpio_pin_set_raw(swdio_spec.port, swdio_spec.pin, 1);
}

/// SWDIO/TMS pin: set output low.
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_CLR(void)
{
	gpio_pin_set_raw(swdio_spec.port, swdio_spec.pin, 0);
}

/// SWDIO pin: read input (SWD mode).
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN(void)
{
	if (swdio_in_present) {
		return gpio_pin_get_raw(swdio_in_spec.port, swdio_in_spec.pin);
	}
	return gpio_pin_get_raw(swdio_spec.port, swdio_spec.pin);
}

/// SWDIO pin: write output (SWD mode).
__STATIC_FORCEINLINE void PIN_SWDIO_OUT(uint32_t bit)
{
	gpio_pin_set_raw(swdio_spec.port, swdio_spec.pin, bit);
}

/// SWDIO pin: switch to output mode.
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_ENABLE(void)
{
	gpio_pin_configure(swdio_spec.port, swdio_spec.pin, GPIO_OUTPUT);
}

/// SWDIO pin: switch to input mode.
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_DISABLE(void)
{
	if (swdio_in_present) {
		gpio_pin_configure(swdio_spec.port, swdio_spec.pin, GPIO_OUTPUT);
	} else {
		gpio_pin_configure(swdio_spec.port, swdio_spec.pin, GPIO_INPUT);
	}
}

//**************************************************************************************************
/**
 * @brief Port setup functions
 *
 * Called by DAP.c when switching debug port mode.
 */

/// Setup SWD port pins. GPIO already initialized by dap_gpio_init().
__STATIC_FORCEINLINE void PORT_SWD_SETUP(void)
{
	/* Pins already configured by dap_gpio_init() */
}

#if (DAP_JTAG != 0)
/// Setup JTAG port pins (not used in Phase 1).
__STATIC_FORCEINLINE void PORT_JTAG_SETUP(void)
{
	/* Not implemented for Phase 1 (DAP_JTAG = 0) */
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
/**
 * @brief nRESET pin I/O
 */

/// nRESET pin: read input.
__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN(void)
{
	if (nreset_spec.port == NULL) {
		return 1; /* Default: not in reset */
	}
	return gpio_pin_get_raw(nreset_spec.port, nreset_spec.pin);
}

/// nRESET pin: write output.
__STATIC_FORCEINLINE void PIN_nRESET_OUT(uint32_t bit)
{
	if (nreset_spec.port == NULL) {
		return;
	}
	gpio_pin_set_dt(&nreset_spec, bit);
}

//**************************************************************************************************
/**
 * @brief LED and misc pin I/O
 *
 * LED functions are stubs for Phase 1. Will be implemented with device tree
 * GPIO specs in a later phase.
 */

__STATIC_FORCEINLINE void LED_CONNECTED_OUT(uint32_t bit)
{
	(void)bit;
}

__STATIC_FORCEINLINE void LED_RUNNING_OUT(uint32_t bit)
{
	(void)bit;
}

//**************************************************************************************************
/**
 * @brief Delay functions
 *
 * IMPORTANT: DAP.h defines PIN_DELAY_SLOW() and PIN_DELAY_FAST() as inline
 * functions using ARM CMSIS intrinsics. These will be overridden by the
 * definitions below because DAP_config.h is included BEFORE DAP.h in DAP.c.
 * However, since DAP.h uses inline functions (not macros), we cannot use
 * #define to override them. Instead, we modify DAP.h to wrap its definitions
 * with #ifndef guards (see Task 5 Step 5).
 *
 * For now, DAP_config.h defines Delayms() directly since DAP.c's version
 * calls PIN_DELAY_SLOW with CPU-cycle-converted values which are incompatible
 * with k_busy_wait(). We also define PIN_DELAY_SLOW and PIN_DELAY_FAST as
 * preprocessor macros so they take precedence over DAP.h's inline functions
 * (only works if DAP.h wraps with #ifndef).
 */

/// Millisecond delay - override DAP.c's version which uses CPU cycle calculation.
/// DAP.c's Delayms() converts ms to CPU cycles then calls PIN_DELAY_SLOW with
/// cycle counts (e.g., 24000 for 1ms on 72MHz). k_busy_wait expects microseconds.
/// To avoid this mismatch, we redefine Delayms() here and ensure it takes priority.
#undef Delayms
#define Delayms(delay) k_busy_wait((uint32_t)(delay) * 1000U)

/// Slow delay for SWJ operations.
/// DAP.c passes CPU cycle counts to this, but we interpret as microseconds.
/// This is safe because DAP.c only calls Delayms() which we've overridden above,
/// and DAP_Delay() which we've also overridden. PIN_DELAY_SLOW is called
/// from SWJ_Sequence() with small values (loop iteration counts).
/// We use a NOP loop here since the values are cycle-based, not time-based.
#define PIN_DELAY_SLOW(n) delay_ns((n) * 10U)

/// Fast delay for SWD clock generation.
/// Called in tight loops by SW_DP.c for bit-banging SWD clock.
/// Uses calibrated NOP loop count.
#define PIN_DELAY_FAST() delay_ns(dap_delay_get_fast_count())

//**************************************************************************************************
/**
 * @brief Clock setup
 *
 * Called by DAP_Setup() to configure clock delay for target frequency.
 */

/// Set clock delay for target SWD/JTAG frequency.
__STATIC_FORCEINLINE void Set_DAP_Clock_Delay(uint32_t clock_hz)
{
	dap_delay_calibrate(clock_hz);
}

//**************************************************************************************************
/**
 * @brief Device-specific setup and reset
 */

/// Device-specific DAP setup (called from DAP_Setup).
/// GPIO is already initialized before DAP_Setup() is called.
__STATIC_FORCEINLINE void DAP_SETUP(void)
{
	/* Nothing extra needed, GPIO already initialized */
}

/// Device-specific target reset.
/// Return 0 to use default reset sequence, 1 for custom.
__STATIC_FORCEINLINE uint32_t RESET_TARGET(void)
{
	return 0; /* Use default reset */
}

#ifdef __cplusplus
}
#endif

#endif /* DAP_CONFIG_H_ */
```

- [ ] **Step 2: 验证编译**

DAP_config.h 被后续 Task 5 中的 DAP 核心代码引用。此时仅验证头文件语法。

Run: `west build -b <board> . 2>&1 | tail -20`

Expected: 编译通过（DAP_config.h 暂未被引用，不影响编译）。

- [ ] **Step 3: 提交**

```bash
git add src/dap/DAP_config.h
git commit -m "feat(dap): add DAP_config.h with Zephyr GPIO adaptation"
```

---

## Task 5: DAP 核心代码引入

从 DAPLink 源码复制 DAP.c、SW_DP.c、DAP.h 到 src/dap/，处理编译兼容性。

**Files:**
- Copy: `../DAPLink/source/daplink/cmsis-dap/DAP.h` → `src/dap/DAP.h`
- Copy: `../DAPLink/source/daplink/cmsis-dap/DAP.c` → `src/dap/DAP.c`
- Copy: `../DAPLink/source/daplink/cmsis-dap/SW_DP.c` → `src/dap/SW_DP.c`
- Modify: `src/dap/DAP.c` (minimal: header includes fix)
- Modify: `src/dap/SW_DP.c` (minimal: header includes fix)

- [ ] **Step 1: 复制 DAP.h**

```bash
cp ../DAPLink/source/daplink/cmsis-dap/DAP.h src/dap/DAP.h
```

- [ ] **Step 2: 复制 DAP.c**

```bash
cp ../DAPLink/source/daplink/cmsis-dap/DAP.c src/dap/DAP.c
```

- [ ] **Step 3: 复制 SW_DP.c**

```bash
cp ../DAPLink/source/daplink/cmsis-dap/SW_DP.c src/dap/SW_DP.c
```

- [ ] **Step 4: 创建 cmsis_compiler.h 兼容桩文件**

DAP.h 包含 `#include "cmsis_compiler.h"`，提供 ARM CMSIS 编译器抽象宏。Zephyr 不包含此文件，需要创建兼容版本。

文件 `src/dap/cmsis_compiler.h`：

```c
/*
 * CMSIS Compiler Compatibility Stub for Zephyr
 *
 * Provides ARM CMSIS compiler abstraction macros using GCC syntax.
 * This file replaces the ARM CMSIS cmsis_compiler.h that is not
 * available in Zephyr builds.
 */

#ifndef CMSIS_COMPILER_H_
#define CMSIS_COMPILER_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/sys/arch_interface.h>

/* Compiler agnostic macros - GCC versions */
#ifndef   __STATIC_FORCEINLINE
  #define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
#endif
#ifndef   __STATIC_INLINE
  #define __STATIC_INLINE  static inline
#endif
#ifndef   __WEAK
  #define __WEAK          __attribute__((weak))
#endif
#ifndef   __ALIGNED
  #define __ALIGNED(x)    __attribute__((aligned(x)))
#endif
#ifndef   __PACKED
  #define __PACKED        __attribute__((packed))
#endif
#ifndef   __PACKED_STRUCT
  #define __PACKED_STRUCT struct __attribute__((packed))
#endif
#ifndef   __UNALIGNED_UINT16_READ
  __PACKED_STRUCT T_UINT16 { uint16_t v; };
  #define __UNALIGNED_UINT16_READ(addr) (((struct T_UINT16 *)(void *)(addr))->v)
#endif
#ifndef   __UNALIGNED_UINT16_WRITE
  __PACKED_STRUCT T_UINT16_WRITE { uint16_t v; };
  #define __UNALIGNED_UINT16_WRITE(addr, val) (void)((((struct T_UINT16_WRITE *)(void *)(addr))->v) = (val))
#endif
#ifndef   __UNALIGNED_UINT32_READ
  __PACKED_STRUCT T_UINT32 { uint32_t v; };
  #define __UNALIGNED_UINT32_READ(addr) (((struct T_UINT32 *)(void *)(addr))->v)
#endif
#ifndef   __UNALIGNED_UINT32_WRITE
  __PACKED_STRUCT T_UINT32_WRITE { uint32_t v; };
  #define __UNALIGNED_UINT32_WRITE(addr, val) (void)((((struct T_UINT32_WRITE *)(void *)(addr))->v) = (val))
#endif

/* __NOP is already provided by Zephyr's arch_nop() via sys/arch_interface.h */
#ifndef __NOP
#define __NOP() arch_nop()
#endif

/* Boolean type compatibility (DAPLink uses BOOL, __TRUE, __FALSE) */
#ifndef BOOL
  typedef uint32_t BOOL;
#endif
#ifndef __TRUE
  #define __TRUE  1
#endif
#ifndef __FALSE
  #define __FALSE 0
#endif

#endif /* CMSIS_COMPILER_H_ */
```

- [ ] **Step 5: 创建 dap_strings_compat.h 桩文件**

DAP.c 包含 `#include "info.h"` 和 `#include "dap_strings.h"`，这些依赖 DAPLink 的板级信息系统。为第一阶段创建简化替代。

文件 `src/dap/dap_strings_compat.h`：

```c
/*
 * DAP Strings Compatibility Stubs
 *
 * Provides weak implementations of DAP string functions that DAP.c
 * calls for ID_DAP_Info responses. These replace DAPLink's
 * dap_strings.h / info.h dependency chain.
 */

#ifndef DAP_STRINGS_COMPAT_H_
#define DAP_STRINGS_COMPAT_H_

#include <stdint.h>

/*
 * String pointers returned by these functions are used in DAP_Info response.
 * Return NULL to indicate "no string available" - DAP.c handles this gracefully.
 */

/* Vendor string (e.g., "ARM") */
static inline const char *DAP_GetVendorString(void) { return "ARM"; }

/* Product string (e.g., "DAPLink CMSIS-DAP") */
static inline const char *DAP_GetProductString(void) { return "CMSIS-DAP"; }

/* Serial number string */
static inline const char *DAP_GetSerNumString(void) { return NULL; }

/* Product firmware version string */
static inline const char *DAP_GetProductFirmwareVersionString(void) { return "1.0"; }

/* Edit the DAP.c #include directives:
 *   Replace #include "info.h"        → (remove or comment out)
 *   Replace #include "dap_strings.h" → #include "dap_strings_compat.h"
 */
#endif /* DAP_STRINGS_COMPAT_H_ */
```

- [ ] **Step 6: 修改 DAP.c 的头文件包含**

编辑 `src/dap/DAP.c`，修改 include 部分：

1. 将 `#include "info.h"` 替换为注释 `/* #include "info.h" */`
2. 将 `#include "dap_strings.h"` 替换为 `#include "dap_strings_compat.h"`
3. 检查是否有其他 DAPLink 特有的 include（如 `"RTL.h"`、`"cmsis_os2.h"` 等），如有则移除或条件编译

**注意：** 这是对引入代码的"必要最小修改"，需在提交信息中注明。

- [ ] **Step 7: 修改 DAP.h 的延时函数，添加 #ifndef 保护**

编辑 `src/dap/DAP.h`，找到 `PIN_DELAY_SLOW` 和 `PIN_DELAY_FAST` 的 inline 函数定义，用 `#ifndef` 包裹：

```c
/* 将原始定义：
 *   __STATIC_FORCEINLINE void PIN_DELAY_SLOW(uint32_t n) { ... }
 *   __STATIC_FORCEINLINE void PIN_DELAY_FAST(void) { ... }
 * 替换为：
 */
#ifndef PIN_DELAY_SLOW
__STATIC_FORCEINLINE void PIN_DELAY_SLOW(uint32_t n)
{
  /* original ARM assembly implementation */
  ...
}
#endif

#ifndef PIN_DELAY_FAST
__STATIC_FORCEINLINE void PIN_DELAY_FAST(void)
{
  /* original implementation */
  ...
}
#endif
```

这样 DAP_config.h 中的宏定义会优先生效。

同样，检查 DAP.h 中是否有 `Delayms()` 或 `DAP_Delay()` 的定义。如果有，也需要类似的 `#ifndef` 保护或注释说明覆盖策略。

- [ ] **Step 8: 检查并修复 SW_DP.c 的头文件包含**

SW_DP.c 只应依赖 DAP_config.h 和 DAP.h。检查并移除任何 DAPLink 特有的 include。

- [ ] **Step 5: 检查并修复 SW_DP.c 的头文件包含**

SW_DP.c 只应依赖 DAP_config.h 和 DAP.h。检查并移除任何 DAPLink 特有的 include。

- [ ] **Step 9: 更新 CMakeLists.txt**

```cmake
target_sources(app PRIVATE
    src/main.c
    src/hal/dap_gpio.c
    src/hal/dap_delay.c
    src/dap/DAP.c
    src/dap/SW_DP.c
)
```

对 DAPLink 引入的文件添加宽松编译选项（在 CMakeLists.txt 末尾）：

```cmake
# Suppress warnings for DAPLink core files
set_source_files_properties(
    src/dap/DAP.c
    src/dap/SW_DP.c
    PROPERTIES
    COMPILE_FLAGS "-Wno-unused-parameter -Wno-sign-compare"
)
```

- [ ] **Step 10: 验证编译**

Run: `west build -b <board> . 2>&1 | tail -30`

Expected: 编译通过。DAP.c 和 SW_DP.c 可能产生警告但不应有错误。

注意：此时 DAP.c 和 SW_DP.c 中的符号未被引用（main.c 没有调用 DAP_Setup），但 Zephyr 构建系统会编译所有 target_sources 中的文件。

- [ ] **Step 11: 提交**

```bash
git add src/dap/DAP.h src/dap/DAP.c src/dap/SW_DP.c src/dap/cmsis_compiler.h src/dap/dap_strings_compat.h CMakeLists.txt
git commit -m "feat(dap): integrate CMSIS-DAP core with Zephyr compatibility shims"
```

---

## Task 6: USB 设备上下文

创建 USB 设备描述符注册和初始化模块，使用 Zephyr 新栈宏。

**Files:**
- Create: `src/usb/usbd_context.h`
- Create: `src/usb/usbd_context.c`

- [ ] **Step 1: 创建 usbd_context.h**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink USB Device Context
 */

#ifndef USBD_CONTEXT_H_
#define USBD_CONTEXT_H_

#include <zephyr/usb/usbd.h>

/**
 * Initialize and register the DAPLink USB device.
 * Sets up device descriptor, string descriptors, configuration,
 * and registers all enabled USB classes.
 *
 * @return 0 on success, negative errno on failure
 */
int daplink_usbd_init(void);

/**
 * Get the DAPLink USB device context pointer.
 *
 * @return Pointer to usbd_context, or NULL if not initialized
 */
struct usbd_context *daplink_usbd_ctx(void);

#endif /* USBD_CONTEXT_H_ */
```

- [ ] **Step 2: 创建 usbd_context.c**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink USB Device Context - Device descriptor and initialization
 */

#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>

#include "usbd_context.h"

LOG_MODULE_REGISTER(usbd_ctx, LOG_LEVEL_INF);

/* USB Vendor ID: Arm Ltd (official DAPLink VID) */
#define DAPLINK_USB_VID    0x0D28
/* USB Product ID: mbed CMSIS-DAP */
#define DAPLINK_USB_PID    0x0204

/*
 * USB Device Definition
 * Uses the default UDC node from device tree (zephyr_udc0).
 */
USBD_DEVICE_DEFINE(daplink_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   DAPLINK_USB_VID,
		   DAPLINK_USB_PID);

/*
 * String Descriptors
 */
USBD_DESC_LANG_DEFINE(daplink_lang);
USBD_DESC_MANUFACTURER_DEFINE(daplink_mfr, "ARM");
USBD_DESC_PRODUCT_DEFINE(daplink_product, "DAPLink CMSIS-DAP");
USBD_DESC_SERIAL_NUMBER_DEFINE(daplink_sn);

/*
 * Configuration Descriptors
 */
USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "DAPLink FS");

/* Full-Speed configuration: self-powered, 250mA (bMaxPower unit = 2mA, so 125 * 2 = 250mA) */
USBD_CONFIGURATION_DEFINE(daplink_fs_config,
			  USB_SCD_SELF_POWERED,
			  125,
			  &fs_cfg_desc);

static void usbd_msg_cb(struct usbd_context *const usbd_ctx,
			const struct usbd_msg *const msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (msg->type == USBD_MSG_CONFIGURATION) {
		LOG_INF("  Configuration value %d", msg->status);
	}
}

int daplink_usbd_init(void)
{
	int err;

	/* Add string descriptors */
	err = usbd_add_descriptor(&daplink_usbd, &daplink_lang);
	if (err) {
		LOG_ERR("Failed to add language descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&daplink_usbd, &daplink_mfr);
	if (err) {
		LOG_ERR("Failed to add manufacturer descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&daplink_usbd, &daplink_product);
	if (err) {
		LOG_ERR("Failed to add product descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&daplink_usbd, &daplink_sn);
	if (err) {
		LOG_ERR("Failed to add serial number descriptor (%d)", err);
		return err;
	}

	/* Add Full-Speed configuration */
	err = usbd_add_configuration(&daplink_usbd, USBD_SPEED_FS,
				     &daplink_fs_config);
	if (err) {
		LOG_ERR("Failed to add FS configuration (%d)", err);
		return err;
	}

	/* Register USB classes - will be added in Task 7 */
	/* usbd_register_class(&daplink_usbd, "hid_dap", 1); */

	/* Set device class code to use interface descriptors */
	usbd_device_set_code_triple(&daplink_usbd, USBD_SPEED_FS,
				    USB_BCC_MISCELLANEOUS, 0x02, 0x01);

	/* Register message callback */
	err = usbd_msg_register_cb(&daplink_usbd, usbd_msg_cb);
	if (err) {
		LOG_ERR("Failed to register msg callback (%d)", err);
		return err;
	}

	/* Set self-powered attribute */
	usbd_self_powered(&daplink_usbd, true);

	/* Initialize USB device */
	err = usbd_init(&daplink_usbd);
	if (err) {
		LOG_ERR("Failed to init USB device (%d)", err);
		return err;
	}

	LOG_INF("USB device initialized (VID=%04X PID=%04X)",
		DAPLINK_USB_VID, DAPLINK_USB_PID);
	return 0;
}

struct usbd_context *daplink_usbd_ctx(void)
{
	return &daplink_usbd;
}
```

- [ ] **Step 3: 更新 CMakeLists.txt**

```cmake
target_sources(app PRIVATE
    src/main.c
    src/hal/dap_gpio.c
    src/hal/dap_delay.c
    src/dap/DAP.c
    src/dap/SW_DP.c
    src/usb/usbd_context.c
)
```

- [ ] **Step 4: 验证编译**

Run: `west build -b <board> . 2>&1 | tail -30`

Expected: 编译通过。注意 `DT_NODELABEL(zephyr_udc0)` 需要目标板有 USB 支持才能链接成功。如果使用 `native_sim`，此步骤可能链接失败 — 需要实际 USB 硬件板或带 USB 支持的 QEMU 目标。

- [ ] **Step 5: 提交**

```bash
git add src/usb/usbd_context.h src/usb/usbd_context.c CMakeLists.txt
git commit -m "feat(usb): add USB device context with DAPLink descriptors"
```

---

## Task 7: HID-DAP 适配器

实现 HID 报告处理，将 USB HID OUT Report 转换为 DAP 命令执行，将响应通过 HID IN Report 返回。

**Files:**
- Create: `src/usb/hid_dap.h`
- Create: `src/usb/hid_dap.c`

- [ ] **Step 1: 创建 hid_dap.h**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * HID-DAP Adapter - Bridges USB HID and CMSIS-DAP command processing
 */

#ifndef HID_DAP_H_
#define HID_DAP_H_

#include <zephyr/device.h>

/**
 * Initialize the HID-DAP adapter.
 * Registers HID device with CMSIS-DAP report descriptor,
 * sets up work queue for DAP command processing.
 *
 * @return 0 on success, negative errno on failure
 */
int hid_dap_init(void);

/**
 * Check if HID-DAP interface is ready.
 *
 * @return true if USB HID interface is active
 */
bool hid_dap_is_ready(void);

#endif /* HID_DAP_H_ */
```

- [ ] **Step 2: 创建 hid_dap.c**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * HID-DAP Adapter Implementation
 *
 * Data flow:
 *   Host HID OUT Report → output_report() → k_msgq → work queue
 *   work queue → DAP_ExecuteCommand() → hid_device_submit_report()
 *   Host HID IN Report ←
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/logging/log.h>

#include "hid_dap.h"
#include "DAP.h"
#include "usbd_context.h"

LOG_MODULE_REGISTER(hid_dap, LOG_LEVEL_INF);

/*
 * CMSIS-DAP HID Report Descriptor
 * Vendor-Defined Usage Page, 64-byte Input/Output reports.
 */
static const uint8_t hid_report_desc[] = {
	0x06, 0x00, 0xFF,    /* Usage Page (Vendor Defined 0xFF00) */
	0x09, 0x01,          /* Usage (0x01) */
	0xA1, 0x01,          /* Collection (Application) */
	/* Input report: device to host */
	0x09, 0x02,          /*   Usage (0x02) */
	0x15, 0x00,          /*   Logical Minimum (0) */
	0x26, 0xFF, 0x00,    /*   Logical Maximum (255) */
	0x75, 0x08,          /*   Report Size (8) */
	0x95, 0x40,          /*   Report Count (64) */
	0x81, 0x02,          /*   Input (Data,Var,Abs) */
	/* Output report: host to device */
	0x09, 0x03,          /*   Usage (0x03) */
	0x15, 0x00,          /*   Logical Minimum (0) */
	0x26, 0xFF, 0x00,    /*   Logical Maximum (255) */
	0x75, 0x08,          /*   Report Size (8) */
	0x95, 0x40,          /*   Report Count (64) */
	0x91, 0x02,          /*   Output (Data,Var,Abs) */
	0xC0                 /* End Collection */
};

/* DAP request/response buffer */
struct dap_msg {
	uint8_t request[DAP_PACKET_SIZE];
	uint16_t req_len;
};

/* Message queue for DAP commands (USB callback → work queue) */
K_MSGQ_DEFINE(dap_cmd_msgq, sizeof(struct dap_msg), 8, 1);

/* Response buffer */
static uint8_t dap_response[DAP_PACKET_SIZE];
static bool hid_ready;

/* HID device reference */
static const struct device *hid_dev;

/* Work item for DAP command processing */
static struct k_work dap_work;

/* Mutex to protect DAP bus access (SWD/JTAG pin operations).
 * Shared between HID and future Bulk interface (Phase 4).
 */
K_MUTEX_DEFINE(dap_bus_mutex);

static void dap_work_handler(struct k_work *work)
{
	struct dap_msg msg;
	uint32_t result;

	while (k_msgq_get(&dap_cmd_msgq, &msg, K_NO_WAIT) == 0) {
		/* Lock DAP bus mutex during command execution */
		k_mutex_lock(&dap_bus_mutex, K_FOREVER);

		/* Clear response buffer to avoid stale data */
		memset(dap_response, 0, DAP_PACKET_SIZE);

		/* Execute DAP command */
		result = DAP_ExecuteCommand(msg.request, dap_response);

		uint16_t resp_len = (uint16_t)(result & 0xFFFF);

		LOG_DBG("DAP cmd 0x%02X: req=%u resp=%u",
			msg.request[0],
			(uint16_t)(result >> 16),
			resp_len);

		k_mutex_unlock(&dap_bus_mutex);

		/* Submit response if HID is ready */
		if (hid_ready) {
			int ret = hid_device_submit_report(hid_dev,
							   resp_len,
							   dap_response);
			if (ret != 0) {
				LOG_WRN("HID submit report failed: %d", ret);
			}
		}
	}
}

/*
 * HID Device Callbacks
 */

static void dap_iface_ready(const struct device *dev, const bool ready)
{
	LOG_INF("HID DAP interface %s", ready ? "ready" : "not ready");
	hid_ready = ready;
}

static int dap_get_report(const struct device *dev,
			  const uint8_t type, const uint8_t id,
			  const uint16_t len, uint8_t *const buf)
{
	/* Get Report is not typically used in CMSIS-DAP,
	 * responses are sent via hid_device_submit_report().
	 */
	LOG_WRN("Get Report type=%u id=%u len=%u (not handled)", type, id, len);
	return -ENOTSUP;
}

static int dap_set_report(const struct device *dev,
			  const uint8_t type, const uint8_t id,
			  const uint16_t len, const uint8_t *const buf)
{
	if (type != HID_REPORT_TYPE_OUTPUT) {
		return -ENOTSUP;
	}

	struct dap_msg msg;

	msg.req_len = (len > DAP_PACKET_SIZE) ? DAP_PACKET_SIZE : len;
	memcpy(msg.request, buf, msg.req_len);

	/* Enqueue for work queue processing */
	if (k_msgq_put(&dap_cmd_msgq, &msg, K_NO_WAIT) != 0) {
		LOG_WRN("DAP command queue full, dropping request");
		return -ENOMEM;
	}

	/* Schedule work */
	k_work_submit(&dap_work);

	return 0;
}

static void dap_output_report(const struct device *dev, const uint16_t len,
			      const uint8_t *const buf)
{
	/* Interrupt OUT pipe received new report */
	dap_set_report(dev, HID_REPORT_TYPE_OUTPUT, 0, len, buf);
}

static void dap_set_protocol(const struct device *dev, const uint8_t proto)
{
	LOG_INF("HID protocol: %s",
		proto == 0U ? "Boot" : "Report");
}

static const struct hid_device_ops hid_dap_ops = {
	.iface_ready = dap_iface_ready,
	.get_report = dap_get_report,
	.set_report = dap_set_report,
	.output_report = dap_output_report,
	.set_protocol = dap_set_protocol,
};

int hid_dap_init(void)
{
	int ret;

	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID device not ready");
		return -ENODEV;
	}

	/* Register HID device with CMSIS-DAP report descriptor */
	ret = hid_device_register(hid_dev,
				  hid_report_desc, sizeof(hid_report_desc),
				  &hid_dap_ops);
	if (ret != 0) {
		LOG_ERR("Failed to register HID device: %d", ret);
		return ret;
	}

	/* Initialize DAP work item */
	k_work_init(&dap_work, dap_work_handler);

	LOG_INF("HID-DAP adapter initialized");
	return 0;
}

bool hid_dap_is_ready(void)
{
	return hid_ready;
}
```

- [ ] **Step 3: 更新 CMakeLists.txt**

```cmake
target_sources(app PRIVATE
    src/main.c
    src/hal/dap_gpio.c
    src/hal/dap_delay.c
    src/dap/DAP.c
    src/dap/SW_DP.c
    src/usb/usbd_context.c
    src/usb/hid_dap.c
)
```

- [ ] **Step 4: 验证编译**

Run: `west build -b <board> . 2>&1 | tail -30`

Expected: 编译通过。`DEVICE_DT_GET_ONE(zephyr_hid_device)` 需要设备树中有 HID 节点（见 Task 9）。

- [ ] **Step 5: 提交**

```bash
git add src/usb/hid_dap.h src/usb/hid_dap.c CMakeLists.txt
git commit -m "feat(usb): add HID-DAP adapter with work queue processing"
```

---

## Task 8: 主程序入口

整合所有模块，实现完整的初始化序列。

**Files:**
- Modify: `src/main.c`
- Modify: `src/usb/usbd_context.c` (取消注释 HID 类注册)

- [ ] **Step 1: 更新 usbd_context.c 中的类注册**

在 `daplink_usbd_init()` 函数中，取消注释并添加 HID 类注册：

```c
/* Register HID DAP class */
err = usbd_register_class(&daplink_usbd, "hid_dap", 1);
if (err) {
	LOG_ERR("Failed to register HID DAP class (%d)", err);
	return err;
}
```

同时在 `usbd_context.c` 顶部添加：
```c
#include "hid_dap.h"
```

- [ ] **Step 2: 重写 main.c**

```c
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DAPLink Zephyr Port - CMSIS-DAP Debug Probe
 *
 * Phase 1: HID debugging interface
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "dap_gpio.h"
#include "dap_delay.h"
#include "DAP.h"
#include "usbd_context.h"
#include "hid_dap.h"

LOG_MODULE_REGISTER(daplink, LOG_LEVEL_INF);

int main(void)
{
	int ret;

	LOG_INF("DAPLink Zephyr starting...");

	/* Step 1: Initialize HAL */
	ret = dap_gpio_init();
	if (ret != 0) {
		LOG_ERR("GPIO init failed: %d", ret);
		return ret;
	}

	dap_delay_init();

	/* Step 2: Initialize DAP core */
	DAP_Setup();
	LOG_INF("CMSIS-DAP initialized");

	/* Step 3: Initialize HID-DAP adapter */
	ret = hid_dap_init();
	if (ret != 0) {
		LOG_ERR("HID-DAP init failed: %d", ret);
		return ret;
	}

	/* Step 4: Initialize USB device */
	ret = daplink_usbd_init();
	if (ret != 0) {
		LOG_ERR("USB device init failed: %d", ret);
		return ret;
	}

	/* Step 5: Enable USB */
	struct usbd_context *usbd = daplink_usbd_ctx();
	if (!usbd_can_detect_vbus(usbd)) {
		ret = usbd_enable(usbd);
		if (ret != 0) {
			LOG_ERR("USB enable failed: %d", ret);
			return ret;
		}
	}

	LOG_INF("DAPLink ready - CMSIS-DAP v2 HID interface active");

	/* Main loop - nothing to do for Phase 1, all handled by callbacks */
	while (true) {
		k_sleep(K_MSEC(1000));
	}

	return 0;
}
```

- [ ] **Step 3: 验证编译**

Run: `west build -b <board> . 2>&1 | tail -30`

Expected: 编译和链接通过。

- [ ] **Step 4: 提交**

```bash
git add src/main.c src/usb/usbd_context.c
git commit -m "feat: wire up main initialization with HAL, DAP, and USB"
```

---

## Task 9: 板级设备树 Overlay

创建设备树 overlay 模板，定义 SWD 引脚和 HID 设备节点。

**Files:**
- Create: `boards/overlay_template.txt` (参考模板，实际使用时复制为 <board>.overlay)

- [ ] **Step 1: 创建 STM32 系列参考 overlay**

文件 `boards/stm32_daplink.overlay`：

```dts
/*
 * DAPLink Zephyr Board Overlay - STM32 Reference
 *
 * Copy and modify this file for your specific board.
 * Place in boards/<your_board>.overlay or use via:
 *   west build -b <board> -- -DDTC_OVERLAY_FILE=boards/stm32_daplink.overlay
 */

/ {
	daplink {
		compatible = "daplink,gpio";

		/* SWD clock pin - push-pull output */
		swclk-gpios = <&gpioB 13 0>;

		/* SWD data pin - bidirectional */
		swdio-gpios = <&gpioB 14 0>;

		/* Optional: separate SWDIO input pin */
		/* swdio-in-gpios = <&gpioB 12 0>; */

		/* Target reset pin - open drain with pull-up */
		nreset-gpios = <&gpioB 0 (GPIO_OPEN_DRAIN | GPIO_ACTIVE_LOW)>;
	};
};

/* HID device node for CMSIS-DAP */
/ {
	daplink_hid: hid_dev {
		compatible = "zephyr,hid-device";
		in-report-size = <64>;
		in-polling-period-us = <1000>;
		out-report-size = <64>;
		out-polling-period-us = <1000>;
	};
};
```

- [ ] **Step 2: 创建 Nordic nRF52 系列参考 overlay**

文件 `boards/nrf52_daplink.overlay`：

```dts
/*
 * DAPLink Zephyr Board Overlay - nRF52840 Reference
 */

/ {
	daplink {
		compatible = "daplink,gpio";
		swclk-gpios = <&gpio0 11 0>;
		swdio-gpios = <&gpio0 12 0>;
		nreset-gpios = <&gpio0 13 (GPIO_OPEN_DRAIN | GPIO_ACTIVE_LOW)>;
	};
};

/ {
	daplink_hid: hid_dev {
		compatible = "zephyr,hid-device";
		in-report-size = <64>;
		in-polling-period-us = <1000>;
		out-report-size = <64>;
		out-polling-period-us = <1000>;
	};
};
```

- [ ] **Step 3: 提交**

```bash
git add boards/
git commit -m "feat(boards): add device tree overlay templates for STM32 and nRF52"
```

---

## Task 10: 集成验证

在目标板上烧录固件，通过 pyOCD 验证 CMSIS-DAP 功能。

**前置条件：**
- 目标板有 USB Device 支持
- 已按照 Task 9 配置正确的 overlay
- 安装 pyOCD：`pip install pyocd`

- [ ] **Step 1: 构建并烧录固件**

```bash
west build -b <board> -- -DDTC_OVERLAY_FILE=boards/<your_overlay>.overlay
west flash
```

Expected: 固件烧录成功，USB 设备枚举。

- [ ] **Step 2: 验证 USB 枚举**

在主机上检查 USB 设备：
```bash
lsusb | grep -i "dap\|0d28"
```

Expected: 看到 `ID 0d28:0204 ARM DAPLink CMSIS-DAP` 设备。

或检查 `dmesg`：
```bash
dmesg | tail -20
```

Expected: USB HID 设备枚举日志。

- [ ] **Step 3: 验证 pyOCD 连接**

```bash
pyocd list
```

Expected: 列出 DAPLink CMSIS-DAP 设备。

```bash
pyocd cmd -t <target_mcu>
```

Expected: 进入 pyOCD 命令行模式。

- [ ] **Step 4: 验证基本 SWD 操作**

在 pyOCD 命令行中：

```
> dap info
> read32 0x00000000
```

Expected: 成功读取目标 MCU 的 IDCODE 或 Flash 起始地址内容。

- [ ] **Step 5: 如果验证失败，排查清单**

1. **USB 不枚举**：检查设备树 `zephyr_udc0` 节点、USB 时钟配置、pinctrl
2. **USB 枚举但 pyOCD 不识别**：检查 VID/PID、HID 报告描述符是否与 CMSIS-DAP 规范匹配
3. **pyOCD 连接但 SWD 失败**：检查 SWD 引脚配置、延时校准、目标 MCU 是否已连接并上电
4. **SWD 时序不稳定**：降低 SWD 时钟频率（修改 `DAP_DEFAULT_SWJ_CLOCK`）

- [ ] **Step 6: 提交最终状态**

```bash
git add -A
git commit -m "feat: complete Phase 1 - HID CMSIS-DAP debug probe functional"
```

---

## 故障排查指南

### 编译问题

| 问题 | 解决方案 |
|------|---------|
| `DAP_config.h` 找不到 | 确保 CMakeLists.txt 中 `target_include_directories` 包含 `src/dap` |
| `__STATIC_FORCEINLINE` 未定义 | 在 DAP_config.h 顶部添加兼容宏定义 |
| `DAP_PACKET_SIZE` 重定义 | 确保 DAP_config.h 只被包含一次，检查 include 守卫 |
| `usb_def.h` 找不到 | DAP.c/SW_DP.c 不应包含此头文件，检查引入的代码 |
| `DEVICE_DT_GET_ONE` 找不到节点 | 确保设备树 overlay 包含 `zephyr,hid-device` 节点 |

### 运行时问题

| 问题 | 解决方案 |
|------|---------|
| USB 不枚举 | 检查 `zephyr_udc0` 设备树节点和 USB 驱动 |
| HID 回调不触发 | 检查 `out-report-size` 设备树属性是否设置 |
| DAP 命令无响应 | 检查工作队列是否正常运行，增加日志级别 |
| SWD 通信失败 | 降低时钟频率，检查引脚连接，增加 `PIN_DELAY_FAST` 延时 |

### 下一步（Phase 2）

第一阶段完成后，继续实施：
- Phase 2: CDC 串口透传（`cdc_uart.c`）
- Phase 3: MSC 拖放烧录（`msc_vfs.c` + VFS）
- Phase 4: Bulk 传输 + JTAG 支持
