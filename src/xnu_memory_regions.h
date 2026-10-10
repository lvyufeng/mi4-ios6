/*
 * 915-B rung 0 - the region model's ONE accessor, declared for both the payload and the host test.
 *
 * A region is a physical span the DT /memory/reg list names: [base, base + size).  The FIRST pair
 * of the list is the boot region (v2 section 2.4); later pairs are additional banks (rung 1 appends
 * the low bank).  The type is deliberately uint32_t-only (the property's own word width), so this
 * header compiles on the host as well as the target and the host test can exercise the walk against
 * the exact arrays the payload emits.
 */
#ifndef XNU_MEMORY_REGIONS_H
#define XNU_MEMORY_REGIONS_H

#include <stdint.h>

struct xnu_mem_region {
	uint32_t base;      /* physical base address */
	uint32_t size;      /* span length; [base, base + size) */
};

/*
 * Walk /memory/reg words into a region list.  Returns the region (pair) count, or 0 on refusal
 * (empty/odd list, boot region not pair 0, an empty later region).  See xnu_memory_regions.c.
 */
uint32_t xnu_memory_regions_walk(const uint32_t *reg_words, uint32_t word_count, uint32_t boot_base,
                                 struct xnu_mem_region *regions, uint32_t max_regions);

#endif /* XNU_MEMORY_REGIONS_H */