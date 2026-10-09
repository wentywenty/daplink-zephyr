# blackmagic → daplink-zephyr 能力移植文档

本文档记录从 [Black Magic Debug](https://black-magic.org) 移植进本 Zephyr CMSIS-DAP 探针
的能力、新增的 Kconfig / 设备树 / vendor 命令接口，以及构建与使用方式。

> 说明：本文档对应一批新增/修改的源码，均已通过 `black_f407ve` 与 `stm32_min_dev`
> 构建验证；除特别注明外**尚未在硬件上实测**。

---

## 1. 总览

在保持 daplink-zephyr「CMSIS-DAP 调试探针 + 拖放烧写」定位不变的前提下（**不引入 GDB server 引擎**），
移植了以下能力：

| # | 能力 | 状态 |
|---|------|------|
| 1 | 严重 bug 修复（拖放烧写/复位/并发） | 完成 |
| 2 | STM32/nRF 内部 flash **直写寄存器**后端（F0/F1/F2/F3/F4/F429/L4/nRF52） | 完成 |
| 3 | 目标识别：CPUID + DBGMCU 排序 + DEV_ID 扩充 | 完成 |
| 4 | SWD multi-drop / dormant→SWD 回退 | 完成 |
| 5 | 烧写校验（回读比对） | 完成 |
| 6 | attach 健壮性（flash 前冻结看门狗/低功耗） | 完成 |
| 7 | **RISC-V** 支持（JTAG/ADI/gateway DTM + DM + 抽象命令 + SBA + GD32VF103 目标） | 完成（未实测） |
| 8 | **SWO** 捕获（UART）+ **ITM 软件解码** | 完成（Manchester 未做） |
| 9 | **RTT**（CMSIS-DAP vendor 命令） | 完成（需主机端工具） |
| 10 | **Option bytes**（STM32F4 读写） | 完成 |
| 11 | **OTA/DFU**（MCUboot sysbuild + REBOOT 命令） | 完成（未实测） |
| 12 | **SPI flash**（探针侧 SPI + JEDEC/SFDP） | 完成（未实测） |

**未做**：GDB/remote 引擎、semihosting、FLM 运行时加载、SWO Manchester 硬件解码。

---

## 2. 内存访问抽象与 flash 后端

### 2.1 `flash_mem` 抽象（`src/flash/flash_mem.c/.h`）
统一 16/32-bit 与块读的 32 位目标内存访问，后端可切换：

- **SWD MEM-AP**（ARM Cortex-M，默认）
- **RISC-V System Bus Access**（`flash_mem_set_backend_sba(true, hart)`）

native flash 驱动全部改走 `flash_mem_*`，因此**同一份驱动可在 SWD 或 RISC-V SBA 上运行**。

### 2.2 native flash 后端（`flash_native_ops_t`，见 `flash_blob.h`）
```c
typedef struct flash_native_ops {
    int (*prepare)(const target_cfg_t *cfg);            /* 冻结看门狗等 */
    int (*erase_sector)(const target_cfg_t *cfg, uint32_t addr);
    int (*program)(const target_cfg_t *cfg, uint32_t addr, const uint8_t *data, uint32_t size);
    int (*mass_erase)(const target_cfg_t *cfg);
    int (*option_read)(const target_cfg_t *cfg, uint32_t idx, uint32_t *val);
    int (*option_write)(const target_cfg_t *cfg, uint32_t idx, uint32_t val);
} flash_native_ops_t;
```
`target_cfg_t.native_ops` 非空时，`flash_manager` 走**直写寄存器**（不下载 RAM 算法、不用断点握手）。

| 目标 | 后端 | 说明 |
|------|------|------|
| STM32 F0/F3 | `flash_native_stm32f1` | 与 F1 同寄存器模型（FLASH@0x40022000，半字编程） |
| STM32 F1 | `flash_native_stm32f1` | |
| STM32 F2 | `flash_native_stm32f4` | 与 F4 同控制器（FLASH@0x40023C00，x32） |
| STM32 F4 / F429 | `flash_native_stm32f4` | 标准 F4 扇区布局（含双 bank 镜像，SNB 12 起） |
| STM32 L4 | `flash_native_stm32l4.c` | PNB/BKER，读 OPTR.DBANK 判单/双 bank，64-bit 双字编程 |
| nRF52832 | `flash_native_nrf52.c` | NVMC@0x4001E000，页擦除 + 字编程 + ERASEALL |
| GD32VF103（RISC-V） | `flash_native_stm32f1` | 复用 F1 逻辑，内存访问改走 **SBA** |

其余目标（F7、L0/L1、LPC1768）仍走 DAPLink RAM blob（LR 修复后该路径也可用）。

### 2.3 关键修复
- **`swd_flash_syscall_exec` 补写 R14(LR)=breakpoint**：RAM 算法 `bx lr` 才能回到 blob 内嵌 BKPT
  （blob 首字 `0xE00ABE00` 已含 BKPT），此前算法必然超时。
- **扇区擦除**：除零保护 + 不跨扇区编程。
- **烧写校验**：`flash_manager_verify()` 回读比对（已接入 `.hex`/`.bin` 拖放流程）。
- **attach**：flash 前写 `DBGMCU_CR` 冻结 IWDG/WWDG/Sleep/Stop/Standby。
- **并发**：`swd_bus_mutex`（`hid_dap.c`）统一保护 HID 与拖放烧写两条 SWD 路径。
- **vfs_disk**：状态文件目录项偏移修正、`.bin` 基址按目标 flash region。
- **nRESET**：`PIN_nRESET_OUT` 改 raw 电平，消除 ACTIVE_LOW 反转。
- **HID 响应长度钳位**。

---

## 3. 目标识别（模块化 probe 框架）

采用 blackmagic 式的 **probe 链 + 弱符号** 机制：

- **`target_probe.h`**：定义 `target_probe_fn`（`const target_cfg_t *(*)(uint32_t dp_idcode)`）并声明各 probe 为
  `__attribute__((weak))`。
- **`target_detect.c`**：`target_detect()` 只需 `swd_init_debug()` + 读 DP IDCODE，然后遍历 registry：
  ```c
  static const target_probe_fn probes[] = {
      stm32_probe, nrf_probe, lpc_probe, riscv_probe,
  };
  ```
  未编译进固件的 probe 是**弱未定义符号 → 地址为 NULL → 自动跳过**，无需 `#if` 守卫。
- **各厂商 probe**：
  - `target_stm32_detect.c` → `stm32_probe()`：CPUID 判核 → 按核排序 DBGMCU 地址 → DEV_ID 查表。
  - `target_simple.c` → `nrf_probe()` / `lpc_probe()`：按 DP IDCODE。
  - `target_riscv.c` → `riscv_probe()`：JTAG → ADI → gateway DTM 三级回退。

**新增一颗芯片**：实现 `x_probe()`，在 `target_probe.h` 声明（weak），加入 `probes[]`，把源码挂到对应
Kconfig 组即可。

Kconfig 分组（默认 y，可关闭以省 Flash）：`DAPLINK_TARGET_STM32`、`DAPLINK_TARGET_MISC`；
RISC-V 归 `DAPLINK_RISCV`。

---

## 4. RISC-V 支持

分层（对应 blackmagic 的 `riscv_*.c`）：

| 层 | 文件 | 内容 |
|----|------|------|
| Transport | `riscv_jtag_dtm.c/.h` | JTAG TAP 导航、DTMCS、DMI（41-bit），分派后端 |
| Transport | `riscv_adi_dtm.c/.h` | ADI（MEM-AP）DTM：DMI 映射到 AP 地址空间（`addr<<2`） |
| Transport | `riscv_gateway_dtm.c/.h` | gateway DTM：自定义 AP（IDR `0x74726976`），`DMI_ADDR`/`DMI_DATA` |
| DM | `riscv_dm.c/.h` | DM 激活、hart 枚举、halt/resume/reset |
| 抽象命令 | `riscv_dm.c` | `riscv_reg_read/write`（`cmdtype=0`，GPR=0x1000+n / CSR） |
| 内存 | `riscv_dm.c` | SBA 16/32-bit `riscv_mem_read/write` |
| 集成 | `target_riscv.c/.h` | `riscv_detect()`：JTAG→ADI→gateway 回退；建立 DM/hart/SBA 后经 **vendor probe registry**（`riscv_probe.h`）分派 |
| 目标 | `target_riscv32.c` | `gd32vf1_probe()`：经 SBA 读 DBGMCU IDCODE 识别 GD32VF103 |

RISC-V 采用与 ARM 相同的 **probe 链**：`riscv_probe()` 起 transport+DM+SBA 后，遍历
`riscv_vendor_probes[] = { gd32vf1_probe, ... }`（weak，未编译则跳过）。新增 RISC-V 芯片 =
新建 `x_probe()` + weak 声明 + 入 registry。

`riscv_detect()` 流程：DTM 初始化 → `dm_init` 枚举 hart → halt hart0 → 读 `misa`/`DMSTATUS`
→ 切 SBA → 读 DBGMCU IDCODE(`0xE0042000`) → `dev_id==0x410` 返回 GD32VF103。

---

## 5. SWO / RTT

### 5.1 SWO（`src/hal/dap_swo.c`）
CMSIS-DAP 标准 `ID_DAP_SWO_*` 命令，后端用 Zephyr UART 捕获 SWO（ISR + 环形缓冲）。
需要设备树节点：
```dts
/ {
    swo0: swo0 {
        compatible = "daplink,swo";
        uart = <&usart6>;   /* RX 接目标 SWO */
    };
};
```
`CONFIG_DAPLINK_SWO=y`（默认关）。

### 5.2 ITM 解码（`src/hal/swo_itm.c/.h`）
把捕获的 SWO 字节流按 ITM/SWIT 协议解码到通道数据，经 vendor 命令读取。
`CONFIG_DAPLINK_SWO_ITM=y`（依赖 `DAPLINK_SWO`）。
**Manchester 编码捕获未实现**（需定时器输入捕获硬件）。

### 5.3 RTT（`src/flash/rtt.c/.h`）
按 SEGGER RTT 规范扫描目标 RAM 的 `"SEGGER RTT"` 控制块，读写 up/down 环形缓冲。
`CONFIG_DAPLINK_RTT=y`（依赖 `DAPLINK_MSC_FLASH`），经 vendor 命令暴露。

---

## 6. OTA / DFU（MCUboot）

- `sysbuild.conf`：`SB_CONFIG_BOOTLOADER_MCUBOOT=y`、`SB_CONFIG_MCUBOOT_MODE_SWAP_SCRATCH=y`
- `sysbuild.cmake`：给 app 与 mcuboot 各自注入分区 overlay
- `pm_static.yml` / `boards/bmd_partitions.dtsi`：512KB 布局（boot 128K + slot0 128K + slot1 128K + scratch 128K，F4 扇区对齐）
- vendor 命令 `0x20`：REBOOT（`CONFIG_DAPLINK_VENDOR_REBOOT=y`，`select REBOOT`）

构建（其余为普通单镜像构建，不受影响）：
```bash
west build --sysbuild -b black_f407ve daplink-zephyr
# 产物：build/mcuboot/zephyr/zephyr.hex（bootloader）
#       build/daplink-zephyr/zephyr/zephyr.signed.hex/.bin（签名应用）
west flash --sysbuild
```

---

## 7. Option bytes / SPI flash

### 7.1 Option bytes（STM32F4，`CONFIG_DAPLINK_OPTION_BYTES=y`）
`flash_native_stm32.c` 的 `option_read/write`（OPTKEYR 解锁 → 写 OPTCR → OPTSTRT → OBL_LAUNCH），
经 vendor 命令访问。

### 7.2 SPI flash（`src/flash/spi_flash.c/.h`，`CONFIG_DAPLINK_SPI_FLASH=y`）
探针侧 SPI NOR：JEDEC ID（0x9F）+ **SFDP**（0x5A）解析容量/扇区/页，提供读/擦/写/整片擦。
设备树（挂在 SPI 控制器下）：
```dts
&spi1 {
    status = "okay";
    cs-gpios = <&gpiob 6 GPIO_ACTIVE_LOW>;
    spiflash: spiflash@0 {
        compatible = "daplink,spi-flash";
        reg = <0>;
        spi-max-frequency = <8000000>;
    };
};
```

---

## 8. 设备树 binding
| compatible | 文件 | 用途 |
|---|---|---|
| `daplink,gpio` | `dts/bindings/gpio/daplink,gpio.yaml` | SWD/JTAG 引脚（原有） |
| `daplink,swo` | `dts/bindings/daplink,swo.yaml` | SWO 捕获 UART |
| `daplink,spi-flash` | `dts/bindings/daplink,spi-flash.yaml` | 探针侧 SPI NOR |

---

## 9. CMSIS-DAP vendor 命令协议（命令 `0x80`）

响应 `[0]` = 状态：`0`=OK，`1`=错误，`0xFF`=未知子命令。

| 子命令 | 方向 | 请求 | 响应 |
|---|---|---|---|
| `0x00` | RTT INFO | `80 00` | `st, found, u32 up, u32 down, u32 cb` |
| `0x01` | RTT SCAN | `80 01` | `st` |
| `0x02` | RTT READ | `80 02 chan max` | `st, u8 count, data…` |
| `0x03` | RTT WRITE | `80 03 chan len data…` | `st, u8 written` |
| `0x10` | OPT READ | `80 10` | `st, u32 opt0, u32 opt1` |
| `0x11` | OPT WRITE | `80 11 idx u32val` | `st` |
| `0x20` | REBOOT | `80 20` | `st` |
| `0x30` | SPIFLASH INFO | `80 30` | `st, present, u32 jedec, u32 cap, u32 sector, u32 page` |
| `0x31` | SPIFLASH READ | `80 31 u24 addr u8 len` | `st, u8 count, data…` |
| `0x32` | SPIFLASH ERASE | `80 32 u24 addr` | `st` |
| `0x33` | SPIFLASH PROGRAM | `80 33 u24 addr u8 len data…` | `st, u8 written` |
| `0x34` | SPIFLASH CHIP ERASE | `80 34` | `st` |
| `0x40` | ITM SET MASK | `80 40 u32 mask` | `st` |
| `0x41` | ITM READ | `80 41` | `st, u8 count, data…` |
| `0x42` | ITM RESET | `80 42` | `st` |

> RTT / Option / SPI / ITM 均需主机端工具配合（无标准 CMSIS-DAP 对应命令）。

---

## 10. Kconfig 开关一览

| 选项 | 默认 | 依赖 | 说明 |
|---|---|---|---|
| `DAPLINK_MSC_FLASH` | 见 DISK_ACCESS | — | 拖放烧写（原有） |
| `DAPLINK_JTAG` | y | — | JTAG 传输（原有） |
| `DAPLINK_TARGET_STM32` | y | `DAPLINK_MSC_FLASH` | STM32 目标检测 + 直写 flash 驱动 |
| `DAPLINK_TARGET_MISC` | y | `DAPLINK_MSC_FLASH` | nRF / LPC 目标 |
| `DAPLINK_SWO` | n | `SERIAL` | CMSIS-DAP SWO 捕获 |
| `DAPLINK_SWO_ITM` | n | `DAPLINK_SWO` | ITM 软件解码 |
| `DAPLINK_RTT` | n | `DAPLINK_MSC_FLASH` | RTT over vendor |
| `DAPLINK_OPTION_BYTES` | n | `DAPLINK_MSC_FLASH` | STM32F4 option bytes |
| `DAPLINK_VENDOR_REBOOT` | n | — | 探针重启 vendor 命令 |
| `DAPLINK_RISCV` | n | `DAPLINK_JTAG && DAPLINK_MSC_FLASH` | RISC-V 支持 |
| `DAPLINK_SPI_FLASH` | n | `SPI` | 探针侧 SPI NOR |

启用方式：板级 `.conf` 或构建时 `-DCONFIG_xxx=y`。

---

## 11. 构建

```bash
# 默认（普通单镜像）
west build -b black_f407ve daplink-zephyr
west build -b stm32_min_dev daplink-zephyr

# 打开可选能力（示例：RTT + option bytes）
west build -b black_f407ve daplink-zephyr -- -DCONFIG_DAPLINK_RTT=y -DCONFIG_DAPLINK_OPTION_BYTES=y

# OTA / DFU（MCUboot）
west build --sysbuild -b black_f407ve daplink-zephyr
```

### 11.1 单元测试（QEMU）

两个纯逻辑模块提供了可在 QEMU 上运行的单元测试（`qemu_cortex_m3`，会打印 `RESULT: PASS/FAIL`）：

```bash
# SWO ITM 解码器
west build -b qemu_cortex_m3 daplink-zephyr/tests/swo_itm
west build -b qemu_cortex_m3 daplink-zephyr/tests/swo_itm -t run   # 或直接用 qemu-system-arm 跑 zephyr.elf

# SFDP 解析器
west build -b qemu_cortex_m3 daplink-zephyr/tests/sfdp
west build -b qemu_cortex_m3 daplink-zephyr/tests/sfdp -t run
```

> 说明：完整探针固件无法在 QEMU 中运行——本 Zephyr 树没有 STM32F4 的 QEMU 板级，
> 且 QEMU 无法模拟探针的 USB/GPIO 硬件。因此 QEMU 只覆盖平台无关的逻辑模块。

---

## 12. 新增文件清单

```
src/flash/flash_mem.c/.h
src/flash/target_probe.h
src/flash/target_stm32_detect.c
src/flash/target_simple.c
src/flash/flash_native_stm32.c
src/flash/flash_native_stm32l4.c
src/flash/flash_native_nrf52.c
src/flash/riscv_jtag_dtm.c/.h
src/flash/riscv_adi_dtm.c/.h
src/flash/riscv_gateway_dtm.c/.h
src/flash/riscv_dm.c/.h
src/flash/riscv_probe.h
src/flash/target_riscv.c/.h
src/flash/target_riscv32.c
src/flash/rtt.c/.h
src/flash/spi_flash.c/.h
src/flash/sfdp.c/.h
src/hal/dap_swo.c
src/hal/swo_itm.c/.h
src/dap/dap_vendor.c
tests/swo_itm/{CMakeLists.txt,prj.conf,src/main.c}
tests/sfdp/{CMakeLists.txt,prj.conf,src/main.c}
dts/bindings/daplink,swo.yaml
dts/bindings/daplink,spi-flash.yaml
sysbuild.conf
sysbuild.cmake
pm_static.yml
boards/bmd_partitions.dtsi
boards/bmd_mcuboot_partitions.overlay
boards/bmd_app_sysbuild.overlay
```

修改的关键文件：`CMakeLists.txt`、`Kconfig`、`.gitignore`、
`src/flash/{flash_blob.h,flash_manager.c/.h,swd_host.c/.h,vfs_disk.c,target_detect.c,target_stm32*.c,target_nrf52.c}`、
`src/dap/DAP_config.h`、`src/usb/hid_dap.c`。

---

## 13. 扩展指南

### 13.1 新增一颗 ARM 目标
1. 写 `<chip>_probe.c`：`const target_cfg_t *<chip>_probe(uint32_t dp_idcode)`——读芯片 ID/DBGMCU
   寄存器，匹配则返回对应 `target_cfg_t`，否则返回 NULL。
2. `target_probe.h` 加 weak 声明；`target_detect.c` 的 `probes[]` 加一项。
3. 目标数据用 `target_cfg_t`（`sectors_info` + flash/ram `regions` + 可选 `native_ops`）。
4. 把源码挂到对应 Kconfig 组的 CMake 列表（如 `DAPLINK_TARGET_STM32`）。

### 13.2 新增一颗 RISC-V 目标
1. 写 `x_probe()`（签名见 `riscv_probe.h` 的 `riscv_target_probe_fn`）——此时 DTM/DM 已起、hart0 已停、
   SBA 已激活，可直接 `riscv_mem_read32` 读芯片 ID；
2. `riscv_probe.h` weak 声明 + `target_riscv.c` 的 `riscv_vendor_probes[]` 加入；
3. flash 复用现成 `flash_native_*`（如 GD32VF103 用 F1 后端）或新建。

### 13.3 新增一个 flash 后端
实现 `flash_native_ops_t`（`erase_sector`/`program`/`mass_erase`，可选 `prepare`/`option_read`/`option_write`），
内存访问一律用 `flash_mem_read32/write32`（自动适配 SWD / RISC-V SBA），再把目标
`target_cfg_t.native_ops` 指向它。

### 13.4 新增纯逻辑模块的 QEMU 测试
把逻辑与硬件解耦（参考 `sfdp.c`），在 `tests/<name>/` 下用 `CMakeLists.txt` 直接编译该 `.c`
（必要时 `-DCONFIG_...=1`），`west build -b qemu_cortex_m3` 后运行，打印 `RESULT: PASS/FAIL`。

---

## 14. 限制与验证状态

- **硬件**: 未在真实硬件上实测。
- **构建**: `black_f407ve`（默认 + 各开关组合：RTT/OPTION/RISCV/SWO/SPI flash）、`stm32_min_dev`、
  `black_f407ve -DCONFIG_DAPLINK_TARGET_STM32=n`（验证弱符号 NULL 跳过）、`west build --sysbuild`（MCUboot）均通过。
- **QEMU 单测**: `tests/swo_itm`（ITM 解码）、`tests/sfdp`（SFDP 解析）在 `qemu_cortex_m3` 上 `RESULT: PASS`。
- SWO **Manchester** 解码未实现（需定时器输入捕获）。
- RISC-V：transport/DM/抽象/SBA/目标链路完整，但仅 GD32VF103 一个 flash 目标；其余 RISC-V 需 per-chip flash 数据。
- RTT / Option / SPI flash / ITM 均使用**自定义 vendor 协议**，需主机端小工具或 pyOCD 插件。
- 厂商 FLM / Keil `.FLM` 运行时加载**未实现**（许可与固件体积原因）；探针侧 SPI NOR 用 SFDP 自描述替代。
- blackmagic 上游为 **No-AI 贡献政策**，本移植代码请勿回馈其上游；本仓库为 Apache-2.0。
