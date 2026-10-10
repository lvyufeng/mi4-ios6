# Experiment 978 — 915-B rung 0 (HOST HALF): the one `/memory/reg` region accessor, host-tested

**Status:** ✅ **BUILT (HOST-ONLY), VERIFIED, PUSHED (`43ece84`).** `src/xnu_memory_regions.c` is the
single walk of the DT `/memory/reg` region list, kept **PURE** (`<stdint.h>` only) so
`tools/check_memory_regions_walk.py` compiles it with the **host** compiler and runs it against the
exact word arrays the payload emits — no target compiler, **no device**. It is deliberately **not** in
`scripts/build.sh` SOURCES, so the shipped press arm `armed-d13-d693864f` stays **byte-frozen** (the
check refuses the module appearing in build.sh). `make check` rc 0. **This is the host-testable half
of rung 0, not the whole rung** (see §4).

Follows `experiment-915b-the-region-port-v2.md` §4 **rung 0** and §2.4/§6.8; builds on 976 (rung 1).
**PRESS IS THE OPERATOR'S.**

---

## 1. What rung 0's host half is

915-B v2 §2.4 makes the DT `/memory/reg` a **REGION LIST** — the boot pair first, additional banks
appended (976's rung 1 appends the low bank `[0, 0x60000000)` as words 2/3). Today **three readers**
each hard-code *"pair 0 is the boot region"*:

```
src/pe_state.c:38-39                    reg_value(…, memory, "reg", 0 / 1, …)
src/pexpert.c:68                        mem_reg[0] == RAM_PHYS_BASE && mem_reg[1] == args->memSize
src/xnu_pe_init_platform_false.c:422-423 stage90_pe_init_reg_word(…, memory, "reg", 0u / 1u, …)
```

That convention is one **the data can violate**: a list that put the low bank first would read as the
boot region, and the reader would carry a wrong base with no refusal. v2 §6.8's fix is a **SINGLE
accessor** whose walk is a **BASE-MATCH** — the boot region is the pair whose base equals the boot
base, and it **MUST be pair 0** — so the convention becomes a **checked invariant**.

`src/xnu_memory_regions.c` is that accessor:

```c
uint32_t xnu_memory_regions_walk(const uint32_t *reg_words, uint32_t word_count, uint32_t boot_base,
                                 struct xnu_mem_region *regions, uint32_t max_regions);
```

It returns the region (pair) count, or **0** on any refusal: a list shorter than one pair, an **odd**
word count, a **pair-0 base that is not `boot_base`** (a low-bank-first list — refused, not silently
mis-read), or an **empty later region**. `regions == NULL` is a pure count.

## 2. Why it is a PURE module

The walk is arithmetic over an array of host-order words. It needs no DT header, no XNU type, no
device. Keeping it pure (only its own header, which pulls `<stdint.h>`) is what lets the **host**
compiler build it — so the refusal logic is proven on the host, before the region-registration rung
wires it into the payload. `#if STAGE90_XNU_REGIONS`, never `#ifdef` ([[mi4-off-option-two-spellings]]).

## 3. By-value verification

- `tools/check_memory_regions_walk.py` (in `make check`) compiles `src/xnu_memory_regions.c` +
  `tools/test_memory_regions_walk.c` with the host `cc` at `-Wall -Wextra -Werror` and **runs** it.
  Cases: **hold** the boot pair at index 0 for the shipped DT `{0x80000000, 0x5e500000}` and 976's
  rung-1 DT `{boot, low}`; **refuse** a low-bank-first list, an odd list, an empty list, an empty
  later region, and a pair-0 base ≠ the boot base.
- Structural facts the host run cannot see: gating `#if`; the module/header include **only**
  `<stdint.h>`/own header (purity); and the module is **not** in build.sh SOURCES (the arm stays
  byte-frozen). `--selftest` mutates the gate and the build.sh wiring and asserts each refusal.
- `make check` **rc 0**; `verify_press_ready.sh` confirms the live `out/stage90` is still 12/12
  byte-identical to the frozen park `armed-d13-d693864f` — the payload was **never rebuilt**.

## 4. Honest scope — this is HALF of rung 0

915-B v2 §4 rung 0 is *"region type + accessor + `pmap_mem_regions[]` population + the L1-slot-walk
refusal + the DT-shape refusals."* **Built here:** the region type (`struct xnu_mem_region`) and the
accessor, with the DT-shape refusals folded into the walk. **NOT built:** the population of the
kernel's `pmap_mem_regions[]` (whose element type is the tree's `mem_region_t {start, phys_table,
end}`, `pmap.h:299` — a **different** type from the DT-derived `xnu_mem_region {base, size}`; the
registration rung converts one to the other and must NOT create a second region table, refusal §6.2),
and the **L1-slot-walk refusal** for the bounce window. Those need the vendored tree and
`arm_vm_init.c`, and are the registration rung's job.

**Why not build the rest now.** The live `out/stage90` **is** the verified press arm; any change that
enters `scripts/build.sh` rebuilds the payload and can break the one artifact the press needs, in a
loop where the press is the only step that advances the goal. The host-only module adds a real,
verifiable deliverable **without touching a byte** of the payload — which is why it was safe to build
while the device is absent.

*Provenance: `src/xnu_memory_regions.c/.h`, `tools/test_memory_regions_walk.c`,
`tools/check_memory_regions_walk.py`, `Makefile` (new `make check` stanza) — all new this session;
`external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.h:299-305` (`mem_region_t`, `pmap_mem_regions[]`),
`src/pe_state.c`, `src/pexpert.c`, `src/xnu_pe_init_platform_false.c`, `scripts/build.sh` read by
value. Follows [[mi4-915b-region-port-v2-design]], [[mi4-976-regions-rung1-inert]].*