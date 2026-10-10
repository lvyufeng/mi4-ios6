/*
 * 915-B rung 0 (host half) - the ONE /memory/reg region accessor, as a PURE module.
 *
 * WHY THIS EXISTS.  915-B v2 section 2.4/6.8: the DT /memory/reg property becomes a REGION LIST
 * whose FIRST PAIR IS THE BOOT REGION, and the low bank is APPENDED after it (rung 1, 976).  Today
 * three independent readers walk that property - `pe_state.c` (words 0/1), `pexpert.c`
 * (mem_reg[0]/[1]) and `xnu_pe_init_platform_false.c` (index 0u/1u) - each hard-coding "the boot
 * region is pair 0".  That convention is one the DATA can violate: a list that put the low bank
 * first would read as the boot region, and the reader would carry a wrong base with no refusal.
 *
 * The v2 fix is a SINGLE accessor whose walk is a BASE-MATCH - the boot region is the pair whose
 * base equals the boot base, and it MUST be pair 0 - so the convention stops being an assumption
 * and becomes a checked invariant.  A refusal forbids re-implementing `reg_value(...,n,0)` outside
 * this accessor.  This file is the first half of rung 0 (the removal of the three copies).
 *
 * WHY A PURE MODULE.  The walk is arithmetic over an array of host-order words; it needs no DT
 * header, no XNU type, no device.  Keeping it pure (`<stdint.h>` only) lets `make check` compile it
 * on the HOST and exercise it against the exact word arrays the payload emits, without a compiler
 * for the target and WITHOUT touching the payload (this file is not yet in `scripts/build.sh`'s
 * SOURCES - wiring it in is the region-registration rung's job, so the shipped press arm stays
 * byte-frozen; [[mi4-one-value-two-definitions]]).
 *
 * GATING.  `#if STAGE90_XNU_REGIONS`, never `#ifdef`: the switch is always defined 0 or 1 by
 * `scripts/build.sh` and by the host test, so a 0 must read as OFF and an undefined name must not
 * be a second spelling of off ([[mi4-off-option-two-spellings]]).
 */
#include "xnu_memory_regions.h"

#if STAGE90_XNU_REGIONS

/*
 * Walk a /memory/reg word array into a region list.
 *
 *   reg_words   host-order words: {base0, size0, base1, size1, ...} (apple_dt_get_prop's output)
 *   word_count  number of words (must be even; a pair is base,size)
 *   boot_base   the boot region's physical base (args->physBase on the entry path)
 *   regions     output array of regions (may be NULL to count only)
 *   max_regions capacity of `regions`
 *
 * Returns the number of REGIONS (pairs) on success, or 0 on any refusal:
 *   - word_count is 0, odd, or below one pair (a region list needs at least base+size);
 *   - the FIRST pair's base is not `boot_base` (the boot region MUST be pair 0 - v2 section 2.4;
 *     a low-bank-first list is refused, not silently mis-read as the boot region, the exact defect
 *     the accessor exists to kill);
 *   - a later pair has size 0 (an empty region is a malformed list).
 * A `regions == NULL` call is a pure COUNT (the caller may size its array first); the refusals still
 * apply, and 0 is still the refusal signal.
 */
uint32_t
xnu_memory_regions_walk(const uint32_t *reg_words, uint32_t word_count, uint32_t boot_base,
                        struct xnu_mem_region *regions, uint32_t max_regions)
{
	if (reg_words == 0 || word_count < 2u || (word_count & 1u) != 0u) {
		return 0u;
	}
	if (reg_words[0] != boot_base) {
		/* The boot region is not pair 0.  v2 section 2.4: refused. */
		return 0u;
	}

	uint32_t pairs = word_count >> 1;
	for (uint32_t i = 0u; i < pairs; i++) {
		uint32_t base = reg_words[i * 2u];
		uint32_t size = reg_words[i * 2u + 1u];
		if (i != 0u && size == 0u) {
			return 0u;      /* an empty non-boot region is malformed */
		}
		if (regions != 0 && i < max_regions) {
			regions[i].base = base;
			regions[i].size = size;
		}
	}
	return pairs;
}

#endif /* STAGE90_XNU_REGIONS */