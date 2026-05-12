# DAPLink Zephyr 移植设计文档

## 概述

将 ARM DAPLink（CMSIS-DAP 调试探针）移植到 Zephyr RTOS，利用 Zephyr USB 新栈（device_next）替换 DAPLink 原有 USB 层，保留 DAPLink 核心协议代码不变。

**移植方案：** 方案 A — 直接移植 DAPLink 核心层

**目标硬件：** 通用（通过设备树 overlay 适配具体平台）

**功能范围：** HID 调试接口、CDC 虚拟串口、MSC 拖放烧录、Bulk 传输

**目标芯片支持：** 分阶段实现，第一阶段仅通用 SWD

---

## 整体架构

```
┌─────────────────────────────────────────────┐
│  Zephyr App (main.c)                        │
├─────────────────────────────────────────────┤
│  USB Adapter Layer                          │
│  ├─ hid_dap.c    (HID → DAP 适配)          │
│  ├─ cdc_uart.c   (CDC → UART 适配)         │
│  ├─ msc_vfs.c    (disk_access → VFS 适配)  │
│  └─ bulk_dap.c   (Bulk → DAP 适配)         │
├─────────────────────────────────────────────┤
│  Zephyr USB New Stack                       │
│  (HID / CDC ACM / MSC / Bulk)               │
├─────────────────────────────────────────────┤
│  DAPLink Core (直接引入，最小修改)           │
│  ├─ DAP.c / SW_DP.c / JTAG_DP.c / SWO.c    │
│  ├─ DAP_queue.c                             │
│  ├─ DAP_UART.c                              │
│  └─ virtual_fs / vfs_manager / flash_mgr    │
├─────────────────────────────────────────────┤
│  HAL Adapter (DAP_config.h → Zephyr API)    │
│  ├─ GPIO (SWD/JTAG 引脚)                    │
│  ├─ Timer (精确延时)                         │
│  └─ UART (串口透传)                         │
└─────────────────────────────────────────────┘
```

---

## 目录结构

```
daplink-zephyr/
├── CMakeLists.txt              # Zephyr 应用构建配置
├── prj.conf                    # Kconfig 默认配置
├── boards/                     # 板级 overlay 和配置
│   └── <board>.overlay         # 设备树（SWD 引脚、USB、HID、CDC）
├── src/
│   ├── main.c                  # 入口：初始化 USB 和 DAP
│   ├── dap/                    # DAPLink 核心（直接引入）
│   │   ├── DAP.c               # CMSIS-DAP 协议核心
│   │   ├── DAP.h
│   │   ├── DAP_UART.c
│   │   ├── DAP_queue.c
│   │   ├── DAP_queue.h
│   │   ├── SW_DP.c             # SWD 协议
│   │   ├── JTAG_DP.c           # JTAG 协议
│   │   ├── SWO.c               # SWO 传输
│   │   └── DAP_config.h        # 硬件抽象适配（重写）
│   ├── usb/                    # USB 适配层
│   │   ├── usbd_context.c      # USB 设备描述符和初始化
│   │   ├── hid_dap.c           # HID → DAP 适配器
│   │   ├── cdc_uart.c          # CDC ACM → UART 透传
│   │   ├── msc_vfs.c           # disk_access 驱动 → VFS 适配
│   │   └── bulk_dap.c          # Bulk → DAP 适配器
│   ├── vfs/                    # 虚拟文件系统（从 DAPLink 引入）
│   │   ├── virtual_fs.c
│   │   ├── virtual_fs.h
│   │   ├── vfs_manager.c
│   │   ├── vfs_manager.h
│   │   └── stream/
│   │       ├── flash_decoder.c
│   │       ├── flash_manager.c
│   │       └── ...
│   ├── target/                 # 目标芯片支持（后续阶段）
│   │   ├── target_flash.c
│   │   └── ...
│   └── hal/                    # Zephyr HAL 适配
│       ├── dap_gpio.c          # SWD/JTAG GPIO 操作
│       ├── dap_gpio.h
│       ├── dap_delay.c         # 精确延时
│       ├── dap_delay.h
│       ├── target_uart.c       # 目标 UART 透传
│       └── target_uart.h
```

---

## DAP 核心移植

### 引入文件清单

| 文件 | 修改程度 | 说明 |
|------|---------|------|
| DAP.c | 无修改 | CMSIS-DAP 协议核心，纯逻辑 |
| DAP.h | 无修改 | 接口定义 |
| SW_DP.c | 无修改 | SWD 协议实现 |
| JTAG_DP.c | 无修改 | JTAG 协议实现 |
| SWO.c | 无修改 | SWO 传输 |
| DAP_UART.c | 无修改 | DAP UART 通信 |
| DAP_queue.c/h | 无修改 | 命令队列管理 |
| DAP_config.h | **重写** | 硬件抽象，适配 Zephyr GPIO |

### DAP_config.h 适配

**编译配置：**
- `CPU_CLOCK` → `CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC`
- `DAP_PACKET_SIZE` = 64（Full-Speed USB）
- `DAP_PACKET_COUNT` = 1
- `DAP_SWD` = 1, `DAP_JTAG` = 0（第一阶段）
- `DAP_DEFAULT_PORT` = 1（默认 SWD）
- `DAP_DEFAULT_SWJ_CLOCK` = 1000000（1MHz）

**引脚操作宏 → Zephyr GPIO 映射：**

所有 SWD/JTAG 引脚统一使用 `_raw` API 操作物理电平，不经过 Zephyr 逻辑电平转换。nRESET 使用 `gpio_pin_set_dt()` 配合设备树 GPIO flags 处理开漏和 Active Low 逻辑。

| DAP 宏 | Zephyr API |
|--------|-----------|
| `PIN_SWCLK_TCK_SET()` | `gpio_pin_set_raw(clk_dev, clk_pin, 1)` |
| `PIN_SWCLK_TCK_CLR()` | `gpio_pin_set_raw(clk_dev, clk_pin, 0)` |
| `PIN_SWDIO_TMS_IN()` | `gpio_pin_get_raw(dio_dev, dio_pin)` |
| `PIN_SWDIO_TMS_SET()` | `gpio_pin_set_raw(dio_dev, dio_pin, 1)` |
| `PIN_SWDIO_TMS_CLR()` | `gpio_pin_set_raw(dio_dev, dio_pin, 0)` |
| `PIN_SWDIO_OUT_ENABLE()` | `gpio_pin_configure(dio_dev, dio_pin, GPIO_OUTPUT)` |
| `PIN_SWDIO_OUT_DISABLE()` | `gpio_pin_configure(dio_dev, dio_pin, GPIO_INPUT)` |
| `PIN_nRESET_IN()` | `gpio_pin_get_raw(nreset_dev, nreset_pin)` |
| `PIN_nRESET_OUT()` | `gpio_pin_set_dt(nreset_spec, value)` |

**SWDIO 双向引脚说明：** 某些硬件设计中 SWDIO 输入和输出使用不同引脚。设备树中 `swdio-gpios` 定义输出引脚，`swdio-in-gpios` 定义输入引脚（可选）。当 `swdio-in-gpios` 存在时，`PIN_SWDIO_TMS_IN()` 读取输入引脚而非输出引脚。DAP_config.h 通过条件编译自动适配。

**延时函数：**
- 慢速延时：`k_busy_wait(n)` 实现微秒级
- 快速延时：`arch_nop()` 内联循环实现纳秒级（SWD 时钟节拍）。使用 Zephyr 可移植的 `arch_nop()` 而非 ARM 专用 `__NOP()`
- 注意：`k_busy_wait()` 在中断上下文中精度依赖硬件定时器，SWD 操作期间需确保中断不影响时序

### HAL 层（src/hal/）

- **dap_gpio.c**：从设备树获取引脚配置并初始化 GPIO
- **dap_delay.c**：基于 `k_busy_wait()` 和 `arch_nop()` 循环的精确延时
- **target_uart.c**：Zephyr UART 驱动 API 实现串口透传

### 设备树 overlay

```dts
/ {
    daplink {
        compatible = "daplink,gpio";
        swclk-gpios = <&gpioB 13 0>;
        swdio-gpios = <&gpioB 14 0>;
        swdio-in-gpios = <&gpioB 12 0>;  /* 可选：独立输入引脚 */
        nreset-gpios = <&gpioB 0 (GPIO_OPEN_DRAIN | GPIO_ACTIVE_LOW)>;
        led-connect-gpios = <&gpioA 9 0>;
        led-running-gpios = <&gpioA 10 0>;
    };
};

/* HID 设备节点：CMSIS-DAP 自定义 HID */
daplink_hid_0: hid_dev_0 {
    compatible = "zephyr,hid-device";
    in-report-size = <64>;
    in-polling-period-us = <1000>;
    out-report-size = <64>;
    out-polling-period-us = <1000>;
};

/* CDC ACM 节点：虚拟串口 */
daplink_cdc_0: cdc_acm_uart_0 {
    compatible = "zephyr,cdc-acm-uart";
};
```

更换目标板只需修改 overlay，无需改动 C 代码。注意不同平台可能需要额外的 USB PHY 初始化、时钟配置或引脚复用（AF）设置，这些在板级 overlay 中通过 pinctrl 节点配置。

---

## USB 适配层

### USB 复合设备配置

```
USB Composite Device (VID=0x0D28, PID=0x0204)
├── Interface 0:    HID (CMSIS-DAP 调试)
│   ├── EP 1 IN  (Interrupt, 64 bytes)
│   └── EP 1 OUT (Interrupt, 64 bytes)
├── Interface 1-2:  CDC ACM (虚拟串口)
│   ├── EP 2 IN  (Interrupt, 通知)
│   ├── EP 3 IN  (Bulk, 数据)
│   └── EP 3 OUT (Bulk, 数据)
├── Interface 3:    MSC (拖放烧录)
│   ├── EP 4 IN  (Bulk, 数据)
│   └── EP 4 OUT (Bulk, 数据)
└── Interface 4:    Bulk (DAPv2，第四阶段)
    ├── EP 5 IN  (Bulk, 512 bytes)
    └── EP 5 OUT (Bulk, 512 bytes)
```

### USB 设备初始化序列（usbd_context.c）

Zephyr 新栈使用宏定义 + 函数调用的方式注册 USB 设备，关键序列如下：

```c
/* 1. 定义设备上下文 */
USBD_DEVICE_DEFINE(daplink_usbd,
                   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   0x0D28, 0x0204);

/* 2. 定义字符串描述符 */
USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "ARM");
USBD_DESC_PRODUCT_DEFINE(product, "DAPLink CMSIS-DAP");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);

/* 3. 定义配置描述符 */
USBD_CONFIGURATION_DEFINE(daplink_fs_config,
                         USBD_ATTR_SELF_POWERED,
                         250, &fs_cfg_desc);

/* 4. 初始化函数中 */
usbd_add_configuration(&daplink_usbd, &daplink_fs_config);
usbd_register_class(&daplink_usbd, "hid_dap", 1);
usbd_register_class(&daplink_usbd, "cdc_acm_0", 1);
usbd_register_class(&daplink_usbd, "msc_0", 1);
usbd_init(&daplink_usbd);
usbd_enable(&daplink_usbd);
```

### HID → DAP 适配器（hid_dap.c）

HID 报告描述符使用 Vendor-Defined Usage Page（0xFF00），Report Size = 8, Report Count = 64，与 CMSIS-DAP 规范一致。

**数据流：**
1. 主机发送 HID OUT Report → `output_report()` 回调（USB 系统线程上下文）
2. 回调将请求数据入队到 DAP 命令队列
3. 工作线程取出命令，调用 `DAP_ExecuteCommand(request, response)` 处理
4. 处理完成后通过 `hid_device_submit_report()` 发送响应

**线程安全设计：**

HID 和 Bulk 接口都调用 `DAP_ExecuteCommand()`，而 DAP 命令执行涉及 SWD 总线操作（GPIO 引脚操作），必须保证互斥。使用 `struct k_mutex dap_bus_mutex` 保护所有 DAP 命令执行和 SWD/JTAG 引脚操作。

**DAP 命令执行策略：**

Zephyr USB 新栈文档明确指出 "Callbacks are called from the USB device stack thread and must not block"。因此 DAP 命令不在 USB 回调中直接执行，而是采用工作队列模式：

- USB 回调中仅将请求数据入队（`k_msgq_put`）
- 专用工作线程（`k_work_queue`）取命令并执行 `DAP_ExecuteCommand()`
- 执行完毕后通过 `hid_device_submit_report()` 提交响应

**关键回调：**
- `output_report()`：接收命令 → 入队到 DAP 工作队列
- `iface_ready()`：USB 连接/断开状态管理，处理 suspend/resume
- `get_report()`：返回缓存的 DAP 响应

### CDC → UART 透传（cdc_uart.c）

双向数据转发：USB CDC ACM ↔ 物理 UART。

- 使用 Zephyr CDC ACM UART 驱动（`zephyr,cdc-acm-uart` 兼容节点）
- UART 端使用中断驱动模式（`uart_irq_rx_ready` / `uart_irq_callback_set`）
- 线路编码同步：CDC 波特率变化时重新配置物理 UART
- 主循环中轮询转发数据，避免在中断中做耗时操作

### MSC → VFS 适配器（msc_vfs.c）

**方案：自定义 disk_access 驱动后端**

Zephyr MSC SCSI 层通过 `disk_access_*` API 访问块设备，与 `disk_access` 框架深度耦合。因此最可行的路径是实现一个自定义 disk_access 驱动，将扇区读写映射到 DAPLink VFS：

```
MSC SCSI 层 (Zephyr 内置)
    ↓ disk_access_read() / disk_access_write()
disk_access 框架
    ↓ struct disk_operations 回调
msc_vfs.c (自定义 disk 驱动)
    ↓ vfs_read() / vfs_write()
DAPLink VFS (virtual_fs.c + vfs_manager.c)
```

**自定义 disk 驱动实现：**

```c
/* msc_vfs.c 中实现 */
static const struct disk_operations daplink_disk_ops = {
    .init   = daplink_disk_init,
    .status = daplink_disk_status,
    .read   = daplink_disk_read,    /* → vfs_read() */
    .write  = daplink_disk_write,   /* → vfs_write() */
    .ioctl  = daplink_disk_ioctl,
};

/* 注册到 disk_access 框架 */
disk_access_register(&daplink_disk_info);

/* MSC 通过 USBD_DEFINE_MSC_LUN 使用该磁盘 */
USBD_DEFINE_MSC_LUN(daplink_lun, "DAPLINK", "ARM", "DAPLink", "1.00");
```

理由：DAPLink VFS 管理器有复杂状态机（传输进度、文件变化检测、超时处理），通过 disk_access 标准接口可以完整保留这些逻辑，同时利用 Zephyr MSC SCSI 子系统处理 BBB 协议和标准 SCSI 命令。

**线程安全：** MSC SCSI 处理在 MSC 工作队列线程中运行，VFS 状态可能被其他上下文修改（如 USB 重连事件）。VFS 操作需要通过 `k_mutex` 保护。

### Bulk → DAP 适配器（bulk_dap.c）

DAPv2 Bulk 传输接口。Zephyr 无内置 Bulk 类驱动，需自定义 USB 类：

**实现方式：**
- 使用 `USBD_DEFINE_CLASS()` 宏注册自定义 Bulk 类
- 手动定义 Bulk IN/OUT 端点描述符
- 实现 `usbd_class_api` 回调：`enable`、`disable`、`request`、`update`
- 数据流与 HID 适配器类似，共享同一个 DAP 工作队列
- 同样使用 `dap_bus_mutex` 保护 DAP 命令执行
- Bulk 端点支持更大包（High-Speed 时 512 字节），吞吐量高于 HID

**端点配置：**
- Bulk OUT EP：接收主机命令
- Bulk IN EP：发送 DAP 响应
- 包大小：Full-Speed 64 字节，High-Speed 512 字节

### 数据流总结

```
主机工具              串口终端           文件管理器
  │ HID                │ CDC              │ MSC
  ▼                    ▼                  ▼
┌─────────────────────────────────────────────┐
│           Zephyr USB New Stack              │
├─────────┬───────────┬──────────┬────────────┤
│hid_dap  │ cdc_uart  │ msc_vfs  │ bulk_dap   │
│  ↓入队  │  轮询转发 │ disk_ops │  ↓入队    │
├─────────┴───────────┼──────────┴────────────┤
│  DAP 工作队列       │  VFS Manager          │
│  (k_work_queue)     │  Flash Manager        │
│  ↓ dap_bus_mutex    │                        │
├─────────────────────┼───────────────────────┤
│  DAP_ExecuteCommand │  virtual_fs           │
│  SW_DP / JTAG_DP    │                        │
├─────────────────────┴───────────────────────┤
│          HAL (GPIO / Delay / UART)          │
└─────────────────────────────────────────────┘
```

---

## 主程序与初始化

### 初始化流程

```
main()
  ├── hal_init()              // GPIO、延时、UART
  ├── DAP_Setup()             // DAP 核心初始化
  ├── vfs_init()              // 虚拟文件系统初始化（MSC 阶段）
  ├── daplink_disk_register() // 注册自定义 disk 驱动（MSC 阶段）
  ├── usbd_context_init()     // USB 设备栈初始化
  │     ├── USBD_DEVICE_DEFINE()
  │     ├── USBD_CONFIGURATION_DEFINE()
  │     ├── usbd_add_configuration()
  │     ├── usbd_register_class() × N
  │     ├── usbd_init()
  │     └── usbd_enable()
  └── 主循环
        ├── cdc_uart_poll()   // CDC↔UART 数据转发
        ├── LED 状态管理
        └── k_sleep(K_MSEC(1))
```

初始化严格线性，USB 使能放在最后，确保所有子系统就绪后才响应主机枚举。

### 线程模型

| 线程 | 优先级 | 职责 |
|------|--------|------|
| USB 系统线程 | 高 | USB 栈内部管理，端点回调（不执行 DAP 命令） |
| DAP 工作队列 | 普通 | 执行 DAP 命令、SWD/JTAG 操作 |
| 主线程 | 普通 | CDC↔UART 轮询、LED 管理 |
| MSC 工作队列 | 普通 | SCSI 命令处理（Zephyr 内置） |

**互斥保护：**
- `dap_bus_mutex`：保护 DAP 命令执行和 SWD/JTAG 引脚操作，HID 和 Bulk 共享
- `vfs_mutex`：保护 VFS 状态，MSC 工作队列和主循环共享

---

## 构建系统

### CMakeLists.txt

引入所有源文件，include 路径覆盖 src/、src/dap/、src/usb/、src/vfs/、src/hal/。对引入的 DAPLink 源文件按需添加 `-Wno-*` 编译选项以抑制与 Zephyr 编码风格不一致产生的警告。

### prj.conf

```ini
# USB 新栈
CONFIG_USB_DEVICE_STACK_NEXT=y
CONFIG_UDC_DRIVER=y
CONFIG_USBD_MAX_SPEED_FULL=y

# HID (CMSIS-DAP)
CONFIG_USBD_HID_SUPPORT=y

# CDC ACM (虚拟串口)
CONFIG_USBD_CDC_ACM_CLASS=y
CONFIG_UART_INTERRUPT_DRIVEN=y

# MSC (拖放烧录)
CONFIG_USBD_MSC_CLASS=y
CONFIG_USBD_MSC_LUNS_PER_INSTANCE=1
CONFIG_DISK_ACCESS=y

# 系统基础
CONFIG_GPIO=y
CONFIG_SERIAL=y

# 内存配置
CONFIG_HEAP_MEM_POOL_SIZE=8192
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_USB_MSC_WORKQUEUE_STACK_SIZE=2048
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048

# 日志（调试用，发布时关闭）
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=2
```

### 内存预算评估

在资源受限 MCU（如 STM32F103, 20KB RAM）上需注意：

| 组件 | 估算 RAM 占用 |
|------|-------------|
| USB 端点缓冲区 | ~1.5 KB (4 端点 × 64 字节 × 双缓冲) |
| DAP 包缓冲区 | ~128 字节 (1 × 64 + 响应) |
| DAP 工作队列栈 | ~1 KB |
| MSC SCSI 缓冲区 | ~512 字节 (1 扇区) |
| CDC 环形缓冲区 | ~256 字节 |
| 堆内存 | ~4 KB |
| **合计** | **~7.5 KB** |

STM32F103 (20KB RAM) 足够运行全部四个接口。第一、二阶段（仅 HID + CDC）仅约 3 KB，余量更大。

---

## 分阶段实施计划

### 第一阶段：最小可用（HID 调试）

目标：pyOCD/OpenOCD 能通过 USB HID 连接并执行 SWD 读写。

1. 创建项目骨架：CMakeLists.txt、prj.conf、空 main.c
2. 实现 HAL 层：dap_gpio.c、dap_delay.c、DAP_config.h
3. 引入 DAP 核心：DAP.c、SW_DP.c、DAP_queue.c
4. 实现 HID 适配：hid_dap.c、usbd_context.c（含工作队列模式）
5. 验证：连接 pyOCD，执行 dap info、read32

### 第二阶段：CDC 串口透传

目标：USB 虚拟串口 ↔ 目标 MCU UART 双向透传。

1. 实现 target_uart.c
2. 实现 cdc_uart.c
3. 验证：串口终端收发数据

### 第三阶段：MSC 拖放烧录

目标：拖放 HEX/BIN 文件到虚拟 U 盘，自动烧录目标芯片。

1. 引入 VFS：virtual_fs.c、vfs_manager.c
2. 实现自定义 disk_access 驱动：msc_vfs.c
3. 引入 Flash 解码：flash_decoder.c、flash_manager.c
4. 引入至少一个目标芯片族（如 STM32F1）
5. 验证：拖放 HEX 文件烧录

### 第四阶段：Bulk 传输 + 完善

目标：DAPv2 Bulk 接口、JTAG 支持、多目标芯片族。

1. 实现 bulk_dap.c（自定义 USBD_DEFINE_CLASS）
2. 启用 JTAG 支持（引入 JTAG_DP.c、实现 JTAG 引脚）
3. 添加更多目标芯片族
4. 性能优化：DMA、双缓冲、中断优先级调整

---

## 风险与对策

| 风险 | 对策 |
|------|------|
| SWD 时序精度不足 | `arch_nop()` 精确循环；实测校准循环次数；必要时禁用中断保护关键时序段 |
| USB 回调中 DAP 命令耗时过长 | 工作队列模式：USB 回调仅入队，DAP 命令在专用线程执行 |
| MSC 自定义后端集成 | 通过标准 disk_access 驱动接口适配，利用 Zephyr SCSI 子系统 |
| DAPLink 代码编译警告 | 按文件粒度 `-Wno-*` 抑制；逐步清理 |
| Zephyr 新栈 API 不稳定 | 锁定 Zephyr 版本，跟进上游变化 |
| DAPLink 头文件与 Zephyr 头文件冲突 | DAPLink 代码使用独立 include 路径；必要时在 DAP_config.h 中 include Zephyr 头文件而非 CMSIS 头文件 |
| USB 线程安全 | `dap_bus_mutex` 保护 DAP/SWD 操作；`vfs_mutex` 保护 VFS 状态 |
| 设备树跨平台可移植性 | 核心引脚通过 overlay 配置；平台特有需求（pinctrl、时钟）在板级 overlay 中处理 |
| USB suspend/resume 和断连重连 | `iface_ready()` 回调管理连接状态；DAP 命令队列自动排空；VFS 状态机处理重连 |
| 资源受限 MCU 内存不足 | 分阶段启用功能；第一、二阶段仅 ~3KB RAM |

## 设计约束

- CMSIS-DAP 协议版本：基于 DAPLink 源码（v2.x）
- USB 速度：初期仅 Full-Speed（12Mbps），包大小 64 字节。架构预留 High-Speed 扩展（Bulk 512 字节）
- 目标调试端口：第一阶段仅 SWD，后续扩展 JTAG
- Zephyr 版本：基于当前 ../zephyr 代码库版本
