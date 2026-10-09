# DAPLink-Zephyr 交接文档

> 更新：2026-10-06 ｜ 作者：AI 结对会话
> 范围：`daplink-zephyr`（基于 Zephyr 的 CMSIS-DAP 调试探针）

本文件记录本轮全部工作、关键设计、验证状态与待办，供接手者快速上手。

---

## 0. 项目一句话

把 ARM 官方 DAPLink 的核心（CMSIS-DAP + 拖放烧写）移植到 **Zephyr RTOS** 上：
一块 STM32 板 → USB 复合设备（HID 调试 + CDC 串口 + MSC 拖放烧写盘），支持 **SWD/JTAG** 调试与运行时目标自动检测。

- 上游 fork：`https://github.com/wentywenty/daplink-zephyr`
- 本地：`D:\env\daplink-zephyr`
- 参考：`D:\env\daplink`（ARMmbed/DAPLink 官方）、`D:\env\blackmagic`（Black Magic Probe）

---

## 1. 开发环境与构建

### 依赖
- Zephyr 源码（west 工作区）：`D:\env\zephyrproject`（`ZEPHYR_BASE=D:\env\zephyrproject\zephyr`）
- Zephyr SDK：`C:\Users\yjb23.MIKU\zephyr-sdk-1.0.1`（ARM 工具链在 SDK 内）
- `west`：`D:\env\zephyrproject\.venv\Scripts\west.exe`
- 主机：Windows + PowerShell

### 构建命令（PowerShell）
```powershell
$west = "D:\env\zephyrproject\.venv\Scripts\west.exe"
$env:ZEPHYR_BASE = "D:\env\zephyrproject\zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\Users\yjb23.MIKU\zephyr-sdk-1.0.1"

# 全功能板（HID+CDC+MSC+JTAG）
& $west build -p always -b black_f407ve "D:\env\daplink-zephyr" -d "D:\env\daplink-zephyr\build_f407"
```
支持的板：`black_f407ve`（全功能）、`stm32_min_dev`（Blue Pill，无 MSC/JTAG）、`nucleo_f103rb`（最小配置）。

### ⚠️ 关键坑
- **改板级 `.conf`/`.overlay` 后必须 `-p always`**（或删 build 目录）。`-p auto` 不会重跑 CMake 配置，新配置**不生效**（本项目踩过一次：`nucleo_f103rb` 加 conf 后 `-p auto` 仍溢出）。
- 两个固件入口：`src/main.c`（interface）—— 本项目只做 interface（无独立 bootloader）。

---

## 2. 本轮完成的工作

### 2.1 修复 `nucleo_f103rb` 编译失败
- **现象**：`region RAM overflowed by 34632 bytes`。
- **根因**：该板只有 `.overlay`、**没有板级 `.conf`**，继承了 `prj.conf` 里 32KB 的 MSC 磁盘 → 20KB RAM 溢出。
- **修复**：新增 `boards/nucleo_f103rb.conf`（关 MSC/日志/console，缩小栈），同 `stm32_min_dev.conf` 思路。

### 2.2 新增 JTAG 传输支持（本轮主特性）
背景：原来 `DAP_config.h` 里 `DAP_JTAG 0`，JTAG 未实现；但 `DAP.c` 的 JTAG 命令处理**已随官方移植就绪**，缺的是底层 `JTAG_DP.c`。

| 文件 | 改动 |
|---|---|
| `src/dap/JTAG_DP.c` | **新增**：ARM 官方 JTAG 位时序（`JTAG_Sequence/JTAG_IR/JTAG_ReadIDCode/JTAG_WriteAbort/JTAG_Transfer`），Apache-2.0，与本项目 `DAP.c/DAP.h` 完全兼容 |
| `src/dap/DAP_config.h` | `DAP_JTAG = IS_ENABLED(CONFIG_DAPLINK_JTAG)`；`DAP_JTAG_DEV_CNT=4`；实现 `PIN_TDI_OUT/PIN_TDO_IN`、`PORT_JTAG_SETUP`；`PORT_OFF` 释放 TDI/TDO |
| `src/hal/dap_gpio.{h,c}` | 新增 `tdi_spec`/`tdo_spec`/`jtag_present` 及初始化（TDI 输出拉高、TDO 输入） |
| `dts/bindings/gpio/daplink,gpio.yaml` | 新增 `tdi-gpios`/`tdo-gpios`（可选） |
| `boards/black_f407ve.overlay`、`boards/stm32_daplink.overlay` | 加 JTAG 引脚（TCK/TMS 复用 SWCLK/SWDIO，TDI/TDO 新增） |
| `CMakeLists.txt` | `JTAG_DP.c` 按 `CONFIG_DAPLINK_JTAG` 条件编译 |
| `Kconfig` | 新增 `DAPLINK_JTAG`（默认 y） |
| `boards/stm32_min_dev.conf`、`boards/nucleo_f103rb.conf` | `CONFIG_DAPLINK_JTAG=n`（Flash 受限） |

设计要点：
- **TCK/TMS 复用**现有 SWCLK/SWDIO 引脚，**只多占 TDI/TDO 两个脚**。
- 整个 JTAG 由 **Kconfig 门控**（64KB Flash 的 Blue Pill 关掉，省 ~9KB；硬开会到 96.86% 太险）。
- **健壮性**：板上没配 TDI/TDO 时（`jtag_present=false`），`PIN_TDI_OUT/PIN_TDO_IN` 安全空操作，主机误选 JTAG 不会 NULL 崩。

### 2.3 目标芯片扩展（Flash 算法 + 检测）
- **新增可复用工具 `tools/port_family.py`**：把 DAPLink `source/family/<厂>/<芯片>/`（`flash_blob.c`+`target.c`）**一键转**成本项目 `target_xxx.c/.h`（同一 `program_target_t` 结构，blob 原样拷贝、`flash`/`sectors_info` 重命名、包一层 `target_cfg_t`）。
- **新增目标**（`src/flash/`）：
  - `target_stm32f2.{c,h}`（STM32F207ZG）
  - `target_stm32f429.{c,h}`（STM32F429/439，2MB）
  - `target_stm32l1.{c,h}`（STM32L151CC）
- **`src/flash/target_detect.c` 检测表从 9 条扩到 ~25 条 DEV_ID**：F0/F1/F2/F3/F4/F7/L0/L4 各代常见型号（如 F1 的 0x410/0x412/0x418、F4 的 0x421(F446)、F7 的 0x450/0x451、L4 的 0x462/0x470 等）；`0x419` 由旧的 F407 改映射到新的 **F429(2MB)**。

### 2.4 文档更新
- `README.md`：功能表加 SWD+JTAG、结构树加 `JTAG_DP.c`、开发阶段加 Phase 5、JTAG 接线图、支持芯片表补 F2/F429/L1 与检测说明。
- `docs/architecture.md`：命令列表补 JTAG 命令；新增 `JTAG_DP.c` 小节。

---

## 3. 验证状态

### 已编译验证（本地 `west build` 全部通过）
| 板 | FLASH | RAM | MSC | JTAG |
|---|---|---|---|---|
| `black_f407ve` | 103164 B / 512 KB (19.68%) | 59964 B / 128 KB (45.75%) | ✅ | ✅ |
| `stm32_min_dev` | 56820 B / 64 KB (86.70%) | 15280 B / 20 KB (74.61%) | ❌ | ❌ |
| `nucleo_f103rb` | 51584 B / 128 KB (39.36%) | 12080 B / 20 KB (58.98%) | ❌ | ❌ |

### 仅「编译级」验证，**功能未上板**
- **JTAG**：DT 已生效（`zephyr.dts` 有 `tdi-gpios/tdo-gpios`）、能力位已置，但**真实 TAP 扫描/时序未在硬件上测**。
- **新目标芯片**：只验证了编译；**未对真实目标烧写验证**（无目标硬件）。
- **拖放烧写/自动检测**：本轮未改动其逻辑，沿用既有状态。

---

## 4. 待办清单（按优先级）

### P0（必须 / 尽快）
- [ ] **上板验证 JTAG**：
  ```
  openocd -f interface/cmsis-dap.cfg -c "transport select jtag" -f target/stm32f4x.cfg -c "scan_chain" -c "exit"
  ```
  确认能识别 TAP 链 / 读 IDCODE。若失败，重点查 `dap_delay` 时序与 TDI/TDO 引脚。
- [ ] **提交并推送**：本轮改动**全部未提交**（见第 5 节文件清单）。建议分 3 个 commit：`fix(nucleo_f103rb)`、`feat(jtag)`、`feat(targets)`。
- [ ] **`.gitignore` 加 `build_*/`**（当前有 6 个 build 目录是未跟踪的构建产物）。

### P1（下一个功能）
- [ ] **非 STM32 目标 + 厂商识别**：
  - 用 `tools/port_family.py` 搬 blob（nRF52840、NXP LPC8xx/55xx、Nuvoton M4x、Maxim、Kinetis…）。
  - **关键短板**：现有检测器只认 STM32-DEV_ID + nRF52/LPC1768 的 DP-IDCODE；需为各厂补「读厂商 ID 寄存器」的识别逻辑（参考 DAPLink `source/daplink/interface/target_flash.c` / 各 family 的 `target.c`）。
- [ ] **SWO 单线 Trace**：移植 CMSIS-DAP `SWO.c`（`DAP.c` 里 `DAP_SWO_*` 命令已就绪，`#if SWO_UART` 关着），用一路 Zephyr UART 捕获目标 SWO；`DAP_config.h` 打开 `SWO_UART/SWO_BUFFER_SIZE`。

### P2（增强）
- [ ] **JTAG 引脚规范化**：可选按 STM32 惯例 `PA13(SWDIO/TMS)/PA14(SWCLK/TCK)/PA15(TDI)/PB3(TDO)`，并同步到 `nrf52_daplink.overlay` 等（注意别和现有 SWD 引脚冲突）。
- [ ] **批量导入 DAPLink 全部目标**（`tools/port_family.py` 已就绪；注意 Flash 占用，需按需门控，如加 `CONFIG_DAPLINK_TARGETS` 子选）。
- [ ] **`DAP_JTAG_DEV_CNT` 配置化**（现固定 4）。
- [ ] 板级：给 `nrf52_daplink.overlay` 补 JTAG 脚（若 nRF 板做探针）。

### P3（工程化）
- [ ] README/architecture 再校一遍 JTAG 与目标表。
- [ ] 加最小 CI（`west build` 三板冒烟）。
- [ ] 清理 `docs/superpowers/plans/*.md` 里 Phase1 的未勾选项（与现状不符）。

---

## 5. 改动文件清单

**修改**
```
CMakeLists.txt
Kconfig
README.md
docs/architecture.md
boards/black_f407ve.overlay
boards/stm32_daplink.overlay
boards/stm32_min_dev.conf
dts/bindings/gpio/daplink,gpio.yaml
src/dap/DAP_config.h
src/flash/target_detect.c
src/hal/dap_gpio.c
src/hal/dap_gpio.h
```
**新增**
```
boards/nucleo_f103rb.conf
src/dap/JTAG_DP.c
src/flash/target_stm32f2.c   src/flash/target_stm32f2.h
src/flash/target_stm32f429.c src/flash/target_stm32f429.h
src/flash/target_stm32l1.c   src/flash/target_stm32l1.h
tools/port_family.py
```
**未跟踪（构建产物，建议 gitignore）**：`build_*/`

---

## 6. 关键机制备忘（读代码入口）

| 主题 | 入口 |
|---|---|
| CMSIS-DAP 命令分发 | `src/dap/DAP.c`（`DAP_ProcessCommand`） |
| SWD 物理层 | `src/dap/SW_DP.c`（`SWD_Transfer`） |
| JTAG 物理层 | `src/dap/JTAG_DP.c`（`JTAG_Transfer/JTAG_IR`） |
| GPIO/Delay 适配 | `src/dap/DAP_config.h`、`src/hal/dap_gpio.c`、`src/hal/dap_delay.c` |
| 拖放烧写链路 | `src/flash/{vfs_disk,flash_manager,swd_host,intelhex}.c` |
| 目标检测 | `src/flash/target_detect.c`（STM32 DEV_ID 0xE0042000/0x40015800；非 STM32 走 DP IDCODE） |
| 目标 Flash 算法 | `src/flash/target_*.c`（`program_target_t` + blob） |
| 移植新目标 | `tools/port_family.py <DAPLink芯片目录> <short>` |

DAPLink 参考实现位置：`D:\env\daplink\source\family\<厂>\<芯片>\`（blob）、`source\daplink\`（App）。
Black Magic 参考（写 JTAG/位时序时参考）：`D:\env\blackmagic\src\platforms\common\{swdptap,jtagtap,adiv5}.c`。

---

## 7. 已知限制（诚实清单）

- **JTAG 未上板验证**；`black_f407ve` 的 JTAG 引脚（TDI=PB15/TDO=PB12）是**选定的**，需确认在目标板上空闲。
- **新目标芯片未对真实硬件烧写验证**；DEV_ID 映射中少数条目（如 `0x417/0x416`）依赖对 ST 参考的经验值，使用前建议核对。
- **非 STM32 厂商**尚无识别逻辑，DAPLink 的对应 blob 未导入。
- **SWO 未实现**。
- **无 bootloader 固件**（仅 interface；升级需外部工具烧写）。
- 本轮改动**未提交**。
