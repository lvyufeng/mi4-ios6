/*
 * Host unit test for 915-B rung 0's region accessor (`src/regions/xnu_memory_regions.c`).
 *
 * Compiled and run on the HOST by `tools/check_memory_regions_walk.py` - no target compiler and no
 * device.  It exercises the walk against the exact word arrays the payload emits for the shipped DT
 * (pair 0 = boot region), for 976's rung-1 DT (low bank APPENDED whole), and for L0.2's rung-2 DT
 * (the low bank CARVED into its three `/proc/iomem` spans), and it asserts each refusal:
 * the boot region must be pair 0, the list must be even and non-empty, no region may be empty, and
 * two regions may not overlap.  A walk that ACCEPTED a low-bank-first list would mis-read the low
 * bank as the boot region - the one-value-two-definitions defect the accessor exists to kill
 * ([[mi4-one-value-two-definitions]]).
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

/* L0.2's rung-2 spans: the low bank's three /proc/iomem `System RAM` lines below the boot bank
 * (`out/stage90/device-reads/mi4-iomem-and-mem.txt` lines 4-6), size = end - start + 1. */
#define LOW_SPAN0_BASE 0x00000000u
#define LOW_SPAN0_SIZE 0x05a00000u
#define LOW_SPAN1_BASE 0x0d200000u
#define LOW_SPAN1_SIZE 0x02800000u
#define LOW_SPAN2_BASE 0x0ff00000u
#define LOW_SPAN2_SIZE 0x50100000u

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

	/* 976's rung-1 DT: the low bank APPENDED whole after the boot pair. */
	static const uint32_t rung1[] = { BOOT_BASE, BOOT_SIZE, LOW_BASE, LOW_SIZE };
	expect_count("rung1: two regions", rung1, 4u, BOOT_BASE, 2u);
	expect_region("rung1: region 0 is the boot region", rung1, 4u, BOOT_BASE, 0u,
	              BOOT_BASE, BOOT_SIZE);
	expect_region("rung1: region 1 is the low bank", rung1, 4u, BOOT_BASE, 1u,
	              LOW_BASE, LOW_SIZE);
	/* COUNT mode (regions == NULL) still counts, and still refuses a bad list. */
	expect_count("rung1: count-mode", rung1, 4u, BOOT_BASE, 2u);

	/* L0.2's rung-2 DT: the boot pair, then the low bank's THREE carved spans - a FOUR-pair walk,
	 * eight words.  (The L0.2 brief says "10-word" and "four extra pairs" while listing three
	 * spans; three pairs is 2 + 3*2 = 8 words, which is the data the brief's own spans describe
	 * and which the enabled arm's `_Static_assert(... == 8)` carries - so the count follows the
	 * spans, not the sentence.) */
	static const uint32_t rung2[] = {
		BOOT_BASE, BOOT_SIZE,
		LOW_SPAN0_BASE, LOW_SPAN0_SIZE,
		LOW_SPAN1_BASE, LOW_SPAN1_SIZE,
		LOW_SPAN2_BASE, LOW_SPAN2_SIZE,
	};
	expect_count("rung2: four regions", rung2, 8u, BOOT_BASE, 4u);
	expect_region("rung2: region 0 is the boot region", rung2, 8u, BOOT_BASE, 0u,
	              BOOT_BASE, BOOT_SIZE);
	expect_region("rung2: region 1 is low span 0", rung2, 8u, BOOT_BASE, 1u,
	              LOW_SPAN0_BASE, LOW_SPAN0_SIZE);
	expect_region("rung2: region 2 is low span 1", rung2, 8u, BOOT_BASE, 2u,
	              LOW_SPAN1_BASE, LOW_SPAN1_SIZE);
	expect_region("rung2: region 3 is low span 2", rung2, 8u, BOOT_BASE, 3u,
	              LOW_SPAN2_BASE, LOW_SPAN2_SIZE);
	/* COUNT mode over the four-pair list. */
	expect_count("rung2: count-mode", rung2, 8u, BOOT_BASE, 4u);

	/* REFUSALS. */
	static const uint32_t low_first[] = { LOW_BASE, LOW_SIZE, BOOT_BASE, BOOT_SIZE };
	expect_count("refuse: low bank first (boot is not pair 0)", low_first, 4u, BOOT_BASE, 0u);

	static const uint32_t odd[] = { BOOT_BASE, BOOT_SIZE, LOW_BASE };
	expect_count("refuse: odd word count", odd, 3u, BOOT_BASE, 0u);

	expect_count("refuse: empty list", shipped, 0u, BOOT_BASE, 0u);

	static const uint32_t empty_later[] = { BOOT_BASE, BOOT_SIZE, LOW_BASE, 0u };
	expect_count("refuse: an empty later region", empty_later, 4u, BOOT_BASE, 0u);

	static const uint32_t empty_boot[] = { BOOT_BASE, 0u, LOW_BASE, LOW_SIZE };
	expect_count("refuse: an empty boot region", empty_boot, 4u, BOOT_BASE, 0u);

	/* Two regions that overlap are refused (a region list double-counting memory). */
	static const uint32_t overlap[] = { BOOT_BASE, BOOT_SIZE, 0x80000000u, 0x10000000u };
	expect_count("refuse: a later region overlapping the boot region", overlap, 4u, BOOT_BASE, 0u);

	/* A span that reaches into the boot region is refused even when its base is low. */
	static const uint32_t reaches[] = { BOOT_BASE, BOOT_SIZE, 0x70000000u, 0x20000000u };
	expect_count("refuse: a low span reaching into the boot region", reaches, 4u, BOOT_BASE, 0u);

	/* A boot base the list does not carry at pair 0 is refused (the base-match invariant). */
	expect_count("refuse: pair 0 base != boot base", shipped, 2u, 0x80001000u, 0u);

	if (failures != 0) {
		printf("memory_regions_walk: %d case(s) FAILED\n", failures);
		return 1;
	}
	printf("memory_regions_walk: ok - boot pair 0 held, rung-1 low bank and rung-2 three carved "
	       "spans appended, every refusal bit\n");
	return 0;
}