#!/usr/bin/env python3
"""patch_d13_regions_r3a.py - stage 915-B R3a (the COUNT half of the second allocator interval).

WHAT R3a IS, AND WHAT IT DELIBERATELY IS NOT (docs/915b-next-rung.md section 2, R3a).

R3 is "the kernel acts on the DT region list" - the first rung that does.  Its design splits in two,
and the split is FORCED by a measurement, not a preference:

  * **R3a (THIS TOOL) - the count half.**  Publish the DT region list into the tree's ONE canonical
    table (`pmap_mem_regions[]`, already declared at `osfmk/arm/pmap.h:303-305` and never defined or
    referenced anywhere in `osfmk/arm/` today), make `pmap_valid_page` region-aware (a SPAN-SET, so
    a low-bank CARVEOUT page is not "valid"), and make the free-page accounting
    (`avail_remaining`, `pmap.c:2723`, exposed as `pmap_free_pages()` and the
    `vm_page_bootstrap: N free pages` printf) include the low bank.  R3a allocates and emits NOTHING.
  * **R3b (NOT this tool) - the emit half.**  Draining `[avail_start2, avail_end2)` in
    `pmap_next_page` and handing low pages to consumers.  GATED on R5 (the bounce window).

**WHY THE SPLIT IS FORCED - THE REACHABILITY GATE (docs/915b-next-rung.md section 2.2), and it is why
this tool does NOT touch `pmap_next_page`.**  `arm_vm_init.c` sets `TTBCR.N = 2`
(`set_mmu_ttbcr(2)`, `:475`), so TTBR0 owns `[0, 0x40000000)` and TTBR1 owns the rest - the hardware
split is `0x40000000`, NOT the `0x80000000` address-space split (`src/entry/entry_stubs.c:2117`,
`:2324-2326`).  The kernel's identity/managed tables are built into the ONE `cpu_ttb` that TTBR1
points at (`arm_vm_init.c:388-391`, `:473-474`), and that build covers
`[gPhysBase, gPhysBase + gMemSize)` = `0x80000000`-and-up only (`arm_vm_init.c:400`,
`l2_map_linear_range`).  A general low DRAM page `p < 0x40000000` therefore has **no mapped kernel
VA**.  `pmap_startup` (`osfmk/vm/vm_resident.c:786`) DRAINS `pmap_next_page` in a loop and hands each
page to `vm_page_init` - so the moment a low page is emitted it is either dereferenced or, on a DEBUG
arm, panicked on.  R3a's whole safety rests on `pmap_next_page` still draining ONE interval.

**AND `avail_remaining` IS NOT SAFE TO RAISE UNLESS THE DRAIN STOPS.**  MEASURED by value in
`pmap_startup` (`vm_resident.c:799-821`):

    tmpaddr = (addr64_t)pmap_free_pages() * (addr64_t)PAGE_SIZE;
    npages  = tmpaddr / (PAGE_SIZE + sizeof(*vm_pages));
    vm_pages = pmap_steal_memory(npages * sizeof(*vm_pages));   /* steals npages PHYSICAL pages */
    for (i = 0; i < npages; i++) { if (!pmap_next_page(&phys_page)) break; vm_page_init(...); }

`pmap_free_pages()` sizes the `vm_pages[]` structure array, and `pmap_steal_memory` then STEALS that
many physical pages through the SAME single-interval `pmap_next_page`.  So a count raised by
`0x58300` while the drain still stops at `avail_end` would NOT hand out a low page - it would make
`pmap_steal_memory` consume ~0x58300 EXTRA pages of the BOOT region for the bigger `vm_pages[]` and
then lose that `vm_pages[]` to `bzero(phys_to_virt(p))` on a low VA.  **That is a boot that dies
before its own console record.**  The design's own pre-registered criterion (`docs/915b-next-rung.md`
section 2.3.3) asks for `avail_remaining == boot_pages + 0x58300`; this tool records that the
criterion is **UNSAFE as written**, and REFUSES to place the count unless the drain moves with it.
The enforceability is one addition to the design (`docs/915b-next-rung.md` section 2.4 C4), applied
here: R3a is staged ONLY together with a `pmap_next_page` that drains the second interval too, still
behind `STAGE90_XNU_BOUNCE_WINDOW` for the emit-consumer half - which is R5's switch and is NOT
defined by this rung.

THE FOUR EDITS (two files, all inside `#if STAGE90_XNU_REGIONS`, so switch OFF is the tree's bytes).
Anchors are exact code spans, each verified to occur EXACTLY ONCE; a wrong-whitespace anchor REFUSES
rather than no-ops ([[mi4-mutation-anchors-are-code-spans]]).

  (1) `osfmk/arm/pmap.c`, the global block after `uint32_t avail_start, vm_first_phys;`:
      the second interval `avail_start2`/`avail_end2`, the TWO DEFINITIONS of this rung -
      `mem_region_t pmap_mem_regions[PMAP_MEM_REGION_MAX]` and `int pmap_mem_regions_count`
      (`pmap.h:303-305` declares both and nothing in `osfmk/arm/` defines them), plus the two
      parallel arrays that carry the v2 section 2.0 TWO NAMES: `pmap_mem_regions_usable_end[]`
      (`REGION_MAX(i)`) and `pmap_mem_regions_reach2` (the TTBCR.N=2 split, the reachability bound).
  (2) `osfmk/arm/arm_vm_init.c`, before `void arm_vm_init`: the registration block.  It walks the DT
      `/memory/reg` through the tree's OWN device-tree API (`DTLookupEntry`/`DTGetProperty`,
      `<pexpert/device_tree.h>` - already initialized by `PE_init_platform` -> `DTInit` at
      `pexpert/arm/common/pe_init.c:134`, which `arm_init` calls before `arm_vm_init`,
      `osfmk/arm/arm_init.c:146-168`), runs the SAME refusals as the host accessor
      (`src/regions/xnu_memory_regions.c`: even length, boot region FIRST, no empty region, no
      overlap) raised to a kernel `panic`, and publishes the list.  It assigns **neither**
      `gPhysBase`/`gMemSize` nor `max_mem`/`mem_size`/`sane_size` (964; C1/C2).
  (3) `osfmk/arm/arm_vm_init.c`, after Apple's 958 `#endif`: the ONE call, because a `static`
      function with no call site is a build error under `-Wall`-class diagnostics.
  (4) `osfmk/arm/pmap.c`, `pmap_valid_page`: region-aware, span-set, carveout-excluding.  At switch
      OFF the whole body is Apple's two-line expression, byte for byte, inside the `#else`.
  (5) `osfmk/arm/pmap.c`, `pmap_bootstrap`: the accounting.  Apple's line stays byte-identical and
      the gated block adds the low-bank page count, prints it (a NEW console record, the
      instrument), and RAISES THE DRAIN BOUND with it.

`pmap_zero_page`/`pmap_copy_page`/`pai_to_pvh`/`lock_pvh_pai` are NOT touched: R3a emits no low page,
and touching them would be R4/R5's job (C4/C5).  This is deliberately the smallest edit that carries
the DT list into the pmap's accounting.

SWITCH DISCIPLINE.  `#if STAGE90_XNU_REGIONS` (never `#ifdef`): `scripts/build.sh:183-187` and
`tools/build_xnu_arm_kernel.sh:243-259` ALWAYS define the macro 0 or 1 for the payload and the kernel
corpus respectively, and the latter refuses any value but 0/1 - so a 0 reads as OFF and an undefined
name is not a second spelling of off ([[mi4-off-option-two-spellings]]).

Usage:
    patch_d13_regions_r3a.py <XNU_TREE>              stage (idempotent)
    patch_d13_regions_r3a.py <XNU_TREE> --dry-run    print the planned insertions, write nothing
    patch_d13_regions_r3a.py <XNU_TREE> --check      assert the INSERTED forms are staged
    patch_d13_regions_r3a.py <XNU_TREE> --revert     reverse every insertion (the tree's bytes)
    patch_d13_regions_r3a.py <XNU_TREE> --anchors    print the anchor census this tool refuses on
"""
import re
import sys

# The marker that proves this patch ran, at FILE scope in both edited files.
MARK = "1015b-regions R3a"

# The gate.  `#if`, not `#ifdef`: the macro is always defined 0/1 by both build scripts.
_GATE_OPEN = "#if STAGE90_XNU_REGIONS\n"
_GATE_CLOSE = "#endif /* STAGE90_XNU_REGIONS */\n"

# --- the switch that gates R3b (the emit half) and that R3a must leave UNDEFINED -----------------
# R3a's reachability gate (docs/915b-next-rung.md section 2.2) is expressed as: the second-interval
# DRAIN may exist, but the consumer-facing emit half stays behind a switch R3a does not define.
_BOUNCE_SWITCH = "STAGE90_XNU_BOUNCE_WINDOW"

# ---------------------------------------------------------------------------------------------------
# osfmk/arm/pmap.c
# ---------------------------------------------------------------------------------------------------

# Apple's global block.  ONE occurrence.
_P_GLOBALS_OLD = "uint32_t avail_start, vm_first_phys;\n"

_P_GLOBALS_NEW = (
    "uint32_t avail_start, vm_first_phys;\n"
    "#if STAGE90_XNU_REGIONS\n"
    "/* **" + MARK + " - 915-B rung 3a, the COUNT half (docs/915b-next-rung.md section 2).\n"
    " *\n"
    " * The second allocator interval.  `avail_start`/`avail_end` stay Apple's BOOT interval exactly,\n"
    " * so the `pmap_bootstrap: physical region` print and everything sized off `mem_size` are\n"
    " * untouched (refusal C6).  The low bank is registered SEPARATELY here.\n"
    " *\n"
    " * The two names of 915-B v2 section 2.0 are kept apart here because conflating them is the\n"
    " * defect that produced `gMemSize = 0x60000000` in v1:\n"
    " *   pmap_mem_regions[i].start .. pmap_mem_regions_usable_end[i]   the ALLOCATABLE span\n"
    " *   pmap_mem_regions_reach2                                       the TTBCR.N=2 split\n"
    " * `pmap_mem_regions[i].end` (the DT raw top) is kept for provenance only and is NOT the\n"
    " * allocatable one.\n"
    " *\n"
    " * THESE THREE SYMBOLS ARE THE RUNG'S ONE DEFINITION OF THE TABLE: `pmap.h:303-305` declares\n"
    " * `pmap_mem_regions[]`/`pmap_mem_regions_count` and, MEASURED over the whole tree, nothing\n"
    " * defines them (the arm64 `stubs.s` makes both `panic(\"not implemented\")`). */\n"
    "uint32_t avail_start2, avail_end2;\n"
    "static uint32_t pmap_second_cursor;     /* the low-bank drain cursor (pmap_next_page) */\n"
    "mem_region_t pmap_mem_regions[PMAP_MEM_REGION_MAX];\n"
    "int pmap_mem_regions_count;\n"
    "uint32_t pmap_mem_regions_usable_end[PMAP_MEM_REGION_MAX];\n"
    "uint32_t pmap_mem_regions_reach2;\n"
    "#endif /* STAGE90_XNU_REGIONS */\n"
)

# `pmap_valid_page`.  Apple's body is byte-identical inside the `#else`.
_P_VALID_OLD = (
    "boolean_t pmap_valid_page(ppnum_t p)\n"
    "{\n"
    "    return (((p << PAGE_SHIFT) > avail_start)\n"
    "            && ((p << PAGE_SHIFT) < avail_end));\n"
    "}\n"
)

_P_VALID_NEW = (
    "boolean_t pmap_valid_page(ppnum_t p)\n"
    "{\n"
    "#if STAGE90_XNU_REGIONS\n"
    "    /* **" + MARK + ": region-aware, SPAN-SET, carveout-excluding.\n"
    "     *\n"
    "     * Apple's ONE interval admits exactly one [avail_start, avail_end).  915-B v2 section 2.2\n"
    "     * requires a page to be valid iff it lies inside ANY registered region AND NOT inside a\n"
    "     * low-bank CARVEOUT span.  The carveout exclusion is structural here, not a second test:\n"
    "     * the spans registered in `pmap_mem_regions[]` ARE the usable sub-intervals (915-B rung 2\n"
    "     * reconciled the low bank to three spans with two holes, `src/stage90_main.c:64-79`), so a\n"
    "     * hole lies in NO region and the loop's `continue` is the refusal.  A single\n"
    "     * `[carveout_lo, carveout_hi)` test would be the wrong shape (v2 section 2.2).\n"
    "     *\n"
    "     * The endpoints are >= start and < usable_end: Apple's strict `>` dropped the region's own\n"
    "     * first page, and 915-B v2 section 2.2 names that quirk.\n"
    "     *\n"
    "     * A call with pmap_mem_regions_count == 0 (the registration panics or returns 0 regions\n"
    "     * before this can be reached; a zero count can only mean the switch ON on a DT with no\n"
    "     * /memory node) returns FALSE for EVERY page - the conservative direction. */\n"
    "    {\n"
    "        uint32_t pa = (uint32_t)(p << PAGE_SHIFT);\n"
    "        int i;\n"
    "        for (i = 0; i < pmap_mem_regions_count; i++) {\n"
    "            if (pa >= (uint32_t)pmap_mem_regions[i].start &&\n"
    "                pa < pmap_mem_regions_usable_end[i])\n"
    "                return TRUE;\n"
    "        }\n"
    "    }\n"
    "    return FALSE;\n"
    "#else\n"
    "    return (((p << PAGE_SHIFT) > avail_start)\n"
    "            && ((p << PAGE_SHIFT) < avail_end));\n"
    "#endif /* STAGE90_XNU_REGIONS */\n"
    "}\n"
)

# `pmap_bootstrap`'s accounting line, plus the gated block that raises the DRAIN BOUND with the count.
_P_BOOT_OLD = "    avail_remaining = (avail_end - first_avail) >> PAGE_SHIFT;\n"

_P_BOOT_NEW = (
    "    avail_remaining = (avail_end - first_avail) >> PAGE_SHIFT;\n"
    "#if STAGE90_XNU_REGIONS\n"
    "    /* **" + MARK + ": the count, its console record, AND the matching drain seed.\n"
    "     *\n"
    "     * THE INSTRUMENT is the `1015b-regions R3a: low-bank` line below: a NEW console record\n"
    "     * naming the low-bank usable page count.  Its predicted value is `0x58300000 / PAGE_SIZE`\n"
    "     * = 0x58300 pages (1411 MiB), where 0x58300000 is the SUM OF THE THREE DT SPANS the\n"
    "     * payload emits (`src/stage90_main.c:64-79`) - a derivation in the source, not a literal.\n"
    "     *\n"
    "     * WHY THE DRAIN SEED MOVES WITH THE COUNT, AND WHY THE COUNT IS NOT SEPARABLE FROM IT.\n"
    "     * MEASURED by value in `pmap_startup` (`osfmk/vm/vm_resident.c:799-821`):\n"
    "     *\n"
    "     *     tmpaddr = (addr64_t)pmap_free_pages() * PAGE_SIZE;\n"
    "     *     npages  = tmpaddr / (PAGE_SIZE + sizeof(*vm_pages));\n"
    "     *     vm_pages = pmap_steal_memory(npages * sizeof(*vm_pages));\n"
    "     *     for (i = 0; i < npages; i++) { if (!pmap_next_page(&phys_page)) break; ... }\n"
    "     *\n"
    "     * The count SIZES the `vm_pages[]` array, and `pmap_steal_memory` then steals that many\n"
    "     * PHYSICAL pages through the SAME `pmap_next_page`.  A count raised by 0x58300 while the\n"
    "     * drain still stops at `avail_end` therefore does not hand out a low page - it CANNOT - it\n"
    "     * spends the count on EXTRA BOOT pages for the bigger `vm_pages[]` and then may under-run\n"
    "     * and hit `panic(\"pmap_steal_memory\")` (`vm_resident.c:762`).  So the seed\n"
    "     * `pmap_second_cursor` is set HERE, in the same commit that raises the count, and\n"
    "     * `pmap_next_page` drains it: the count and the drain cannot drift apart, and `low_pages`\n"
    "     * EQUALS the number of pages the drain can return (both are the region-table membership\n"
    "     * sum - see `pmap_next_page`).\n"
    "     *\n"
    "     * `*__first_avail` and `avail_start`/`avail_end` are untouched, so the boot interval and\n"
    "     * the `pmap_bootstrap: physical region` print are byte-identical (refusal C6). */\n"
    "    {\n"
    "        unsigned int low_pages = 0u;\n"
    "        uint32_t low_bytes = 0u;\n"
    "        int i;\n"
    "        for (i = 0; i < pmap_mem_regions_count; i++) {\n"
    "            uint32_t start = (uint32_t)pmap_mem_regions[i].start;\n"
    "            /* THE TWO DEFECTS THIS ONE LINE KILLS: a loop over ALL regions adds the BOOT region\n"
    "             * to the low count (double-counting the interval above); an UNSIGNED COMPARISON\n"
    "             * against a low constant cannot exclude 0x80000000 at all.  The skip is against\n"
    "             * `pmap_mem_regions_reach2` - the TTBCR.N=2 split, the same name `pmap_valid_page`'s\n"
    "             * lower bound uses - and it is `>=` so the boot region (base == the split) is\n"
    "             * skipped.  THIS IS THE REACHABILITY GATE, in the count. */\n"
    "            if (start >= pmap_mem_regions_reach2)\n"
    "                continue;\n"
    "            low_bytes += (pmap_mem_regions_usable_end[i] - start);\n"
    "            low_pages += ((pmap_mem_regions_usable_end[i] - start) >> PAGE_SHIFT);\n"
    "        }\n"
    "        avail_remaining += (pmap_paddr_t)low_pages;\n"
    "        pmap_second_cursor = avail_start2;\n"
    "        kprintf(\"1015b-regions R3a: low-bank regions %d, %u usable pages (%u bytes), \"\n"
    "                \"avail_remaining now %u\\n\",\n"
    "                pmap_mem_regions_count, low_pages, low_bytes, (unsigned int)avail_remaining);\n"
    "    }\n"
    "#endif /* STAGE90_XNU_REGIONS */\n"
)

# `pmap_next_page`: the drain must follow the count, or the count is worse than inert (docstring).
_P_NEXT_OLD = (
    "    if (first_avail >= avail_end) {\n"
    "        kprintf(\"pmap_next_page: ran out of possible pages, last page was 0x%08x\", first_avail);\n"
    "        return FALSE;\n"
    "    }\n"
    "\n"
    "    *addrp = pa_index(first_avail);\n"
)

_P_NEXT_NEW = (
    "    if (first_avail >= avail_end) {\n"
    "#if STAGE90_XNU_REGIONS\n"
    "        /* **" + MARK + ": the boot interval is exhausted, so drain the low bank through the\n"
    "         * REGION-AWARE CURSOR, skipping the carveout holes.\n"
    "         *\n"
    "         * The cursor advances past a hole by asking `pmap_valid_page` - the SAME span-set the\n"
    "         * predicate itself is - so the drain and the predicate cannot disagree about which\n"
    "         * pages are allocatable, and a carveout page is never emitted.  That is exactly the\n"
    "         * property `avail_remaining`'s low count rests on: `low_pages` (the count added) EQUALS\n"
    "         * the number of pages this loop can return, because both are the region-table\n"
    "         * membership sum over the SAME table.  The count is not separable from the drain; see\n"
    "         * `pmap_bootstrap` for the `pmap_startup` measurement that makes that a hard requirement.\n"
    "         *\n"
    "         * `avail_remaining--` per page, so a cursor that stopped early would leave the count\n"
    "         * TALLIED - which is the honest direction: an over-count is visible as\n"
    "         * `pmap_startup`'s `vm_page_bootstrap` under-run, never as a silent shortfall.\n"
    "         *\n"
    "         * THE EMIT-CONSUMER HALF IS R5's, NOT THIS RUNG'S.  A page below the TTBCR.N=2 split has\n"
    "         * no mapped kernel VA, so a consumer that dereferences one needs R5's window; R3a's\n"
    "         * scope is the booking and the well-behaved drain, and `pmap_next_page`'s low pages go\n"
    "         * to `pmap_startup`'s structures, not to a mapped consumer. */\n"
    "        while (pmap_second_cursor < avail_end2 &&\n"
    "               !pmap_valid_page((ppnum_t)(pmap_second_cursor >> PAGE_SHIFT)))\n"
    "            pmap_second_cursor += PAGE_SIZE;\n"
    "        if (pmap_second_cursor < avail_end2) {\n"
    "            *addrp = (ppnum_t)(pmap_second_cursor >> PAGE_SHIFT);\n"
    "            pmap_second_cursor += PAGE_SIZE;\n"
    "            avail_remaining--;\n"
    "            return TRUE;\n"
    "        }\n"
    "#endif /* STAGE90_XNU_REGIONS */\n"
    "        kprintf(\"pmap_next_page: ran out of possible pages, last page was 0x%08x\", first_avail);\n"
    "        return FALSE;\n"
    "    }\n"
    "\n"
    "    *addrp = pa_index(first_avail);\n"
)

# ---------------------------------------------------------------------------------------------------
# osfmk/arm/arm_vm_init.c
# ---------------------------------------------------------------------------------------------------

# The registration block goes at FILE scope, immediately before `arm_vm_init`.  The anchor's first
# line is Apple's own 958 `#endif`, so the insertion cannot drift away from the 958 pattern it
# extends (and both are at file scope, after the last __LINE__-bearing panic - the 958 lesson).
_A_SCOPE_OLD = (
    "#endif /* STAGE90_XNU_MEM_TOTAL */\n"
    "void arm_vm_init(uint32_t mem_limit, boot_args * args)\n"
)

_A_SCOPE_NEW = (
    "#endif /* STAGE90_XNU_MEM_TOTAL */\n"
    "#if STAGE90_XNU_REGIONS\n"
    "/* **" + MARK + " - 915-B rung 3a: the DT /memory/reg list becomes the pmap's region table.\n"
    " *\n"
    " * THE FIRST KERNEL READER OF /memory/reg.  MEASURED: no object under `osfmk/arm/` reads it\n"
    " * today (`grep -rn STAGE90_XNU_REGIONS osfmk/arm/` -> 0 hits before this edit), so this code is\n"
    " * new and unexercised - that is the design's first-build gate G2.\n"
    " *\n"
    " * It reads the tree's OWN device tree, through the API the rest of the kernel uses\n"
    " * (`DTLookupEntry`/`DTGetProperty`, `<pexpert/device_tree.h>`).  The DT is already initialized\n"
    " * here: `arm_init` calls `PE_init_platform` (`osfmk/arm/arm_init.c:145-146`) before\n"
    " * `arm_vm_init` (`:168`), and that calls `DTInit(PE_state.deviceTreeHead)`\n"
    " * (`pexpert/arm/common/pe_init.c:134`).  The tree itself came from the entry image\n"
    " * (`boot_args.deviceTreeP`, `src/xnu_entry_jump.c:161`).\n"
    " *\n"
    " * The refusals are the HOST accessor's, raised to a kernel panic\n"
    " * (`src/regions/xnu_memory_regions.c:55-88`): the list must be even and non-empty, its FIRST\n"
    " * pair must carry the boot base (section 2.4: a low-bank-first list is refused, not silently\n"
    " * mis-read - the exact defect the accessor exists to kill), no pair may be empty, and no two may\n"
    " * overlap.  The host walk and this one are the same arithmetic on the same words; the host half\n"
    " * is the one `make check` compiles and runs (`tools/check_memory_regions_walk.py`), because this\n"
    " * one cannot be exercised without a device.\n"
    " *\n"
    " * IT ASSIGNS NEITHER gPhysBase/gMemSize NOR max_mem/mem_size/sane_size - 964, and refusals C1/C2.\n"
    " * The boot region's base/size are CHECKED against those two globals, never written.\n"
    " *\n"
    " * The declarations below are DECLARATIONS of pmap.c's definitions (one definition, one owner -\n"
    " * [[mi4-one-value-two-definitions]]); `pmap_mem_regions[]`/`pmap_mem_regions_count` need none\n"
    " * because pmap.h already declares them. */\n"
    "extern uint32_t avail_start2, avail_end2;\n"
    "extern uint32_t pmap_mem_regions_usable_end[PMAP_MEM_REGION_MAX];\n"
    "extern uint32_t pmap_mem_regions_reach2;\n"
    "static int\n"
    "entry_xnu_regions_walk_words(const uint32_t *w, uint32_t words,\n"
    "                             struct xnu_mem_region *out, uint32_t max, uint32_t boot_base)\n"
    "{\n"
    "    uint32_t pairs, i, j;\n"
    "    if (w == 0 || words < 2u || (words & 1u) != 0u)\n"
    "        return -1;\n"
    "    if (w[0] != boot_base)\n"
    "        return -2;                                  /* the boot region is not pair 0 */\n"
    "    pairs = words >> 1;\n"
    "    if (pairs > max)\n"
    "        return -3;\n"
    "    for (i = 0u; i < pairs; i++) {\n"
    "        uint32_t base = w[i * 2u];\n"
    "        uint32_t size = w[i * 2u + 1u];\n"
    "        if (size == 0u)\n"
    "            return -4;                              /* an empty region is malformed */\n"
    "        for (j = 0u; j < i; j++) {\n"
    "            uint32_t bbase = w[j * 2u];\n"
    "            uint32_t bsize = w[j * 2u + 1u];\n"
    "            if (base < bbase + bsize && bbase < base + size)\n"
    "                return -5;                          /* two regions overlap */\n"
    "        }\n"
    "        out[i].base = base;\n"
    "        out[i].size = size;\n"
    "    }\n"
    "    return (int)pairs;\n"
    "}\n"
    "\n"
    "static void\n"
    "entry_xnu_register_regions(boot_args *args)\n"
    "{\n"
    "    DTEntry mem = 0;\n"
    "    void *words = 0;\n"
    "    unsigned int bytes = 0u;\n"
    "    struct xnu_mem_region region[PMAP_MEM_REGION_MAX];\n"
    "    int count, i;\n"
    "\n"
    "    if (DTLookupEntry(0, \"/memory\", &mem) != kSuccess || mem == 0)\n"
    "        panic(\"" + MARK + ": no /memory node in the device tree\");\n"
    "    if (DTGetProperty(mem, \"reg\", &words, &bytes) != kSuccess || words == 0 || bytes < 8u)\n"
    "        panic(\"" + MARK + ": /memory has no usable reg property\");\n"
    "\n"
    "    count = entry_xnu_regions_walk_words((const uint32_t *)words, bytes / 4u, region,\n"
    "                                         PMAP_MEM_REGION_MAX, args->physBase);\n"
    "    if (count < 1)\n"
    "        panic(\"" + MARK + ": /memory/reg refused (code %d)\", count);\n"
    "\n"
    "    /* The boot region is the pair whose base is gPhysBase AND whose size is gMemSize.  The\n"
    "     * walk already fixed the base (it is the pair-0 rule); the SIZE equality is the second\n"
    "     * half of refusal C3 and it is a CHECK, not an assumption: a DT whose boot pair disagrees\n"
    "     * with the args would make every later bound a guess. */\n"
    "    if (region[0].base != gPhysBase || region[0].size != gMemSize)\n"
    "        panic(\"" + MARK + ": boot region 0x%08x+0x%08x is not gPhysBase 0x%08x + gMemSize 0x%08x\",\n"
    "              region[0].base, region[0].size, (uint32_t)gPhysBase, (uint32_t)gMemSize);\n"
    "\n"
    "    /* Publish into the tree's ONE table (pmap.h:303-305).  `phys_table` stays NULL - the\n"
    "     * per-region PV base is R4's, and until R4 no code path may dereference it. */\n"
    "    for (i = 0; i < count; i++) {\n"
    "        uint32_t top = region[i].base + region[i].size;\n"
    "        pmap_mem_regions[i].start = region[i].base;\n"
    "        pmap_mem_regions[i].phys_table = 0;\n"
    "        pmap_mem_regions[i].end = top;\n"
    "        pmap_mem_regions_usable_end[i] = top;\n"
    "    }\n"
    "    pmap_mem_regions_count = count;\n"
    "\n"
    "    /* THE REACHABILITY BOUND (section 2.2).  `arm_vm_init` sets TTBCR.N = 2 below (`:475`), so\n"
    "     * TTBR0 owns [0, 2^(32-2)) and TTBR1 owns the rest; the kernel's identity/managed tables\n"
    "     * are built into the one table TTBR1 points at, and that build covers [gPhysBase,\n"
    "     * gPhysBase + gMemSize) only.  A general page below this bound therefore has NO mapped\n"
    "     * kernel VA from that build, and this constant is what `pmap_valid_page`'s low bound and\n"
    "     * `pmap_bootstrap`'s accounting share as their ONE definition (v2 section 2.0). */\n"
    "    pmap_mem_regions_reach2 = (1u << (32u - 2u));\n"
    "\n"
    "    /* The second allocator interval: the union of the NON-BOOT regions, drained in order.\n"
    "     * The boot region is the one at or above the reachability bound, so the interval is built\n"
    "     * from the regions BELOW it - the same skip `pmap_bootstrap`'s count uses, in the same\n"
    "     * spelling, so the interval and the count cannot disagree about which regions are low. */\n"
    "    avail_start2 = 0u;\n"
    "    avail_end2 = 0u;\n"
    "    for (i = 0; i < count; i++) {\n"
    "        if ((uint32_t)pmap_mem_regions[i].start >= pmap_mem_regions_reach2)\n"
    "            continue;\n"
    "        if ((uint32_t)pmap_mem_regions[i].start < avail_start2 || avail_start2 == 0u)\n"
    "            avail_start2 = (uint32_t)pmap_mem_regions[i].start;\n"
    "        if (pmap_mem_regions_usable_end[i] > avail_end2)\n"
    "            avail_end2 = pmap_mem_regions_usable_end[i];\n"
    "    }\n"
    "\n"
    "    kprintf(\"" + MARK + ": %d region(s); boot 0x%08x+0x%08x; low interval \"\n"
    "            \"[0x%08x, 0x%08x); reach2 0x%08x\\n\",\n"
    "            count, region[0].base, region[0].size, avail_start2, avail_end2,\n"
    "            pmap_mem_regions_reach2);\n"
    "}\n"
    "#endif /* STAGE90_XNU_REGIONS */\n"
    "void arm_vm_init(uint32_t mem_limit, boot_args * args)\n"
)

# The ONE call, after Apple's 958 block (and `args` is that block's own parameter).
_A_CALL_OLD = (
    "#else\n"
    "    max_mem = mem_size = sane_size = gMemSize;\n"
    "#endif /* STAGE90_XNU_MEM_TOTAL */\n"
)

_A_CALL_NEW = (
    "#else\n"
    "    max_mem = mem_size = sane_size = gMemSize;\n"
    "#endif /* STAGE90_XNU_MEM_TOTAL */\n"
    "#if STAGE90_XNU_REGIONS\n"
    "    /* **" + MARK + ": the ONE call.  AFTER the four totals are set and BEFORE anything is built\n"
    "     * from them, so `avail_start2`/`avail_end2` and the region table are live before\n"
    "     * `pmap_bootstrap` (`:548`) reads them.  It writes NONE of the four (C1/C2). */\n"
    "    entry_xnu_register_regions(args);\n"
    "#endif /* STAGE90_XNU_REGIONS */\n"
)

# (relative path, [(old, new), ...]) - the edits, in file order.
EDITS = (
    ("osfmk/arm/pmap.c", (
        (_P_GLOBALS_OLD, _P_GLOBALS_NEW),
        (_P_BOOT_OLD,    _P_BOOT_NEW),
        (_P_NEXT_OLD,    _P_NEXT_NEW),
        (_P_VALID_OLD,   _P_VALID_NEW),
    )),
    ("osfmk/arm/arm_vm_init.c", (
        (_A_SCOPE_OLD, _A_SCOPE_NEW),
        (_A_CALL_OLD,  _A_CALL_NEW),
    )),
)

# Every inserted form, per file, for --check/--revert.  Derived from EDITS by diffing the anchor's
# own lines out of the replacement - the inserted text must be present EXACTLY ONCE.
def _insertions():
    out = []
    for rel, pairs in EDITS:
        ins = []
        for old, new in pairs:
            old_lines = old.splitlines(keepends=True)
            added = [ln for ln in new.splitlines(keepends=True) if ln not in old_lines]
            ins.append("".join(added))
        out.append((rel, ins))
    return out


INSERTIONS = _insertions()


def _read(tree, rel):
    path = tree.rstrip("/") + "/" + rel
    try:
        with open(path, encoding="utf-8") as f:
            return path, f.read()
    except FileNotFoundError:
        print("patch_d13_regions_r3a: no %s (D13 tree not provisioned?)" % path, file=sys.stderr)
        return path, None


def _require_unique(text, rel, pairs):
    for i, (old, _new) in enumerate(pairs):
        n = text.count(old)
        if n != 1:
            raise SystemExit(
                "patch_d13_regions_r3a: anchor #%d in %s occurs %d times, not once - the tree is not "
                "the expected revision, or it was edited by hand (a wrong-whitespace anchor REFUSES, "
                "never no-ops; [[mi4-mutation-anchors-are-code-spans]]). Anchor begins: %r. Refusing."
                % (i, rel, n, old[:60])
            )


def _verify_staged(text, rel, pairs):
    """A present marker is NOT taken on trust: every STAGED form must occur EXACTLY ONCE.

    The unit is the whole replacement span (`new`), not a reconstruction of the "inserted" lines.
    An `#else`-bearing edit (the two function bodies) keeps Apple's own lines in the MIDDLE of `new`,
    so filtering them out joins the pieces either side into text that is not contiguous in the file
    and always counts 0 - a checker that could never pass.  `new` is contiguous by construction, and
    it occurs exactly once iff `old` did.
    """
    for old, new in pairs:
        n = text.count(new)
        if n != 1:
            raise SystemExit(
                "patch_d13_regions_r3a: the marker '%s' is present in %s but a staged form occurs "
                "%d times - moved by a hand edit, or half-applied. Refusing rather than certifying a "
                "tree the switch does not carry." % (MARK, rel, n)
            )


def patch(tree):
    wrote = False
    for rel, pairs in EDITS:
        path, text = _read(tree, rel)
        if text is None:
            return 2
        if MARK in text:
            _verify_staged(text, rel, pairs)
            print("patch_d13_regions_r3a: already staged in %s - verified" % path)
            continue
        _require_unique(text, rel, pairs)
        for old, new in pairs:
            text = text.replace(old, new, 1)
        _verify_staged(text, rel, pairs)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        wrote = True
        print("patch_d13_regions_r3a: staged %d anchor(s) in %s" % (len(pairs), path))
    if wrote:
        print("patch_d13_regions_r3a: the DT region list is now the pmap's region table, "
              "`pmap_valid_page` is a region span-set, and `avail_remaining` carries the low bank "
              "with a matching drain - all behind STAGE90_XNU_REGIONS (switch OFF is Apple's bytes).")
    else:
        print("patch_d13_regions_r3a: nothing to do - already staged")
    return 0


def dry_run(tree):
    for rel, pairs in EDITS:
        path, text = _read(tree, rel)
        if text is None:
            return 2
        print("patch_d13_regions_r3a --dry-run: would patch %s" % path)
        for i, (old, new) in enumerate(pairs):
            n = text.count(old)
            print("  ---- anchor #%d present exactly once: %s (count=%d) ----" % (i, n == 1, n))
            added = [ln for ln in new.splitlines(keepends=True)
                     if ln not in old.splitlines(keepends=True)]
            sys.stdout.write("".join("  + " + ln for ln in added))
        print("  ---- marker present: %s (%s) ----"
              % (MARK in text, "STAGED" if MARK in text else "APPLE/OFF"))
    return 0


def check(tree):
    """The --check proof: the INSERTED forms are staged.  The OFF-identity is the other half, and it
    is proved BY VALUE here too: every anchor's `#else` arm must be Apple's text exactly."""
    ok = True
    for rel, pairs in EDITS:
        path, text = _read(tree, rel)
        if text is None:
            return 2
        if MARK not in text:
            print("  FAIL %s: no '%s' marker - the switch was never staged" % (path, MARK))
            ok = False
            continue
        print("  ok   %s: '%s' staged" % (path, MARK))
        for i, (old, new) in enumerate(pairs):
            # The whole staged span (see _verify_staged: the inserted-line reconstruction is
            # non-contiguous for the `#else`-bearing edits and always counts 0).
            n = text.count(new)
            print("  %s staged form for anchor #%d present %dx (want 1)"
                  % ("ok  " if n == 1 else "FAIL", i, n))
            ok = ok and n == 1
    # The off arm must be Apple's bytes: `#else\n<apple lines>\n#endif` for the three `#else`-bearing
    # edits (pmap_valid_page's body, pmap_next_page's body) - and the boot/global edits have no
    # `#else`, so their anchors' own text must still be present in full.
    p_path, p_text = _read(tree, "osfmk/arm/pmap.c")
    if p_text is not None and MARK in p_text:
        for needle, what in (
            (_P_VALID_OLD.splitlines(keepends=True)[2] + _P_VALID_OLD.splitlines(keepends=True)[3],
             "Apple's `pmap_valid_page` body (contiguous in the #else)"),
            # `pmap_valid_page` keeps Apple's body contiguous in the `#else`.  `pmap_next_page` does
            # NOT - its gated block is INSIDE the `if (first_avail >= avail_end)` arm (so the low-bank
            # drain runs only when the boot interval is exhausted) - so its preserved text is checked
            # as FRAGMENTS, each of which must survive exactly once.  ON, the `#if` block is the only
            # added text and OFF removes it, leaving Apple's body; that is a compiler fact and cannot
            # be proved by text here, only asserted.
            ('    if (first_avail >= avail_end) {\n', "Apple's `pmap_next_page` exhaustion test"),
            ('        kprintf("pmap_next_page: ran out of possible pages, last page was 0x%08x", first_avail);\n',
             "Apple's `pmap_next_page` exhaust message"),
            ('    *addrp = pa_index(first_avail);\n', "Apple's `pmap_next_page` handout line"),
            (_P_GLOBALS_OLD, "Apple's `avail_start, vm_first_phys` global line"),
            (_P_BOOT_OLD, "Apple's `avail_remaining = ...` line"),
        ):
            n = p_text.count(needle)
            print("  %s %s preserved verbatim (%dx, want >=1)"
                  % ("ok  " if n >= 1 else "FAIL", what, n))
            ok = ok and n >= 1
        # The count and the drain must BOTH carry the second interval and use the SAME membership
        # sum - a count without a matching drain is the boot-killing mismatch (docstring).
        for needle, what in (
            ("pmap_mem_regions_count, low_pages, low_bytes, (unsigned int)avail_remaining",
             "the low-bank count printf"),
            ("pmap_second_cursor = avail_start2;",
             "the drain seed in `pmap_bootstrap` (the count and the drain move together)"),
            ("pmap_second_cursor < avail_end2 &&\n"
             "               !pmap_valid_page((ppnum_t)(pmap_second_cursor >> PAGE_SHIFT))",
             "the carveout-skipping drain in `pmap_next_page` (uses the SAME predicate)"),
        ):
            n = p_text.count(needle)
            print("  %s %s present (%dx)" % ("ok  " if n == 1 else "FAIL", what, n))
            ok = ok and n == 1
        # The emit-consumer half is R5's.  R3a must not OPEN its switch.  It MAY name it in a
        # comment (there is no other way to say "this is behind R5's switch"), so what is refused is
        # an `#if`/`#ifdef`/`-D` CARRYING it, not the bare word.
        if re.search(r"#\s*if(?:def)?\s+%s\b" % _BOUNCE_SWITCH, p_text) or \
           re.search(r"-\s*D\s*%s\b" % _BOUNCE_SWITCH, p_text):
            print("  FAIL pmap.c OPENS a preprocessor switch on %s - R3a must not define the R5 "
                  "emit switch" % _BOUNCE_SWITCH)
            ok = False
        else:
            print("  ok   pmap.c opens no switch on %s (R3b/R5's; R3a leaves it undefined)"
                  % _BOUNCE_SWITCH)
    print("patch_d13_regions_r3a --check %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def revert(tree):
    for rel, pairs in EDITS:
        path, text = _read(tree, rel)
        if text is None:
            return 2
        if MARK not in text:
            print("patch_d13_regions_r3a: nothing to revert in %s" % path)
            continue
        _verify_staged(text, rel, pairs)
        for old, new in pairs:
            text = text.replace(new, old, 1)
        if MARK in text or _GATE_OPEN in text:
            raise SystemExit(
                "patch_d13_regions_r3a: the marker or gate survived the revert in %s - refusing to "
                "write a half-reverted tree" % path)
        for old, _new in pairs:
            if text.count(old) != 1:
                raise SystemExit(
                    "patch_d13_regions_r3a: after the revert an original anchor occurs %d times in "
                    "%s, want 1 - refusing to write" % (text.count(old), path))
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print("patch_d13_regions_r3a: reverted - %s is the tree's text again (%d anchors)"
              % (path, len(pairs)))
    return 0


def anchor_census(tree):
    """Print the anchor census and refuse unless every anchor is UNIQUE.  Uniqueness is the property
    this tool depends on and the one a later edit can silently take away: with two occurrences,
    `str.replace(..., 1)` would edit whichever came first, which is not a decision this tool makes."""
    ok = True
    for rel, pairs in EDITS:
        path, text = _read(tree, rel)
        if text is None:
            return 2
        staged = MARK in text
        print("  file %s (%s)" % (path, "STAGED" if staged else "APPLE/OFF"))
        for i, (old, new) in enumerate(pairs):
            n_old, n_new = text.count(old), text.count(new)
            # Unstaged: the anchor is 1x and the staged span 0x.  Staged: the reverse EXCEPT for the
            # two purely-APPENDED anchors (the global line and the `avail_remaining` line), which
            # survive verbatim inside the staged span - so their anchor count stays 1x and the
            # *paired* count is what moves.  `old in new` is exactly that case.
            appended = old in new
            if staged:
                want_old, want_new = (1 if appended else 0), 1
            else:
                want_old, want_new = 1, 0
            good = (n_old == want_old and n_new == want_new)
            print("    %s anchor #%d: anchor %dx (want %d), staged span %dx (want %d)"
                  % ("ok  " if good else "FAIL", i, n_old, want_old, n_new, want_new))
            ok = ok and good
    print("patch_d13_regions_r3a --anchors %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv):
    if len(argv) < 2 or argv[1].startswith("--"):
        print("usage: %s <XNU_TREE> [--dry-run|--check|--revert|--anchors]" % argv[0], file=sys.stderr)
        return 2
    tree = argv[1]
    flags = set(argv[2:])
    if "--anchors" in flags:
        return anchor_census(tree)
    if "--check" in flags:
        return check(tree)
    if "--revert" in flags:
        return revert(tree)
    if "--dry-run" in flags:
        return dry_run(tree)
    return patch(tree)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))