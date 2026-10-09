# DAPLink Zephyr 原理文档

## 概述

DAPLink 是 ARM 官方的开源调试探针固件，本项目的目标是将 DAPLink 的核心功能移植到 Zephyr RTOS 上，利用 Zephyr 的 USB 设备栈实现复合设备（HID + CDC + MSC），同时保留 CMSIS-DAP 协议兼容性和拖放烧写能力。

### 与官方 DAPLink 的区别

| 特性 | 官方 DAPLink | DAPLink Zephyr |
|------|-------------|----------------|
| 运行环境 | 裸机 (HAL) | Zephyr RTOS |
| USB 栈 | 自实现 | Zephyr USBD (new stack) |
| 复合设备 | 手动枚举 | Zephyr 声明式 |
| 目标检测 | 编译时固定 | 运行时自动检测 |
| 文件系统 | 自实现 FAT | 自定义 disk_operations |
| 功耗管理 | 无 | 可接入 Zephyr PM |

---

## 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                      USB Host (PC)                      │
│   pyOCD / OpenOCD / Keil   │  Terminal  │  File Manager│
└──────────┬──────────────────┼────────────┼──────────────┘
           │ HID              │ CDC ACM    │ MSC
┌──────────┴──────────────────┴────────────┴──────────────┐
│               Zephyr USB Device Stack                    │
│  ┌─────────┐  ┌─────────────┐  ┌──────────────────────┐│
│  │ HID Class│  │ CDC ACM Class│  │ MSC Class            ││
│  └────┬────┘  └──────┬──────┘  └──────────┬───────────┘│
└───────┼──────────────┼─────────────────────┼────────────┘
        │              │                     │
┌───────┼──────────────┼─────────────────────┼────────────┐
│       ▼              ▼                     ▼            │
│  ┌─────────┐  ┌──────────┐  ┌───────────────────────┐  │
│  │ HID-DAP │  │UART Bridge│  │  VFS Disk Driver     │  │
│  │ Adapter │  │           │  │  (struct disk_ops)    │  │
│  └────┬────┘  └─────┬────┘  └───────────┬───────────┘  │
│       │             │                    │              │
│       ▼             │                    ▼              │
│  ┌──────────┐       │         ┌───────────────────┐    │
│  │ DAP Core │       │         │ FAT16 Filesystem  │    │
│  │ (DAP.c)  │       │         │ (in-memory image) │    │
│  └────┬─────┘       │         └───────┬───────────┘    │
│       │             │                 │ CTRL_SYNC      │
│       ▼             │                 ▼                │
│  ┌──────────┐  ┌────┴─────┐  ┌───────────────┐       │
│  │ SW_DP.c  │  │USART2    │  │Target Detect  │       │
│  │(bit-bang)│  │(HW UART) │  │(IDCODE match) │       │
│  └────┬─────┘  └──────────┘  └───────┬───────┘       │
│       │                              │                │
│       ▼                              ▼                │
│  ┌──────────┐               ┌──────────────────┐     │
│  │ SWD Host │               │ Flash Manager    │     │
│  │(swd_host)│◄──────────────┤ (flash_manager)  │     │
│  └────┬─────┘               └────────┬─────────┘     │
│       │                              │                │
│       ▼                              ▼                │
│  ┌──────────┐               ┌──────────────────┐     │
│  │ HAL GPIO │               │ Intel HEX Parser │     │
│  │ + Delay  │               │ (intelhex.c)     │     │
│  └──────────┘               └──────────────────┘     │
│                                                        │
│                   DAPLink Zephyr App                    │
└────────────────────────────────────────────────────────┘
                    │ SWD (GPIO)
                    ▼
            ┌──────────────┐
            │  Target MCU  │
            │  (被调试芯片) │
            └──────────────┘
```

---

## 模块详解

### 1. CMSIS-DAP 核心 (src/dap/)

CMSIS-DAP 是 ARM 定义的调试协议，通过 USB HID 传输 DAP 命令。

#### DAP.c — 命令分发器

- 接收 HID 报文，按 Command ID 分发处理
- 支持的命令：DAP_Info、DAP_HostStatus、DAP_Connect、DAP_Disconnect、DAP_Transfer、DAP_TransferBlock、DAP_WriteABORT、DAP_Delay、DAP_ResetTarget、DAP_SWJ_Pins、DAP_SWJ_Clock、DAP_SWJ_Sequence
- JTAG 命令（`DAP_JTAG != 0` 时）：DAP_JTAG_Sequence、DAP_JTAG_Configure、DAP_JTAG_IDCODE
- 每个 DAP 命令处理函数返回响应报文长度
- 从 ARM CMSIS_5 官方仓库移植，保持协议兼容

#### SW_DP.c — SWD 协议物理层

SWD (Serial Wire Debug) 是 ARM 的双线调试协议，使用 bit-bang GPIO 实现：

```
SWD 传输时序：
    ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐     ┌─┐ ┌─┐ ┌─┐
CLK ─┘ └─┘ └─┘ └─┘ └─┘     └─┘ └─┘ └─┘
DIO ─<Start><APnDP><RnW><Addr><Parity><Turn><Data...>
```

- **SWD_Transfer()**：执行一次完整的 SWD 读写事务
  - 发送 8-bit 请求包 (Start + APnDP + RnW + Addr[2:3] + Parity + Stop + Park)
  - 读取/写入 32-bit 数据 + Parity
  - 返回 ACK (OK/FAULT/WAIT)
- **SWJ_Sequence()**：发送任意位序列，用于 SWJ-DP 切换（JTAG → SWD）
- **SWD_Config()**：设置时钟分频（控制 bit-bang 速度）

时序由 `dap_delay.c` 的精确延时保证，默认约 1MHz SWD 时钟。

#### JTAG_DP.c — JTAG 协议物理层（可选）

与 SW_DP.c 并列，实现 CMSIS-DAP 的 JTAG 传输（TCK/TMS/TDI/TDO bit-bang）：

- **JTAG_Sequence()**：任意 JTAG 位序列（对应 DAP_JTAG_Sequence），TMS 电平固定、逐位移位 TDI 并捕获 TDO。
- **JTAG_IR()**：走 TAP 状态机（Select-DR → Select-IR → Capture-IR → Shift-IR → … → Update-IR），按 `DAP_Data.jtag_dev` 的 `ir_before/ir_length/ir_after` 处理多器件链（含 bypass）。
- **JTAG_Transfer()**：DPACC/APACC 读/写（含 ACK 生成、DAP_TRANSFER_RnW 数据阶段、idle_cycles）。
- **JTAG_ReadIDCode() / JTAG_WriteAbort()**：DP IDCODE 读 / ABORT 写。

`TCK/TMS 复用 SWCLK/SWDIO`，仅新增 `TDI`(输出) / `TDO`(输入) 两个引脚。整个 JTAG 传输由 `CONFIG_DAPLINK_JTAG` 门控（默认开；64KB Flash 小板建议关闭以省 ~9KB）。

#### DAP_config.h — Zephyr 适配层

将 CMSIS-DAP 核心的 GPIO 操作映射到 Zephyr API：
| CMSIS-DAP 宏 | Zephyr 实现 |
|--------------|-------------|
| `PIN_SWCLK_TCK_SET()` | `gpio_pin_set(swclk, 1)` |
| `PIN_SWDIO_TMS_SET()` | `gpio_pin_set(swdio, 1)` |
| `PIN_SWDIO_TMS_IN()` | `gpio_pin_get(swdio)` |
| `PIN_SWDIO_TMS_SET_PUSHPULL()` | 配置输出模式 |
| `PIN_SWDIO_TMS_SET_OPENDRAIN()` | 配置开漏模式 |
| `PIN_nRESET_OUT()` | `gpio_pin_set(nreset, val)` |
| `PORT_SWD_SETUP()` | 初始化 SWD GPIO |

### 2. USB 设备栈 (src/usb/)

使用 Zephyr USB new stack (device_next) 构建复合设备。

#### usbd_context.c — 复合设备定义

```c
USBD_DEVICE_DEFINE(daplink_usbd, ..., 0x1234, 0x5678);  // VID/PID
USBD_DESCRING_DEFINE(daplink_descring);                    // 描述符池

// 三个接口：
USBD_HID_DEFINE(hid_dev);                // Interface 0: HID
USBD_CDC_ACM_DEFINE(cdc_dev, 0);         // Interface 1+2: CDC ACM
USBD_MSFD_DEFINE(msfd_dev, "DAPLINK");    // Interface 3: MSC
USBD_DEFINE_MSC_LUN(lun0, "DAPLINK");    // MSC 后端：注册名 "DAPLINK" 的磁盘
```

设备描述符中配置：
- VID/PID: 0x1234/0x5678（可自定义）
- 配置 1: Composite (IAD for CDC)
- 字符串描述符：厂商、产品、序列号

#### hid_dap.c — HID-DAP 适配器

将 Zephyr HID 类回调连接到 CMSIS-DAP 核心：

1. HID OUT 中断端点收到数据 → 调用 `DAP_ExecuteCommand()` 处理
2. DAP 核心生成响应 → 通过 HID IN 中断端点发送回主机
3. 支持最大 64 字节 HID 报文（与 CMSIS-DAP v2 默认一致）

### 3. SWD Host 层 (src/flash/swd_host.c)

在 DAP 核心 `SWD_Transfer()` 之上封装高层 SWD 操作，用于目标 Flash 编程。

#### 寄存器访问

SWD 协议通过 DP (Debug Port) 和 AP (Access Port) 访问目标寄存器：

```
编码: Request = APnDP(bit0) | RnW(bit1) | Addr(bits2-3)

DP 寄存器:
  0x00 → DP_IDCODE (只读)
  0x00 → DP_ABORT  (只写)
  0x04 → DP_CTRL_STAT
  0x08 → DP_SELECT
  0x0C → DP_RDBUFF

AP 寄存器:
  0x00 → AP_CSW   (Control Status Word)
  0x04 → AP_TAR   (Transfer Address Register)
  0x0C → AP_DRW   (Data Read/Write)
```

AP 读写流程：
```
写 AP_CSW (32-bit, 自增模式)
  → 写 AP_TAR (目标地址)
  → 写/读 AP_DRW (数据)
```

#### 关键函数

**swd_init_debug()** — 建立 SWD 调试连接：

1. 发送 SWJ-DP 切换序列（JTAG → SWD）
2. 读 DP IDCODE 验证连接
3. 写 DP ABORT 清除粘性错误
4. 写 DP CTRL/STAT 上电请求
5. 轮询 CTRL/STAT 确认上电
6. 写 DHCSR 暂停目标
7. 写 DEMCR 启用向量捕获

**swd_flash_syscall_exec()** — 在目标上执行 Flash 算法：

这是最关键的函数，通过调试寄存器控制目标 CPU 执行 Flash 操作：

```
1. 设置目标寄存器:
   R0  = arg1 (参数1，如地址)
   R1  = arg2 (参数2，如大小)
   R9  = static_base (RO 数据段基址)
   R13 = stack_pointer (算法栈)
   R15 = entry (算法入口)
   xPSR = 0x01000000 (Thumb 模式)

2. 通过 DHCSR 恢复目标执行

3. 轮询 DHCSR.S_HALT 等待算法结束
   (算法执行 BKPT 指令停止)

4. 读 R0 检查返回值 (0 = 成功)
```

### 4. Flash 算法机制

#### Flash Blob

Flash 算法是一段 ARM Thumb 机器码，由编译器为特定芯片系列生成。DAPLink 从目标芯片的 RAM 中执行这段代码来操作 Flash 控制器。

```
目标 RAM 布局 (以 STM32F4 为例):
0x20000000 ┌────────────────────┐
           │ Flash Algorithm    │  algo_blob (下载到此处)
           │ (ARM Thumb code)   │  ~0x200+ bytes
0x20000214 ├────────────────────┤
           │ Algorithm RW Data  │  RSB 指向此处
           │                    │
0x20000A00 ├────────────────────┤
           │ Program Buffer     │  host 写入数据到此处
           │ (4KB)              │
0x20001A00 ├────────────────────┤
           │                    │
0x20002000 ├────────────────────┤
           │ Algorithm Stack    │  RSP 指向此处
           │ (向下增长)          │
           └────────────────────┘
```

#### 算法结构 (program_target_t)

```c
typedef struct {
    uint32_t init;            // Init 函数地址
    uint32_t uninit;          // UnInit 函数地址
    uint32_t erase_chip;      // EraseChip 函数地址
    uint32_t erase_sector;    // EraseSector 函数地址
    uint32_t program_page;    // ProgramPage 函数地址
    program_syscall_t sys_call_s;  // BKPT/RSB/RSP
    uint32_t program_buffer;  // 数据缓冲区地址
    uint32_t algo_start;      // 算法加载地址
    uint32_t algo_size;       // 算法大小
    const uint32_t *algo_blob; // 算法数据指针
    uint32_t program_buffer_size; // 页大小
} program_target_t;
```

每个算法包含 5 个入口函数，签名为：

```c
int Init(uint32_t flash_start, uint32_t clock_freq, uint32_t algo_flags);
int UnInit(uint32_t algo_flags);
int EraseChip(void);
int EraseSector(uint32_t sector_addr);
int ProgramPage(uint32_t page_addr, uint32_t size, const uint8_t *data);
```

#### 编程流程

```
flash_manager_init(target)
│
├─ swd_init_debug()                    // 建立 SWD 连接
├─ swd_write_memory(algo_start, blob)  // 下载算法到目标 RAM
└─ swd_flash_syscall_exec(Init)        // 调用算法 Init

flash_manager_write(addr, data, size)
│
├─ for each page:
│   ├─ if sector boundary:
│   │   └─ swd_flash_syscall_exec(EraseSector, addr)
│   ├─ swd_write_memory(program_buffer, data)  // 数据写入目标 RAM
│   └─ swd_flash_syscall_exec(ProgramPage, addr, size)

flash_manager_finish()
│
├─ swd_flash_syscall_exec(UnInit)
└─ swd_set_target_state(RESET_RUN)     // 复位并运行目标
```

### 5. 虚拟文件系统磁盘 (src/flash/vfs_disk.c)

实现 Zephyr `struct disk_operations` 接口，向 USB MSC 呈现 FAT16 磁盘。

#### FAT16 布局 (32KB 磁盘)

```
Sector 0:     MBR/Boot Sector (BPB)
Sector 1:     FAT1
Sector 2:     FAT2 (FAT1 副本)
Sector 3:     Root Directory (16 entries)
Sector 4+:    Data Area
```

BPB 参数：
- 每扇区 512 字节，每簇 1 扇区
- 2 个 FAT 表，各 1 扇区
- 16 个根目录项
- 64 个扇区总计

#### 文件写入检测

1. 主机写入扇区数据 → `vfs_disk_write_fn()` 记录写入范围
2. 若写入范围覆盖根目录 → `scan_root_directory()` 扫描新文件
3. 检测到 .hex/.bin 文件名 → 标记为 `FILE_STATE_RECEIVING`
4. 主机发送 CTRL_SYNC → 触发烧写流程

```
    Host writes file         CTRL_SYNC          Flash complete
         │                     │                     │
         ▼                     ▼                     ▼
  FILE_STATE_NONE ──► FILE_STATE_RECEIVING ──► FILE_STATE_FLASHING
                                               │            │
                                               │   success  │  error
                                               ▼            ▼
                                    FILE_STATE_SUCCESS  FILE_STATE_ERROR
                                               │            │
                                               ▼            ▼
                                          SUCCESS.TXT    FAIL.TXT
```

#### 状态反馈

烧写完成后更新磁盘镜像：
- 成功：创建 `SUCCESS.TXT`（"Flash programming successful!"）
- 失败：创建 `FAIL.TXT`（"Flash programming FAILED!"）

主机刷新磁盘后即可看到结果文件。

### 6. 目标自动检测 (src/flash/target_detect.c)

#### 检测策略

```
         SWD Connect
             │
             ▼
      Read DP IDCODE
             │
     ┌───────┴───────┐
     ▼               ▼
 Read 0xE0042000  Read 0x40015800     ← STM32 DBGMCU_IDCODE
 (F1/F2/F4/F7)   (F0/F3/L0/L4)
     │               │
     ▼               ▼
 DEV_ID (bits 11:0) DEV_ID (bits 11:0)
     │               │
     └───────┬───────┘
             │
     lookup_stm32(dev_id)
             │
        ┌────┴────┐
        ▼         ▼
    Found!     Not STM32
        │         │
        │         ▼
        │    Check DP IDCODE
        │    ├─ 0x2BA01477 → nRF52
        │    ├─ 0x4BA00477 → LPC1768
        │    └─ other → NULL
        ▼
   Return target_cfg_t
```

#### STM32 DBGMCU_IDCODE

STM32 芯片在两个地址之一提供芯片标识寄存器：

| 地址 | 适用系列 | 内容 |
|------|----------|------|
| 0xE0042000 | F1/F2/F4/F7 | bits[31:16]=REV_ID, bits[11:0]=DEV_ID |
| 0x40015800 | F0/F3/L0/L4/G0/G4/L5/U5 | 同上 |

DEV_ID 唯一标识芯片系列（如 F407=0x413, F103=0x414），不区分同系列不同容量。

#### 目标配置表

每个目标芯片由 `target_cfg_t` 描述：

```c
typedef struct {
    const sector_info_t *sectors_info;    // 扇区地址和大小表
    uint32_t sector_info_length;          // 扇区表长度
    flash_region_t flash_regions[2];      // Flash 区域范围
    ram_region_t ram_regions[2];          // RAM 区域范围
    const char *target_vendor;            // 厂商名
    const char *target_part_number;       // 型号名
} target_cfg_t;
```

扇区表从低地址到高地址排列，`flash_sector_size()` 从高到低搜索以找到地址所属扇区。这支持变长扇区（如 STM32F4 的 16KB/64KB/128KB 混合布局）。

---

## 数据流

### CMSIS-DAP 调试数据流

```
pyOCD/OpenOCD
    │ USB HID OUT Report (64 bytes)
    ▼
Zephyr HID Class
    │ hid_int_out_callback()
    ▼
hid_dap.c: hid_dap_out_cb()
    │ DAP_ExecuteCommand(report_in, report_out)
    ▼
DAP.c: DAP_ProcessCommand()
    │ 解析 Command ID
    ├── DAP_Transfer → SW_DP.c: SWD_Transfer()
    ├── DAP_SWJ_Sequence → SW_DP.c: SWJ_Sequence()
    └── ...
    │
    │ SWD_Transfer() 内部:
    │   PIN_SWCLK_SET() + PIN_SWDIO_SET()  ←→  dap_gpio.c
    │   → 目标 SWD 引脚
    ▼
HID IN Report → USB Host
```

### 拖放烧写数据流

```
主机文件管理器: cp firmware.hex /media/DAPLINK/
    │
    │ USB MSC WRITE (SCSI Write10)
    ▼
Zephyr MSC Class
    │ 扇区写入回调
    ▼
vfs_disk.c: vfs_disk_write_fn()
    │ memcpy() 到 disk_image[]
    │ scan_root_directory() 检测文件名
    ▼
vfs_disk.c: vfs_disk_ioctl_fn(DISK_IOCTL_CTRL_SYNC)
    │ 传输完成
    ▼
target_detect.c: target_detect()
    │ SWD 连接 → 读 IDCODE → 匹配目标
    ▼
flash_manager.c: flash_manager_init(target)
    │ 下载 flash algorithm blob 到目标 RAM
    ▼
intelhex.c: intelhex_parse(data, size, callback)
    │ 解析 HEX 记录 → 回调每段数据
    ▼
flash_manager.c: flash_manager_write(addr, data, size)
    │ for each page:
    │   swd_write_memory(buffer, page_data)  // 数据 → 目标 RAM
    │   swd_flash_syscall_exec(ProgramPage)  // 目标执行编程
    ▼
flash_manager.c: flash_manager_finish()
    │ swd_flash_syscall_exec(UnInit)
    │ swd_set_target_state(RESET_RUN)
    ▼
vfs_disk.c: update_status_file()
    │ 创建 SUCCESS.TXT 或 FAIL.TXT
```

---

## 关键设计决策

### 1. Bit-bang vs 硬件 SPI

选择 GPIO bit-bang 实现 SWD 协议，而非使用 STM32 SPI 外设：

- **优点**：引脚选择灵活，不占用 SPI 外设，与 DAPLink 官方实现一致
- **缺点**：速度受限于 CPU 循环（约 1MHz），CPU 占用高
- **时序保证**：`dap_delay.c` 使用 `k_busy_wait()` 实现亚微秒精确延时

### 2. 内存中磁盘 vs 流式处理

选择将完整 FAT16 磁盘镜像保持在 RAM 中（32KB），而非流式处理：

- **优点**：实现简单，随机读写完全支持，文件系统状态一致
- **缺点**：占用 32KB RAM（占总 128KB 的 25%），限制最大文件大小
- **权衡**：32KB 磁盘可容纳 ~28KB 固件文件，足够大多数 Cortex-M 目标

### 3. 运行时目标检测 vs 编译时选择

官方 DAPLink 在编译时绑定单一目标，本项目实现了运行时检测：

- **优点**：一个固件支持所有目标芯片，用户体验好
- **缺点**：所有目标 blob 常驻 Flash（增加约 5KB），检测增加约 100ms 延迟
- **策略**：STM32 通过 DBGMCU_IDCODE 检测，nRF/LPC 通过 DP IDCODE 检测

### 4. Flash 算法执行模型

使用 DAPLink 的经典 "下载-执行" 模型：

1. 将算法 blob 下载到目标 RAM
2. 将待编程数据写入目标 RAM 缓冲区
3. 设置目标寄存器 (R0-R15, xPSR)
4. 恢复目标执行 → 目标 CPU 运行算法操作 Flash 控制器
5. 算法执行 BKPT → 调试探针重新获得控制权
6. 检查返回值

这比 ICP (In-Circuit Programming) 方式更通用，因为算法直接使用芯片厂商的 Flash 驱动代码。

---

## 调试与排错

### 日志系统

所有模块使用 Zephyr Logging 系统，可通过 `prj.conf` 调整级别：

```
CONFIG_LOG_DEFAULT_LEVEL=2   # INF 级别
CONFIG_LOG_DEFAULT_LEVEL=3   # DBG 级别（查看 SWD 事务细节）
```

模块日志标签：
- `daplink` — 主程序
- `dap_gpio` — GPIO 操作
- `swd_host` — SWD 读写操作
- `flash_mgr` — Flash 编程流程
- `vfs_disk` — 磁盘操作
- `intelhex` — HEX 解析
- `target_detect` — 目标检测

### 常见问题

| 现象 | 原因 | 解决 |
|------|------|------|
| 拖入文件后无反应 | SWD 连接失败 | 检查接线，确认目标供电 |
| FAIL.TXT 出现 | Flash 编程失败 | 查看日志中 EraseSector/ProgramPage 错误 |
| 磁盘无法识别 | FAT 损坏 | 重新插拔 USB |
| OpenOCD 连接失败 | HID 描述符问题 | 确认 in/out report size = 64 |
| 串口无输出 | UART 未连接 | 检查 PA2/PA3 接线 |

---

## 扩展指南

### 添加新目标芯片

1. 从 DAPLink 获取 flash_blob.c（或自行编译算法）
2. 创建 `src/flash/target_xxx.c/.h`
3. 在 `target_detect.c` 的检测表中添加条目
4. 在 `CMakeLists.txt` 中添加源文件
5. 编译验证

### 添加新 USB 接口

1. 在 `usbd_context.c` 中使用对应的 `USBD_*_DEFINE` 宏
2. 实现 Zephyr 类回调
3. 更新 USB 描述符（接口数、端点数）
