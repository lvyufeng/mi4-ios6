/*
 * Host unit test for 915-B rung 0's region accessor (`src/xnu_memory_regions.c`).
 *
 * Compiled and run on the HOST by `tools/check_memory_regions_walk.py` - no target compiler and no
 * device.  It exercises the walk against the exact word arrays the payload emits for the shipped DT
 * (pair 0 = boot region) and for 976's rung-1 DT (low bank APPENDED), and it asserts each refusal:
 * the boot region must be pair 0, the list must be even and non-empty, and a later region may not be
 * empty.  A walk that ACCEPTED a low-bank-first list would mis-read the low bank as the boot region
 * - the one-value-two-definitions defect the accessor exists to kill, so that case is the test's
 * sharpest bite ([[mi4-one-value-two-definitions]]).
 *
 * Exit 0 iff every case holds; 1 otherwise, naming the first case that did not.
 */
#include <stdint.h>
#include <stdio.h>

#include "xnu_memory_regions.h"

#define BOOT_BASE 0x80000000u
#define BOOT_SIZE 0x5e500000u
#define LOW_BASE  0x00000000u
#define LOW_SIZE  0x60000000u

static int failures = 0;

static void
expect_count(const char *what, const uint32_t *words, uint32_t n, uint32_t boot_base,
             uint32_t want)
{
	uint32_t got = xnu_memory_regions_walk(words, n, boot_base, 0, 0u);
	if (got != want) {
		printf("FAIL: %s - count %u, want %u\n", what, got, want);
		failures++;
	}
}

static void
expect_region(const char *what, const uint32_t *words, uint32_t n, uint32_t boot_base,
              uint32_t idx, uint32_t wbase, uint32_t wsize)
{
	struct xnu_mem_region r[8];
	uint32_t rc = xnu_memory_regions_walk(words, n, boot_base, r, 8u);
	if (rc == 0u || idx >= rc) {
		printf("FAIL: %s - walk returned %u, cannot read region %u\n", what, rc, idx);
		failures++;
		return;
	}
	if (r[idx].base != wbase || r[idx].size != wsize) {
		printf("FAIL: %s - region %u = {0x%08x, 0x%08x}, want {0x%08x, 0x%08x}\n",
		       what, idx, r[idx].base, r[idx].size, wbase, wsize);
		failures++;
	}
}

int
main(void)
{
	/* The shipped DT: the boot pair alone. */
	static const uint32_t shipped[] = { BOOT_BASE, BOOT_SIZE };
	expect_count("shipped: boot pair alone", shipped, 2u, BOOT_BASE, 1u);
	expect_region("shipped: region 0 is the boot region", shipped, 2u, BOOT_BASE, 0u,
	              BOOT_BASE, BOOT_SIZE);

	/* 976's rung-1 DT: the low bank APPENDED after the boot pair. */
	static const uint32_t rung1[] = { BOOT_BASE, BOOT_SIZE, LOW_BASE, LOW_SIZE };
	expect_count("rung1: two regions", rung1, 4u, BOOT_BASE, 2u);
	expect_region("rung1: region 0 is the boot region", rung1, 4u, BOOT_BASE, 0u,
	              BOOT_BASE, BOOT_SIZE);
	expect_region("rung1: region 1 is the low bank", rung1, 4u, BOOT_BASE, 1u,
	              LOW_BASE, LOW_SIZE);
	/* COUNT mode (regions == NULL) still counts, and still refuses a bad list. */
	expect_count("rung1: count-mode", rung1, 4u, BOOT_BASE, 2u);

	/* REFUSALS. */
	static const uint32_t low_first[] = { LOW_BASE, LOW_SIZE, BOOT_BASE, BOOT_SIZE };
	expect_count("refuse: low bank first (boot is not pair 0)", low_first, 4u, BOOT_BASE, 0u);

	static const uint32_t odd[] = { BOOT_BASE, BOOT_SIZE, LOW_BASE };
	expect_count("refuse: odd word count", odd, 3u, BOOT_BASE, 0u);

	expect_count("refuse: empty list", shipped, 0u, BOOT_BASE, 0u);

	static const uint32_t empty_later[] = { BOOT_BASE, BOOT_SIZE, LOW_BASE, 0u };
	expect_count("refuse: an empty later region", empty_later, 4u, BOOT_BASE, 0u);

	/* A boot base the list does not carry at pair 0 is refused (the base-match invariant). */
	expect_count("refuse: pair 0 base != boot base", shipped, 2u, 0x80001000u, 0u);

	if (failures != 0) {
		printf("memory_regions_walk: %d case(s) FAILED\n", failures);
		return 1;
	}
	printf("memory_regions_walk: ok - boot pair 0 held, the low bank appended, every refusal bit\n");
	return 0;
}