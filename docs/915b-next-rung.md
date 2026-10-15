# 915-B — the NEXT rung: the second allocator interval (rung 3)

**Status:** 📐 **DESIGNED, host-side, NOT built.** The device is DARK (operator press). This document is
host-side design work only — no build, no device action was taken. It establishes the *current* rung by
measurement over the tree, then designs the next owed rung concretely. Read with
`docs/experiments/experiment-915b-the-region-port-v2.md` (§4 forced rung order), which this supersedes
only where it is measurably stale.

**PRESS IS THE OPERATOR'S.**

---

## 1. What is BUILT, by measurement (not by memory)

The task premise — "rung 2 = the three carved spans appended" is *proposed, not implemented — verify" —
is **false as of `a3b0ec2`**. Rung 2 **is** implemented. The tree is ahead of the 915-B v2 §7 "NOT built"
status (that doc is 2026-10-10; two commits landed 2026-10-11).

| rung (v2 §4) | what | state | evidence (read by value this session) |
|---|---|---|---|
| **R0** region type + accessor | host half of rung 0 | ✅ BUILT | `src/regions/xnu_memory_regions.c` (+ `.h`); `tools/check_memory_regions_walk.py` compiles it on the HOST and runs the refusals. Byte-frozen off the payload (not in `scripts/build.sh` SOURCES — `check_memory_regions_walk.py:67-70` refuses it there). Commit `e229c14`/`43ece84`, re-homed `0b95bee`. |
| **R1** append the low bank (inert) | reg[2]/[3] = `[0, 0x60000000)` | ✅ BUILT then **SUPERSEDED** | `experiment-976-…`. Off arm byte-neutral. R1's *whole* low bank is now replaced by R2's three spans. |
| **R2** carveout reconciliation → append the THREE spans | reg[2..7] = 3 spans | ✅ **BUILT** (contra the premise) | `src/stage90_main.c:64-79` (`#if STAGE90_XNU_REGIONS` arm = `{RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE, 0x00000000u,0x05a00000u, 0x0d200000u,0x02800000u, 0x0ff00000u,0x50100000u,}`; `_Static_assert == 8`). Spans derived BY VALUE from `out/stage90/device-reads/mi4-iomem-and-mem.txt:4-6` (`00000000-059fffff`, `0d200000-0f9fffff`, `0ff00000-5fffffff`), provenance carried in the source and enforced by `check_d13_regions.py`. Commits `4ea04a9`, `08b4397`. Still **provably inert** (4 readers read pair 0; `check_d13_regions.py` fact 4 refuses a reader reaching word ≥ 2). |
| **R2.5** (un-numbered, "L3.1") thread `STAGE90_XNU_REGIONS` into the KERNEL corpus | so kernel `#if STAGE90_XNU_REGIONS` is a defined 0, not undefined | ✅ BUILT | `tools/build_xnu_arm_kernel.sh:225-259` (`case 0\|1`, else refuse; always defines; post-append array assert). Commit `c2f6e98`. **Necessary but not sufficient** — its own commit says "the rungs 3a-5 reader code in the XNU tree is still owed." |
| **R3** predicates + SECOND allocator interval | `[avail_start2, avail_end2)` | ⬜ **OWED — this rung** | Nothing in `external/xnu-hd2-darwin13/xnu/osfmk/arm/` reads `STAGE90_XNU_REGIONS` (grep: 0 hits), defines `pmap_mem_regions[]`, or names `avail_start2`/`avail_end2` (grep: 0 hits). `pmap_next_page` is single-interval (`pmap.c:2072-2086`). |
| **R4** per-region PV/`pai_to_pvh`/lock tables + pmap-object delta | | ⬜ OWED | `pai_to_pvh` `pmap.c:497`, `lock_pvh_pai` `:518`, sizing `pmap_init` `:3593-3617`, `_vm_object_allocate(mem_size,…)` `:3729`. |
| **R5** bounce window + visualizer second field | | ⬜ OWED | No `BOUNCE` symbol anywhere in the tree (grep: 0 hits). `ENTRY_WINDOW` is still a single field (`build_entry.sh:1987` `ENTRY_ARM_KEYS`). |
| **R6** first press | | ⬜ OPERATOR'S | |

**R2 is not inert *by construction* any more than R1 was — it is inert *by the four readers not reading
words ≥ 2*, and that is a check, not a claim.** So the current frontier is: **the region list is in the
DT (R1+R2), the switch reaches the kernel (R2.5); the kernel still reads NONE of it. R3 is the first rung
that makes the kernel act on the list.**

**Two definition-guards are already structurally in place and must be cited, not re-derived:**
- `tools/check_d13_memory_total.py` **fact 5** (`:152-167`, in `make check` at `Makefile:270-271`):
  every assignment *statement* to `mem_size`/`sane_size` in D13 `arm_vm_init.c` must have terminal RHS
  exactly `gMemSize`. A port that follows the stale 915 §5 row (`mem_size = sane_size = <sum>`) is
  **refused at make check**. `max_mem` keeps its ONE writer (958's `PE_get_default("hw.memsize", …)`,
  `arm_vm_init.c:337-341`). **R3 must not touch any of the four.**

---

## 2. The next rung — R3, the second allocator interval

### 2.0 The ONE question R3 answers

> **Does a region-aware single-interval allocator, given the DT-declared low-bank spans, produce a
> coherent low-bank free-page COUNT without disturbing the boot interval?**

R3 does **not** ask "can a consumer use a low page" — that is R5. It asks whether the *bookkeeping*
(validity predicate, second interval, free-page accounting) can carry the low bank at all. The v2 §4
rung-3 note already scopes the observable this way: *"rung 3 is pressable only for a scoped observation
of `avail_remaining` counting the low bank, with a refusal (§6.5) naming the missing window for any
consumer path."* R3's honest deliverable is the *count*, and the emit path is refused until R5.

### 2.1 The edit sites (exact anchors, D13 tree `external/xnu-hd2-darwin13/xnu/`)

**(a) The registration — `osfmk/arm/arm_vm_init.c`, after `:347`.** A new `#if STAGE90_XNU_REGIONS`
block (byte-neutral off, exactly 958's `STAGE90_XNU_MEM_TOTAL` pattern at `:310-347`, placed *after the
last `panic`* so `.rodata.str1.1` does not shift — the 958 lesson):

- walk the DT `/memory/reg` (already handed to XNU at `src/xnu_entry_jump.c:161`), reusing the **R0
  accessor's** logic (base-match, `boot_base = args->physBase`; `xnu_memory_regions_walk`'s refusals:
  boot-pair-first, disallow odd/empty/overlap — `src/regions/xnu_memory_regions.c:55-88`);
- build `region[]` with each region's reachable top `REGION_MAX(i) = min(base+size, RAM_CONSOLE_BASE)`
  (the v2 §2.0 two-name fix — never conflate `region[i].size` with the clamp);
- assert the **boot region** is the pair with `base == gPhysBase` and `size == gMemSize`
  (`arm_vm_init.c:331-333`); refuse otherwise (§3 refusal C3);
- publish `region[]`/count into **`pmap_mem_regions[]`/`pmap_mem_regions_count`** — the tree's ONE
  canonical table, **already declared** at `osfmk/arm/pmap.h:303-305` and never
  defined/referenced in `osfmk/arm/` today (grep: only the declaration + arm64 stubs.s). R3 becomes its
  **single definition** (v2 §2.3, refusal §6.2);
- assign the **second allocator interval** `avail_start2`/`avail_end2` from the *non-boot* regions, and
  **only** those globals plus `pmap_mem_regions[]`/count — **never** `max_mem`/`mem_size`/`sane_size`/
  `gPhysBase`/`gMemSize` (refusals C1/C2; `check_d13_memory_total.py` fact 5).

**(b) The predicates + second interval — `osfmk/arm/pmap.c`:**

| anchor | today (read by value) | R3 change |
|---|---|---|
| `pmap.c:2207-2211` `pmap_valid_page` | `((p<<PAGE_SHIFT) > avail_start) && ((p<<PAGE_SHIFT) < avail_end)` — ONE interval | TRUE iff the page is inside **any** registered region **and not inside a low-bank carveout span** (span-set membership, v2 §2.2/§2.2-rung-2). Also fixes the endpoints (`>= region.start`, `< REGION_MAX`). |
| `pmap.c:2072-2086` `pmap_next_page` | drains `[first_avail, avail_end)`; terminator `first_avail >= avail_end` | **drain the boot interval first, then `[avail_start2, avail_end2)`** — the emit half. **Gated:** see §2.4. R3-ships the interval + accounting; the second-interval *drain into consumers* is behind the R5 refusal. |
| `pmap.c:2106-2109` `pmap_free_pages` | `return avail_remaining;` | `avail_remaining` initialized at `pmap.c:2723` = `(avail_end - first_avail)>>PAGE_SHIFT`. R3 makes it `boot + low` when the second interval is present. This is **the deciding instrument** (§2.3). |
| `pmap.c:2003-2014` `pmap_zero_page` | panics unless inside `[avail_start, avail_end)`; `bzero(phys_to_virt(p<<PAGE_SHIFT))` | must use the **region-aware accessor** (R5's window), refused for a non-boot page until R5 (refusal C5). **R3 does not move this line's behaviour for a low page** — it only must not *emit* one yet. |
| `pmap.c:2291-2294` `pmap_copy_page` | `ovbcopy(phys_to_virt(src), phys_to_virt(dst))` | same — region-aware accessor, refused pre-R5 (C5). |
| `pmap.c:496-497` `pa_index`/`pai_to_pvh`; `:518-519` `lock_pvh_pai`/`unlock_pvh_pai` | `atop(pa)`; `pai - atop(gPhysBase)` — index off the boot base | **R4's job** (per-region PV table). R3 must NOT emit a low page into `pai_to_pvh` before R4 — refusal C4. |
| `pmap.c:2712-2727` `pmap_bootstrap` | prints `physical region 0x%08x - 0x%08x`, sets `avail_remaining` from ONE interval | keep the print **byte-identical for the boot region** (`avail_end == gPhysBase+gMemSize`, `arm_vm_init.c:467`); the low interval is registered *separately*, so the boot print does not move (refusal C6). |

**The `gPhysBase` underflow claim is FALSE and R3 must not act on it.** v2 §3.1 already corrected 964 §3
with the arithmetic (`phys_to_virt(p) = p` for `p < gPhysBase`, mod-2³² reduction at `pmap.h:193`). R3
does not need `phys_to_virt` for a low page at all in the count half.

### 2.2 The reachability gate R3 must honour (the measurement this design ADDS)

**A low page is NOT reached the way the region model assumed.** This session's measurement, by value:

- `arm_vm_init.c:475` sets `TTBCR.N = 2` (`set_mmu_ttbcr(2)`). Under ARM's rule that **TTBR0 owns
  `[0, 2^(32-N)) = [0, 0x40000000)`** and TTBR1 owns `[0x40000000, 4 GiB)`
  (`src/entry/entry_stubs.c:2324-2326`, `:2117` — "the hardware split is `0x40000000`, NOT the
  `0x80000000` address-space split").
- The kernel's identity/managed tables are built into **`cpu_ttb`**, the ONE table **TTBR1** points at
  (`arm_vm_init.c:388-391`, `set_mmu_ttb_alt(cpu_ttb)` `:474`; `entry_stubs.c:2331-2333`).
- **Every low-bank page `p ∈ [0, 0x60000000)` with `p < 0x40000000` is below the split →
  walked via TTBR0 (`TTBR0`), which is the USER page table, not `cpu_ttb`.** The registration block's
  `region[]`/`pmap_mem_regions[]` describe **PA**, and R3's *count* needs no VA — but the moment a low
  page below `0x40000000` is **handed to a consumer and dereferenced** (e.g. `vm_pages[i]` in
  `vm_page_init`, `vm_resident.c:1565`, or `bzero` in `pmap_zero_page`), it must be reachable at a
  **kernel** VA. The identity map at `l2_map_linear_range(identityCachePA, gPhysBase-sectionOffset,
  gPhysBase+gMemSize)` (`arm_vm_init.c:400`) covers `[0x80000000, 0x80000000+gMemSize)`, **not** the
  low bank below `0x40000000`.
- Therefore the emit half (a low page consumed by the VM subsystem) is **unsafe until R5's bounce
  window** maps it into a bounded managed VA — and R5's own gate is the visualizer second field
  (v2 §3.5), which does not exist (`ENTRY_ARM_KEYS` has no second field). This is the sharper, correct
  form of v2 §3.1's "no slot in the fixed `MANAGED_BASE` map" reason: it is not only *no slot*, it is
  the *wrong page table* for `p < 0x40000000`.

  **Honest caveat (measured, not assumed).** `src/entry/entry_smem.h:53-55` records that on this device
  the *entry* channel shows `xnu_live_ttbr0 == xnu_live_ttbr1`, and the two identity/managed maps are
  built into the one `cpu_ttb` that `set_mmu_ttb_alt` also points TTBR1 at (`arm_vm_init.c:388-391,473-474`).
  So whether the *kernel's* TTBR0 equals `cpu_ttb` at `arm_vm_init` time is **not established by value
  here** — it is checked only after `set_mmu_ttbcr(2)` in the entry channel, not in the kernel.
  **Do not lean on "TTBR0 is the user table" as proven.** The load-bearing, proven facts are: the
  kernel's identity/managed build covers `[gPhysBase, gPhysBase+gMemSize)` = `0x80000000+` only
  (`arm_vm_init.c:400`, `l2_cache_to_range` `:388-391`), and the low bank `[0, 0x40000000)` sits
  strictly below the TTBCR.N=2 split `0x40000000`, so a general low DRAM page `p` has **no mapped
  kernel VA from that build**. (Residual, flagged not solved: the kernel `bcopy`s the boot table
  `[gTopOfKernel] → cpu_ttb` (`arm_vm_init.c:365`), and `src/entry/entry_smem.c:38` installs a *single*
  low identity section at `0x0fa00000` — so exactly that one SMEM page may survive into the kernel
  table. That is 2 MiB of SMEM, not DRAM, and does not read general low-bank pages.) The R3b refusal (C4)
  rests on this, and R5 answers it with a window regardless of which table TTBR0 holds.

**Consequence for R3's scope:** R3 must be **R3a (the count half)**, buildable and pressable
independently; R3b (the drain-into-consumers half of `pmap_next_page`) is **gated on R5** and is
*refused at build* until the window exists (refusal C4). Attempting to emit a low page in R3 (as v2 §4
rung 3 as literally worded would) is the "a lower rung's side-effect poisoned the rung above"
defect class.

### 2.3 The PRE-REGISTERED by-value criterion (R3a)

**PASS** (all four, read by value; a non-return alone is neither pass nor fail —
`mi4-911-resident-nonreturn-is-not-a-wedge`):

1. `pmap_bootstrap: physical region 0x%08x - 0x%08x` is **byte-identical** to the R2.5-off arm's
   (boot interval unchanged), AND
2. a NEW console record names the low-bank free-page count, and its value **equals**
   `(sum of the three span sizes) / PAGE_SIZE` = `0x58300000 / 0x1000` = **`0x58300` pages** (1411 MiB);
   the constant is derived from the three DT spans, **not** a literal — refusing the hard-code, and
3. `pmap_free_pages()` (→ `avail_remaining`, `pmap.c:2106-2109`) equals
   `boot_pages + 0x58300`, where `boot_pages = (avail_end - first_avail) >> PAGE_SHIFT` as today, AND
4. **`vm_page_init`'s `#if DEBUG` guard does not fire** (`vm_resident.c:1560-1562`
   `panic("vm_page_init: non-DRAM phys_page …")`) — because no low page was emitted (R3a drains the
   boot interval only).

**FAIL** if: (1) the boot print moved; or (3) `avail_remaining` is unchanged (the interval was
registered but not wired into the accounting — the exact defect the v2 note warns about: *"without it
the diagnostic could be satisfied with ZERO low-bank pages"*); or (4) the `vm_page_init` panic fires
(a low page was emitted below the TTBCR split → the R3b/R5 ordering was violated).

**The single deciding measurement is (3): `avail_remaining` (exposed as `pmap_free_pages()` and the
`vm_page_bootstrap: N free pages` printf, `vm_resident.c:702`) rising by exactly the low-bank page
count.** If it rises by `0x58300`, the region-aware accounting works. If it does not, the interval is
inert. Nothing else in R3 moves this number.

### 2.4 Build refusals (each a CHECK that stops the build, not a comment)

Per this project's rule ([[mi4-a-claim-in-a-comment-is-not-a-check]]), each is a refused build, in a new
`tools/check_d13_regions_rung3.py` wired into `make check` beside `check_d13_regions.py`, with
`--selftest` mutating each fact. (Anchors are the D13 tree paths R3 edits.)

- **C1 — `max_mem`/`mem_size`/`sane_size` untouched.** Re-assert `check_d13_memory_total.py` fact 5
  over the post-R3 `arm_vm_init.c`: terminal RHS exactly `gMemSize`; `max_mem`'s single writer is the
  958 DT block. Cite, do not re-implement.
- **C2 — `gPhysBase`/`gMemSize` each have ONE assignment.** `arm_vm_init.c:331-333` unchanged; the R3
  block assigns **neither** (nor the four totals). Grep-refuse a second assignment.
- **C3 — boot region is the pair with `base == gPhysBase && size == gMemSize`.** A list whose pair-0
  base ≠ `args->physBase` fails the build (this is R0's accessor refusal, raised to a kernel refuse).
- **C4 — NO low page emitted before R5.** The R3 block must leave `pmap_next_page`'s second-interval
  drain **behind** `#if STAGE90_XNU_BOUNCE_WINDOW` (a switch that stays **undefined/0 until R5**).
  Refuse if `pmap_next_page` drains `avail_start2` while the window switch is off. This is the refusal
  that encodes §2.2's reachability gate.
- **C5 — NO raw `phys_to_virt` on a LOW page.** Negative check over `pmap.c`: `pmap_zero_page`
  (`:2013`), `pmap_copy_page` (`:2293`), `pmap_grab_page`, `pai_to_pvh`/`lock_pvh_pai` must name the
  region accessor before a low page can be their input; refuse a bare `phys_to_virt` reachable from a
  low `ppnum`. (This is v2 §6.5; R3 arms it, R4/R5 satisfy it.)
- **C6 — boot `pmap_bootstrap` print unchanged.** Refuse a build where `avail_end`/`first_avail` at
  `pmap.c:2723-2727` derive from anything but the boot interval.
- **C7 — ONE region table.** `mem_region_t` defined once; the only `mem_region_t[]` is
  `pmap_mem_regions[]` (`pmap.h:303-305`); refuse a second array. (`src/regions/xnu_memory_regions.c`'s
  `struct xnu_mem_region {base,size}` is the *host* DT-derived type — R3 converts it to
  `mem_region_t {start,phys_table,end}`; it must not become a second *kernel* table.)
- **C8 — the switch reaches the file R3 edits.** `build_xnu_arm_kernel.sh:243-259` already defines
  `STAGE90_XNU_REGIONS` 0/1 for the kernel corpus; refuse a build where `arm_vm_init.o`/`pmap.o` were
  compiled with the macro undefined.
- **C9 — the existing guards still pass.** `check_d13_managed_base.py` (`MANAGED_BASE == 0xC0000000`,
  `gMemSize = args->memSize`), `check_d13_vm_init_staged.sh`, `check_d13_regions.py`,
  `check_memory_regions_walk.py`.

### 2.5 NULL CONTROL for each instrument

Every new instrument needs a null control that would read the *same value if the mechanism were absent*:

| instrument (deciding reading) | null control | expected null value |
|---|---|---|
| low-bank page count record | build with `STAGE90_XNU_REGIONS=0` | the record key is **absent** (not `0`) — the 958/preflight one-way absent-key idiom (`preflight_boot_check.sh:965`); absence is the value |
| `avail_remaining` = `boot + 0x58300` | R2.5-off (or R3 off) | `avail_remaining == boot_pages` exactly (the today value); the delta of `0x58300` is the *effect* |
| `pmap_valid_page(low_ppnum)==TRUE` | probe a **carveout** ppnum inside a hole (`0x05a00000-0x0d1fffff`) | **FALSE** — a validity predicate that returns TRUE for a carveout page is the span-set defect, not the mechanism |
| boot `physical region` print | the same print in the R2.5-off arm | byte-identical string (the whole inertness claim) |
| the R3b drain is off | `STAGE90_XNU_BOUNCE_WINDOW` undefined | `pmap_next_page` never returns a `ppnum < 0x40000000` (assert it) |

### 2.6 Honest: what is UNMEASURED until the first build

- **`avail_remaining`'s exact boot value and the `0x58300` sum's interaction with the region clamp.**
  The three DT spans sum to `0x58300000` = 1411 MiB (arithmetic: `9*0x100000 + 0x2800000 + 0x50100000`,
  and 1411 + the 125 MiB carveout = 1536 = `0x60000000`). This is *measured from the capture file*, not
  from a build. The first build's console will print the real `vm_page_bootstrap: N free pages`; **that N
  is the measurement**, and the `0x58300` above is the *prediction* to be falsified, not a result.
- **Whether the DT list reaches `arm_vm_init` at all on the entry path** (the `apple_dt` handover at
  `xnu_entry_jump.c:161` is by value in the design; the kernel half reading `/memory/reg` is **new code
  R3 introduces** and is untested — no kernel object reads the DT `/memory/reg` today).
- **The `mem_region_t.phys_table` field.** R3 populates `pmap_mem_regions[]` but the element's
  `phys_table` (per-region PV base, `pmap.h:297`) is **R4's**; until R4 it must be NULL and R3 must
  refuse any code path that dereferences it. UNMEASURED whether leaving it NULL is inert for a
  count-only R3 — flagged as the R3→R4 seam risk.
- **No `STAGE90_XNU_BOUNCE_SIZE` number is invented here.** R3a does not need it (it is R5's open input,
  v2 §3.3 — genuinely unmeasurable from this device's DT, confirmed: no `reserved-memory` node, 964 §4).

### 2.7 The measurement-defect self-check

> Is any proposed clause unsatisfiable, or asserting a compiler artifact?

- **Clause (3) `avail_remaining == boot_pages + 0x58300`** is *satisfiable by construction*: R3 sets
  `avail_start2/avail_end2` from the same three spans whose sizes sum to `0x58300000`, so the added
  page count is `0x58300` **iff** the accounting reads the second interval. It cannot be satisfied
  without the mechanism and cannot fail with it. **It is not a tautology and not a compiler artifact** —
  it is a runtime console value. *(Contrast the just-burned `ASID==0 / full>=2` clause: that asked for a
  value a compiler/allocator could never produce in three scripts. Here the value is produced at run
  time by the code under test.)*
- **Clause (4)** deliberately checks the **`#if DEBUG` guard** at `vm_resident.c:1560`. **Defect caught:**
  if the build is non-DEBUG, that panic is compiled out and clause (4) is **vacuous** — an unsatisfiable
  *check* masquerading as a satisfied one. Mitigation: the R3a claim must **not** rest on (4); (4) is a
  secondary guard only meaningful on a DEBUG arm, and the primary criterion is (3). If the D13 build is
  non-DEBUG, (4) is struck, not silently passed.
- **Clause (2) `0x58300`** must be **computed from the spans**, not written as a literal — a literal is
  a number with no derivation (the `check_d13_regions.py` provenance rule). The check reads the same
  three spans the source carries.
- **No clause asserts `pmap_valid_page(low)==TRUE` alone** — that would pass even if the allocator never
  used it. The deciding clause is (3), the *count*.

---

## 3. Is R3 buildable and pressable NOW? — the gates

**R3a (the count half) is BUILDABLE now and PRESS-GATED.** The gates, named:

1. **G1 — must ride a pressed 958.** v2 §7 / 964 §5: the region port must be built on a *pressed* 958
   so the reported total (`max_mem` ← `hw.memsize`) is observed before anything is layered on it.
   **Evidence the gate is OPEN:** `STAGE90_XNU_MEM_TOTAL` **is** carried by the current press arms
   (`scripts/build_1012.sh:231`, in the switch list, and `build_1012.sh` drives the live line), but the
   958 arm itself was **PARKED, NOT PRESSED** (`armed-d13-ad6b4a68`, `experiment-958`). **Closed by:**
   the operator pressing a 958 arm and its `max_mem == 0xC0000000` being read in a capture.
2. **G2 — R3a introduces the first KERNEL reader of `/memory/reg`.** No `osfmk/arm/` object reads it
   today; the code is new and unexercised. **Closed by:** the R3a build itself + its own console
   record (the §2.3 criterion). Not a device gate — a first-build gate.
3. **G3 — R3b (emit) is gated on R5**, and R5 is gated on the **visualizer second field** (v2 §3.5),
   which does not exist (`ENTRY_ARM_KEYS`, `build_entry.sh:1987`, carries only the single
   `ENTRY_WINDOW` field). **Closed by:** adding the window's `(BASE, SIZE)` as a **second** record field
   (NOT by widening `ENTRY_WINDOW` — that moves `memSize` → `gMemSize` → the 956/970 ceiling). **R3a does
   not need this gate.**
4. **G4 — `STAGE90_XNU_BOUNCE_SIZE` open input** (v2 §3.3). **R3a does not need it** (count-only). It
   gates R5/R3b. Not closed; a design input, not derivable — do not invent a number.

**So: R3a is the next rung that can be built host-side today** (byte-neutral off; `make check` 0), and
it is **pressable only after G1** (a pressed 958). R3b/R4/R5 chain off G3/G4.

**One-line answer to the task's Step 3:** *R3a — the count half of the second allocator interval — is
buildable now but press-gated on the un-pressed 958 (G1); its emit half (R3b) is additionally gated on
the bounce window (G3), whose size input (`STAGE90_XNU_BOUNCE_SIZE`) is unmeasurable from this device
(G4).*

---

## 4. Provenance

Read by value this session: `src/stage90_main.c:49-79`, `src/stage90.h:29-58`,
`src/regions/xnu_memory_regions.c`/`.h`, `tools/check_d13_regions.py`, `tools/check_memory_regions_walk.py`,
`tools/check_d13_memory_total.py:152-167`, `tools/build_xnu_arm_kernel.sh:225-259`, `Makefile:263-389`,
`scripts/build_1012.sh:231`, `scripts/preflight_boot_check.sh:716-965`,
`out/stage90/device-reads/mi4-iomem-and-mem.txt:1-8`,
`external/xnu-hd2-darwin13/xnu/osfmk/arm/{pmap.c,pmap.h,arm_vm_init.c}`,
`external/xnu-hd2-darwin13/xnu/osfmk/vm/{vm_resident.c,vm_map.c}`,
`src/entry/entry_stubs.c:2117/2324-2341`, `src/entry/entry_smem.h:15-60`. Commits
`4ea04a9`,`08b4397`,`c2f6e98` (all ancestors of HEAD `a3b0ec2`) read via `git show`. No build, no device
action. Follows `experiment-915b-the-region-port-v2.md`, `experiment-964-…`, `experiment-976-…`,
`experiment-978-…`; [[mi4-915b-region-port-v2-design]], [[mi4-964-915b-corrected-against-958]],
[[mi4-956-d13-entry-window-ceiling]], [[mi4-958-3gb-rides-the-report]].

**PRESS IS THE OPERATOR'S.**