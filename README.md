# DAPLink Zephyr

基于 Zephyr RTOS 的 CMSIS-DAP 调试探针，支持拖放烧写。

将一块 STM32F407 开发板变为全功能调试器：通过 USB 同时提供 HID 调试接口、串口透传、拖放烧写磁盘。

## 功能

| 功能 | 接口 | 说明 |
|------|------|------|
| CMSIS-DAP v2 | USB HID | 兼容 Keil/OpenOCD/pyOCD |
| 串口透传 | CDC ACM | 调试目标 UART 输出 |
| 拖放烧写 | MSC | 拖入 .hex/.bin 自动烧写目标 |
| 自动检测 | — | SWD IDCODE 识别目标芯片 |

## 支持的目标芯片

### STM32 系列

| 系列 | 代表型号 | Flash | 扇区 | DEV_ID |
|------|----------|-------|------|--------|
| STM32F0 | STM32F072RB | 128 KB | 1 KB | 0x445 |
| STM32F1 | STM32F103xB | 128 KB | 1 KB | 0x414 |
| STM32F3 | STM32F334R8 | 64 KB | 2 KB | 0x438 |
| STM32F4 | STM32F407VE | 512 KB | 变长 | 0x413 |
| STM32F7 | STM32F746ZG | 1 MB | 32 KB | 0x449 |
| STM32L0 | STM32L082CZ | 192 KB | 128 B | 0x407 |
| STM32L4 | STM32L476RG | 1 MB | 2 KB | 0x439 |

STM32F401/411/429 共用 F4 算法，DEV_ID 分别为 0x423/0x431/0x419。

### 其他厂商

| 厂商 | 型号 | Flash | 扇区 | 检测方式 |
|------|------|-------|------|----------|
| Nordic | nRF52832 | 512 KB | 4 KB | DP IDCODE |
| NXP | LPC1768 | 512 KB | 4 KB | DP IDCODE |

## 硬件要求

### 调试探针板（运行本固件）

- STM32F407 或兼容芯片
- USB Full-Speed 接口
- 至少 128 KB Flash、64 KB RAM

### 引脚连接

调试探针通过 SWD 连接目标板：

```
探针 (STM32F407)          目标板
─────────────────        ──────────
PB13 (SWCLK)  ─────────> SWCLK
PB14 (SWDIO)  ─<───────> SWDIO
PB0  (nRESET) ─────────> nRST
GND           ─────────> GND
```

串口透传（可选）：

```
PA2 (USART2 TX) ────────> 目标 RX
PA3 (USART2 RX) ─<─────── 目标 TX
```

## 构建与烧写

### 环境准备

```bash
# 安装 Zephyr SDK (需包含 ARM 工具链)
west init -m https://github.com/zephyrproject-rtos/zephyr
cd zephyr
west update

# 激活环境
source zephyr-env.sh
```

### 编译

```bash
# 克隆本项目
git clone <repo-url> daplink-zephyr
cd daplink-zephyr

# 编译 (black_f407ve 开发板)
west build -b black_f407ve --pristine

# 或其他支持的开发板
west build -b nucleo_f103rb --pristine
```

### 烧写探针固件

```bash
# 使用 ST-Link
west flash

# 或使用 openocd
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program build/zephyr/zephyr.elf verify reset exit"
```

## 使用方法

### 1. CMSIS-DAP 调试

将探针 USB 连接主机，调试工具会自动识别 CMSIS-DAP 设备：

```bash
# OpenOCD
openocd -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg

# pyOCD
pyocd gdbserver -t stm32f407ve
```

### 2. 串口透传

主机识别到 CDC ACM 串口设备，直接连接即可：

```bash
minicom -D /dev/ttyACM0 -b 115200
```

### 3. 拖放烧写

1. 将探针 USB 连接主机
2. 主机出现 `DAPLINK` 可移动磁盘
3. 将 `.hex` 或 `.bin` 文件拖入磁盘
4. 等待烧写完成（磁盘闪烁指示中）
5. 烧写成功后出现 `SUCCESS.TXT`，失败出现 `FAIL.TXT`

```
$ cp firmware.hex /media/DAPLINK/
$ cat /media/DAPLINK/SUCCESS.TXT
Flash programming successful!
```

## 项目结构

```
src/
├── main.c              # 入口：初始化 GPIO/DAP/VFS/USB
├── dap/                # CMSIS-DAP 核心
│   ├── DAP.c           # DAP 命令处理（从 ARM 官方移植）
│   ├── DAP.h
│   ├── DAP_config.h    # Zephyr GPIO 适配层
│   └── SW_DP.c         # SWD 协议底层 bit-bang
├── hal/                # 硬件抽象层
│   ├── dap_gpio.c      # SWD GPIO 初始化和控制
│   └── dap_delay.c     # 精确延时（SWD 时序）
├── usb/                # USB 设备栈
│   ├── usbd_context.c  # 复合设备定义 (HID+CDC+MSC)
│   └── hid_dap.c       # HID-DAP 适配器
├── flash/              # 拖放烧写子系统
│   ├── flash_blob.h    # Flash 算法数据结构
│   ├── swd_host.c      # SWD 高层接口
│   ├── flash_manager.c # Flash 编程管理器
│   ├── intelhex.c      # Intel HEX 解析器
│   ├── vfs_disk.c      # 虚拟 FAT16 磁盘驱动
│   ├── target_detect.c # 目标芯片自动检测
│   ├── target_stm32*.c # STM32 各系列目标配置
│   ├── target_nrf52.c  # Nordic nRF52 目标配置
│   └── target_lpc1768.c # NXP LPC1768 目标配置
boards/
├── black_f407ve.overlay   # Black F407VE 开发板
├── nucleo_f103rb.overlay  # NUCLEO-F103RB
├── stm32_daplink.overlay  # 通用 STM32 DAPLink
└── nrf52_daplink.overlay  # nRF52 DAPLink
```

## 资源占用

以 STM32F407VE (black_f407ve) 为目标编译：

| 资源 | 使用量 | 总量 | 占比 |
|------|--------|------|------|
| Flash | 92.5 KB | 512 KB | 17.7% |
| RAM | 61 KB | 128 KB | 46.6% |

RAM 主要被 VFS 磁盘镜像占用（32 KB）。

## 开发阶段

| Phase | 内容 | 状态 |
|-------|------|------|
| 1 | USB HID + CMSIS-DAP 核心 | Done |
| 2 | CDC ACM 串口透传 | Done |
| 3 | MSC USB 大容量存储 | Done |
| 4 | 拖放烧写 + 多目标自动检测 | Done |

## 参考项目

- [ARMmbed/DAPLink](https://github.com/ARMmbed/DAPLink) — 官方 DAPLink 实现（裸机）
- [zephyrproject-rtos/zephyr](https://github.com/zephyrproject-rtos/zephyr) — Zephyr RTOS
- [ARM-software/CMSIS_5](https://github.com/ARM-software/CMSIS_5) — CMSIS-DAP 参考实现

## License

Apache-2.0
