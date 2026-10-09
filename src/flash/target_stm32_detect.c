/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32 target probe (DBGMCU DEV_ID based).
 *
 * Strategy:
 *   1. Read the Cortex-M CPUID to learn the core type / implementer
 *   2. Probe the STM32 DBGMCU_IDCODE, trying the address appropriate for the
 *      detected core first:
 *        - 0x40015800 (F0/F3/L0/L4, Cortex-M0/M0+)
 *        - 0xE0042000 (F1/F2/F4/F7, Cortex-M3/M4)
 *   3. Match DEV_ID (bits 11:0) against the known chip table
 */

#include <zephyr/kernel.h>

#include "target_probe.h"
#include "swd_host.h"

#include "target_stm32f1.h"
#include "target_stm32f4.h"
#include "target_stm32f2.h"
#include "target_stm32f429.h"
#include "target_stm32f0.h"
#include "target_stm32f3.h"
#include "target_stm32f7.h"
#include "target_stm32l0.h"
#include "target_stm32l4.h"
#include "target_stm32l1.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(target_stm32_probe);

#define CM_CPUID_ADDR             0xE000ED00U
#define STM32_DBGMCU_IDCODE_ADDR_1  0xE0042000U /* F1/F2/F4/F7 */
#define STM32_DBGMCU_IDCODE_ADDR_2  0x40015800U /* F0/F3/L0/L4 */
#define STM32_DEV_ID_MASK         0x0FFFU

#define CM_PARTNO_M0  0xC20U
#define CM_PARTNO_M0P 0xC60U
#define CM_PARTNO_M1  0xC21U
#define CM_PARTNO_M3  0xC23U
#define CM_PARTNO_M4  0xC24U
#define CM_PARTNO_M7  0xC27U
#define CM_PARTNO_M23 0xD20U
#define CM_PARTNO_M33 0xD21U

typedef struct {
	uint32_t dev_id;
	const target_cfg_t *target;
	const char *name;
} stm32_dev_entry_t;

static const stm32_dev_entry_t stm32_dev_table[] = {
	/* STM32F1 */
	{ 0x410, &target_stm32f1,  "STM32F101/102" },
	{ 0x412, &target_stm32f1,  "STM32F103 (MD)" },
	{ 0x414, &target_stm32f1,  "STM32F103 (HD)" },
	{ 0x418, &target_stm32f1,  "STM32F105/107" },
	/* STM32F2 */
	{ 0x411, &target_stm32f2,  "STM32F205/207" },
	/* STM32F4 family */
	{ 0x413, &target_stm32f4,  "STM32F405/407" },
	{ 0x423, &target_stm32f4,  "STM32F401" },
	{ 0x433, &target_stm32f4,  "STM32F401 (E)" },
	{ 0x431, &target_stm32f4,  "STM32F411" },
	{ 0x421, &target_stm32f4,  "STM32F446" },
	{ 0x419, &target_stm32f429, "STM32F429/439 (2MB)" },
	/* STM32F0 */
	{ 0x440, &target_stm32f0,  "STM32F030" },
	{ 0x444, &target_stm32f0,  "STM32F03x" },
	{ 0x445, &target_stm32f0,  "STM32F072" },
	{ 0x448, &target_stm32f0,  "STM32F07x" },
	{ 0x442, &target_stm32f0,  "STM32F09x" },
	/* STM32F3 */
	{ 0x422, &target_stm32f3,  "STM32F30x/31x/32x" },
	{ 0x432, &target_stm32f3,  "STM32F37x/38x" },
	{ 0x438, &target_stm32f3,  "STM32F334" },
	{ 0x439, &target_stm32f3,  "STM32F302/303/358" },
	/* STM32F7 */
	{ 0x449, &target_stm32f7,  "STM32F74x/75x" },
	{ 0x450, &target_stm32f7,  "STM32F75x" },
	{ 0x451, &target_stm32f7,  "STM32F76x/77x" },
	{ 0x452, &target_stm32f7,  "STM32F72x/73x" },
	/* STM32L4 */
	{ 0x415, &target_stm32l4,  "STM32L47x/48x" },
	{ 0x461, &target_stm32l4,  "STM32L496/L4A6" },
	{ 0x462, &target_stm32l4,  "STM32L45x/46x" },
	{ 0x470, &target_stm32l4,  "STM32L4Rx/L4P5" },
	/* STM32L1 */
	{ 0x416, &target_stm32l1,  "STM32L15x" },
	/* STM32L0 */
	{ 0x407, &target_stm32l0,  "STM32L0x1" },
	{ 0x417, &target_stm32l0,  "STM32L0x2/3" },
};

static uint32_t try_read_dev_id(uint32_t addr)
{
	uint32_t idcode;

	if (swd_read_word(addr, &idcode)) {
		return idcode & STM32_DEV_ID_MASK;
	}
	return 0xFFFFFFFFU;
}

static const target_cfg_t *lookup_stm32(uint32_t dev_id)
{
	for (size_t i = 0; i < sizeof(stm32_dev_table) / sizeof(stm32_dev_table[0]); i++) {
		if (stm32_dev_table[i].dev_id == dev_id) {
			return stm32_dev_table[i].target;
		}
	}
	return NULL;
}

static const char *cm_core_name(uint32_t part_no)
{
	switch (part_no) {
	case CM_PARTNO_M0:  return "Cortex-M0";
	case CM_PARTNO_M0P: return "Cortex-M0+";
	case CM_PARTNO_M1:  return "Cortex-M1";
	case CM_PARTNO_M3:  return "Cortex-M3";
	case CM_PARTNO_M4:  return "Cortex-M4";
	case CM_PARTNO_M7:  return "Cortex-M7";
	case CM_PARTNO_M23: return "Cortex-M23";
	case CM_PARTNO_M33: return "Cortex-M33";
	default:            return "Cortex-M (unknown)";
	}
}

static bool read_cpuid(uint32_t *part_no)
{
	uint32_t cpuid;

	if (!swd_read_word(CM_CPUID_ADDR, &cpuid)) {
		return false;
	}
	*part_no = (cpuid >> 4U) & 0xFFFU;
	LOG_INF("CPUID 0x%08X: %s", cpuid, cm_core_name(*part_no));
	return true;
}

const target_cfg_t *stm32_probe(uint32_t dp_idcode)
{
	(void)dp_idcode;

	uint32_t part_no = 0;
	const bool have_cpuid = read_cpuid(&part_no);
	const bool m0_class = have_cpuid && (part_no == CM_PARTNO_M0 || part_no == CM_PARTNO_M0P);

	const uint32_t addrs[2] = { STM32_DBGMCU_IDCODE_ADDR_1, STM32_DBGMCU_IDCODE_ADDR_2 };
	const uint32_t order[2] = { m0_class ? 1U : 0U, m0_class ? 0U : 1U };

	for (int o = 0; o < 2; o++) {
		const uint32_t addr = addrs[order[o]];
		const uint32_t dev_id = try_read_dev_id(addr);

		if (dev_id == 0xFFFFFFFFU || dev_id == 0U) {
			continue;
		}
		const target_cfg_t *target = lookup_stm32(dev_id);

		if (target != NULL) {
			LOG_INF("STM32 DEV_ID=0x%03X via DBGMCU @0x%08X", dev_id, addr);
			return target;
		}
		LOG_WRN("Unknown STM32 DEV_ID 0x%03X at 0x%08X", dev_id, addr);
	}
	return NULL;
}
