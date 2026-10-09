/*
 * QEMU host-side unit test for the SWO ITM software decoder.
 * Build/run: west build -b qemu_cortex_m3 tests/swo_itm && west build -t run -d build
 */

#include <zephyr/kernel.h>
#include <string.h>

#include "swo_itm.h"

static int failures;

static void check(const char *name, bool ok)
{
	printk("%s: %s\n", name, ok ? "PASS" : "FAIL");
	if (!ok) {
		failures++;
	}
}

/* ITM instrumentation packet: header = (channel << 3) | size_code, 1..3 => 1/2/4 bytes. */

static void test_decode_channel0(void)
{
	static const uint8_t stream[] = { 0x01, 'H', 0x01, 'i', 0x01, '\n' };
	uint8_t out[16];

	swo_itm_reset();
	swo_itm_set_mask(1U << 0);
	swo_itm_feed(stream, sizeof(stream));

	const uint32_t n = swo_itm_read(out, sizeof(out));
	check("decode channel 0", n == 3 && memcmp(out, "Hi\n", 3) == 0);
}

static void test_mask_filters(void)
{
	static const uint8_t stream[] = { 0x01, 'A' };
	uint8_t out[16];

	swo_itm_reset();
	swo_itm_set_mask(1U << 1); /* channel 0 not enabled */
	swo_itm_feed(stream, sizeof(stream));
	check("mask filters other channel", swo_itm_read(out, sizeof(out)) == 0);
}

static void test_channel_and_size(void)
{
	/* channel 2 (header = 2<<3 | 2 = 0x12), 2-byte payload 0x1234 LE */
	static const uint8_t stream[] = { 0x12, 0x34, 0x12 };
	uint8_t out[16];

	swo_itm_reset();
	swo_itm_set_mask(1U << 2);
	swo_itm_feed(stream, sizeof(stream));
	const uint32_t n = swo_itm_read(out, sizeof(out));
	check("channel 2 / 2-byte", n == 2 && out[0] == 0x34 && out[1] == 0x12);
}

static void test_split_packet(void)
{
	/* header and payload fed in separate chunks */
	uint8_t out[16];
	static const uint8_t hdr[] = { 0x01 };
	static const uint8_t dat[] = { 'X', 'Y' };

	swo_itm_reset();
	swo_itm_set_mask(1U << 0);
	swo_itm_feed(hdr, sizeof(hdr));
	swo_itm_feed(dat, sizeof(dat));
	const uint32_t n = swo_itm_read(out, sizeof(out));
	check("split packet", n == 1 && out[0] == 'X');
}

int main(void)
{
	printk("\n=== SWO ITM decoder test ===\n");
	test_decode_channel0();
	test_mask_filters();
	test_channel_and_size();
	test_split_packet();
	printk("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
	return 0;
}
