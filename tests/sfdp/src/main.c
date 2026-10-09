/*
 * QEMU unit test for the SFDP parser (src/flash/sfdp.c) with a synthetic image.
 * Build/run: west build -b qemu_cortex_m3 tests/sfdp && west build -t run -d build
 */

#include <zephyr/kernel.h>
#include <string.h>

#include "sfdp.h"

static int failures;
static uint8_t g_image[128];

static void check(const char *name, bool ok)
{
	printk("%s: %s\n", name, ok ? "PASS" : "FAIL");
	if (!ok) {
		failures++;
	}
}

/*
 * Build a minimal SFDP image:
 *   header ("SFDP", minor, major, count=0)
 *   one parameter-table header pointing at 0x30 with id 0xFF00 (BFPT)
 *   BFPT: sector-erase opcode at [1], erase types at [28], timing/page at [40]
 */
static void build_image_buf(uint32_t density, bool exponential, uint8_t se_opcode, uint8_t se_exp,
			    uint8_t page_shift, uint8_t minor, uint8_t major)
{
	uint8_t *b;

	memset(g_image, 0, sizeof(g_image));
	memcpy(g_image, "SFDP", 4);
	g_image[4] = minor;
	g_image[5] = major;
	g_image[6] = 0;
	g_image[7] = 0xFF;

	/* parameter table header at offset 8 */
	g_image[8] = 0x00;
	g_image[9] = 1;
	g_image[10] = 1;
	g_image[11] = 9;    /* length in u32s */
	g_image[12] = 0x30; /* table address = 0x30 */
	g_image[13] = 0x00;
	g_image[14] = 0x00;
	g_image[15] = 0xFF; /* id high -> 0xFF00 */

	b = &g_image[0x30];
	b[1] = se_opcode;
	b[4] = (uint8_t)density;
	b[5] = (uint8_t)(density >> 8);
	b[6] = (uint8_t)(density >> 16);
	b[7] = (uint8_t)(((density >> 24) & 0x7FU) | (exponential ? 0x80U : 0U));
	b[28] = se_exp;
	b[29] = se_opcode;
	b[40] = (uint8_t)(page_shift << 4U);
}

static void test_basic(void)
{
	struct sfdp_params p;

	/* density 2^24 bits = 2 MiB, 4 KiB sector (exp 12), 256-byte page (shift 8), v1.6 */
	build_image_buf(24U, true, 0x20U, 12U, 8U, 6U, 1U);
	check("parse basic", sfdp_parse(g_image, sizeof(g_image), &p));
	check("capacity 2MiB", p.capacity == 2U * 1024U * 1024U);
	check("sector 4KiB", p.sector_size == 4096U);
	check("page 256", p.page_size == 256U);
	check("erase opcode 0x20", p.sector_erase_opcode == 0x20U);
}

static void test_old_version_page_default(void)
{
	struct sfdp_params p;

	/* v1.0 -> page size defaults to 256 regardless of the timing dword */
	build_image_buf(20U, true, 0x20U, 12U, 9U, 0U, 1U);
	check("parse v1.0", sfdp_parse(g_image, sizeof(g_image), &p));
	check("page default 256 (v1.0)", p.page_size == 256U);
}

static void test_bad_magic(void)
{
	struct sfdp_params p;

	build_image_buf(24U, true, 0x20U, 12U, 8U, 6U, 1U);
	memcpy(g_image, "XxXX", 4);
	check("reject bad magic", !sfdp_parse(g_image, sizeof(g_image), &p));
}

static void test_linear_density(void)
{
	struct sfdp_params p;

	/* non-exponential density: value N means N+1 bits -> (3+1)>>3 = 0 bytes */
	build_image_buf(3U, false, 0x20U, 12U, 8U, 6U, 1U);
	check("parse linear", sfdp_parse(g_image, sizeof(g_image), &p));
	check("linear capacity", p.capacity == 0U);
}

int main(void)
{
	printk("\n=== SFDP parser test ===\n");
	test_basic();
	test_old_version_page_default();
	test_bad_magic();
	test_linear_density();
	printk("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
	return 0;
}
