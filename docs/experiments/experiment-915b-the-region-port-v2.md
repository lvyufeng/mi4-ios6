# 915-B (v2) — the D13 region-list pmap port: the low bank becomes *allocatable* (2026-10-10)

**Status:** 📐 **DESIGNED, corrected a second time; NOT built.** This is the concrete, line-anchored
revision of 915/964, produced by an adversarial design pass (4 independent verifiers, 16 defects found,
all folded in) and re-confirmed by hand this session. Two open inputs block a full build (§8), and the
bounce window is not pressable until the visualizer carries a second field (§4.5). **PRESS IS THE
OPERATOR'S.**

Follows `experiment-915-multibank-region-list.md` (the original design) and
`experiment-964-the-915b-region-port-corrected.md` (the binding correction: do not touch
`max_mem`/`mem_size`/`sane_size`). Supersedes the *shape* of 915 §5 and corrects one claim §3.1 of 964.

---

## 1. What 915-B is, and what it is NOT

**Is:** the second half of the goal's memory clause, after 958. 958 made XNU *recognise* the 3 GB
(`max_mem` ← `/defaults hw.memsize`). 915-B makes the **low bank `[0, 0x60000000)` allocatable** — the
allocator hands its pages out and the kernel can reach them.

**Is NOT:**
- NOT a change to the reported total (`max_mem` keeps its ONE writer, the DT property, 958).
- NOT a change to `gMemSize`, `mem_size`, or `sane_size` (964 §1/§2).
- NOT a bigger entry window (956/970: D13's window is ceiling-bounded at
  `RAM_CONSOLE_BASE − MANAGED_BASE = 0x1e500000`, safe max `0x1e400000`; the window **is** the
  allocator's physical end, `avail_end = gPhysBase + gMemSize`).

## 2. The region model, and the exact edit sites

### 2.0 The two names (the fix for the v1 "one value, two definitions" defects)

**ONE quantity has TWO names, and at the console boundary they are genuinely different numbers:**

```
region[i]      = [region[i].base, region[i].base + region[i].size)     /* physical span = the DT /memory data */
REGION_MAX(i)  = min(region[i].base + region[i].size, RAM_CONSOLE_BASE) /* the reachable top */
```

`RAM_BOOT_BANK_SIZE` (`src/stage90.h:57`) is the boot region's size **after** the console clamp
(`RAM_CONSOLE_BASE − RAM_PHYS_BASE`); the raw DT high-bank size is `0x60000000`. v1 conflated these and
produced `gMemSize = 0x60000000` — which breaks the 956/970 ceiling and the 958 guard in one motion.
v2 names them apart; every consumer uses the name it needs.

### 2.1 `arm_vm_init.c` — the boot region is ALREADY selected; do not re-select it

Current latch (`arm_vm_init.c:331-334`) and 958 block (`:337-347`):

```
arm_vm_init.c:331  gPhysBase = args->physBase;
arm_vm_init.c:332  gVirtBase = args->virtBase;
arm_vm_init.c:333  gMemSize  = args->memSize;
arm_vm_init.c:337-347  #ifdef STAGE90_XNU_MEM_TOTAL … #else max_mem = mem_size = sane_size = gMemSize; #endif
```

**The v1 edit site (re-aim `gPhysBase`/`gMemSize` after line 347) is DELETED.** Both are load-bearing
single definitions:

- `gPhysBase` is the linear offset consumed by `phys_to_virt`/`virt_to_phys` (`pmap.h:193`),
  `pai_to_pvh` (`pmap.c:488`), `lock_pvh_pai` (`pmap.c:509-510`), `pmap_grab_page`
  (`pmap.c:2767-2769`). Re-aiming it is a **second definition of the linear offset**.
- `gMemSize` is the entry-window length the map is built with (`arm_vm_init.c:388-391,434`) and is what
  `check_d13_memory_total.py:144-145` binds (`gMemSize = args->memSize`).

**Why no selection is needed.** On the entry path the boot region base **is** the linear-map base:
`a->physBase = a->virtBase = STAGE90_XNU_ENTRY_BASE = 0x80000000` (`src/xnu_entry_jump.c:141-142`,
`src/entry/build_entry.sh:76`), and `gPhysBase + gMemSize = 0x80000000 + 0x1e400000 = 0x9E400000`
(real RAM, the current 484 MiB arm). So the boot region is simply *the region containing
`args->physBase`* — no bank is invented; the existing latch defines it.

**The v2 edit site** is a pure *registration*, after line 347, gated `#ifdef STAGE90_XNU_REGIONS`
(byte-neutral off, 958's exact pattern):

- read the region list from the DT `/memory/reg` (§2.4); build `region[]` with `REGION_MAX` clamped at
  `RAM_CONSOLE_BASE`;
- assert the boot region has `base == gPhysBase` and `size == gMemSize` (refusal §6.3);
- publish `region[]`/`region_count` into `pmap_mem_regions[]`/`pmap_mem_regions_count` (§2.3);
- assign **only** `pmap_mem_regions[]`, `pmap_mem_regions_count`, and the second allocator interval
  (§2.2) — **no** `max_mem`/`mem_size`/`sane_size`/`gPhysBase`/`gMemSize`.

### 2.2 `pmap.c` — region-aware predicates AND a second allocator interval

The single-interval assumptions (re-read by value this session):

```c
pmap.c:2152   pmap_valid_page: (((p<<PAGE_SHIFT) > avail_start) && ((p<<PAGE_SHIFT) < avail_end))
pmap.c:1955   pmap_zero_page:  panic if (p<<PAGE_SHIFT) < avail_start || > avail_end;  bzero(phys_to_virt(…))
pmap.c:2017   pmap_next_page:  if (first_avail >= avail_end) return FALSE;  *addrp = pa_index(first_avail); first_avail += PAGE_SIZE
pmap.c:1938   pmap_next_page_hi: bare `return pmap_next_page(pnum);`
```

v2:

- `pmap_valid_page`: TRUE iff `p<<PAGE_SHIFT` is inside ANY registered region **and** not inside a
  low-bank carveout span (§5 rung 2). Also fixes v1's endpoint quirk (`>=` region.start, `<` REGION_MAX).
- `pmap_zero_page` / `pmap_copy_page`: zero/copy through a **region-aware accessor** that returns the
  reachable VA for the page — never a raw `phys_to_virt` for a non-boot-region page (§3, refusal §6.5).
- Allocator: **two intervals.** Keep `avail_start`/`avail_end` exactly as they are (the boot region, so
  `pmap_bootstrap: physical region` print and `mem_size` sizing stay 958-neutral), and add a **second
  interval** `[avail_start2, avail_end2)`. `pmap_next_page` drains the first, then the second;
  `pmap_next_page_hi` inherits it for free. **This is what makes a low-bank ppnum emittable at all** —
  without it the first-press diagnostic could be satisfied with ZERO low-bank pages. It is a required
  deliverable of rung 3, not an afterthought.
- The allocator must **skip the low-bank carveout spans** (firmware/modem — `out/stage90/device-reads/`
  `mi4-iomem-and-mem.txt:4-6` = `[0,0x05A00000)`, `[0x0D200000,0x0FA00000)`, `[0x0FF00000,0x60000000)`
  minus the SMEM-reconciled usable set, `src/entry/entry_smem.c:245-248`). A prerequisite rung (§5 rung 2),
  not a property of the later window.

### 2.3 The beyond-the-task sites — ONE region table

`pmap.h:296-305` **already declares the canonical table** (never referenced anywhere in `osfmk/arm/`):

```c
pmap.h:303-305  #define PMAP_MEM_REGION_MAX 26
                extern mem_region_t pmap_mem_regions[PMAP_MEM_REGION_MAX];
                extern int pmap_mem_regions_count;
```

v2 chooses `pmap_mem_regions[]` as the **single definition** and drops v1's private-table alternative.
A refusal forbids a second `mem_region_t[]` (§6.2).

The sites indexed/sized off the single `mem_size`/`gPhysBase` must become region-aware. The clean
resolution that does **not** touch `mem_size`: **each region carries its own PV index base, and the PV
tables are sized per region** — the existing `pv_head_table` (sized by `mem_size`) stays the boot
region's; a second table sized by the non-boot region's length is allocated:

```c
pmap.c:488       #define pai_to_pvh(pai)  (&pv_head_hash_table[pai - atop(gPhysBase)])
pmap.c:509-510   lock_pvh_pai / unlock_pvh_pai   bit_lock(pai - atop(gPhysBase), pv_lock_table)
pmap.c:2767-2769 assert((page->phys_page << PAGE_SHIFT) > gPhysBase); … bzero(phys_to_virt(…))
pmap.c:3068/3908 map->pm_l1_virt = phys_to_virt(pages->phys_page << PAGE_SHIFT)
pmap.c:3498/3522-3533  pv_head_table / pv_rooted sizing = f(mem_size / PAGE_SIZE)
pmap.c:3658      _vm_object_allocate(mem_size, &pmap_object_store)
```

Each becomes `pai_to_pvh_region(pai)` selecting the region by `pai`, with a per-region lock table.
`pmap.c:3658`'s global pmap object needs the non-boot page count added as a **separate delta** — exactly
the kind of "attribute a global to `mem_size`" change that must be proven inert for the boot region by
refusal (§6.6).

```c
pmap.c:4492  kern_return_t pmap_add_physical_memory(...) { panic("Forget it! …"); }
```

The region path never calls it (refusal §6.9) — a region is registered at `arm_vm_init`, not added later.

### 2.4 The channel: the DT `/memory/reg` array, APPENDED (no `boot_args` ABI change)

`struct boot_args` has no region field (`src/stage90.h:133-147`), so the DT is the channel (already
handed over at `src/xnu_entry_jump.c:161`). Current writer/reader:

```
src/stage90_main.c:49  static const uint32_t memory_reg[] = { RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE, };
src/stage90_main.c:883 apple_dt_prop_u32_array(b, "reg", memory_reg, ARRAY_SIZE(memory_reg));
src/pe_state.c:38-39   memoryBase = reg_value(…,0,0); memorySize = reg_value(…,1,0);
src/pe_state.c:78-79   ok &= (memoryBase == RAM_PHYS_BASE); ok &= (memorySize == RAM_CONSOLE_BASE − RAM_PHYS_BASE);
src/pexpert.c:65-69    mem_reg[0]==RAM_PHYS_BASE && mem_reg[1]==args->memSize → "memory matches boot_args"
```

**v2:** `/memory/reg`'s **first pair stays `{RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE}`** (index 0/1 = the boot
region, unchanged) and the **low bank is APPENDED as reg[2]/reg[3]**:

```
memory_reg[] = { RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE, 0x00000000, 0x60000000, }
```

Genuinely inert for the boot region: `pe_state.c:38-39`, `pexpert.c:68`, and the gap-7 gate at
`src/xnu_pe_init_platform_false.c:283-304` (which the v1 `{low,high}` order made FALSE → a `platform_reboot`
at `src/stage90_main.c:1331`) all keep reading pair 0. Rung 2 consumes reg[2..3]. The reader's list walk
(which pair is the boot region) becomes a **base-match** on `args->physBase`, so "index 0/1 = the boot
region" stops being a convention the data can violate and becomes a checked invariant. Every reader
(`pe_state.c`, `pexpert.c`, `xnu_pe_init_platform_false.c:283-304,424`, `mmu.c:2386-2394`) routes
through the one accessor (§6.8).

## 3. The bounce window — the real reason, and the SIZE open input

### 3.1 Correcting 964 §3: the low bank does NOT underflow `phys_to_virt`

964 §3 (and 915 §2.1) state: *"any PA below `gPhysBase` underflows"*. **That is false for this arm.**
With `gPhysBase = gVirtBase = 0x80000000` and a low page `p < 0x80000000`, the intermediate
`(p − gPhysBase)` is negative in `unsigned long`, but the **cast to `unsigned int`** (`pmap.h:193`)
reduces it modulo 2³², so

```
phys_to_virt(p) = p + (gVirtBase − gPhysBase) = p        /* a valid low VA, mapped 1:1 */
```

The entry identity map covers `[gVirtBase, gVirtBase+gMemSize)` and the low bank is inside kernel VA
(`p < 0x80000000 ≤ KVA_BASE`), so a low page is reachable at its identity VA with no fault.

**The actual reason a window is needed** is different, and must be stated instead: a low page **does not
fit the boot-region-sized PV/KMA bookkeeping** (`pv_head_table`, `pmap_object_store`, `kernel_map` are
sized and indexed off the boot region's `mem_size`/`gPhysBase`, §2.3) and has **no slot in the fixed
`MANAGED_BASE = 0xC0000000` map** (`arm_vm_init.c:142,373-391`). The window's job is to give a
**managed-side consumer** a bounded VA slot for a low-bank page, sized by that page's DMA footprint —
not to paper over an underflow. *(964 §3 is the same doc that is otherwise binding; its underflow wording
is superseded here.)*

### 3.2 Base placement — computed and proven, not assumed

v1 asserted "0xA0000000 sits in the free gap". That gap is a function of `gMemSize`, not a fixed slot.
v2 computes `BASE` from the arm's resolved `gMemSize` and proves it by walking the live L1:

```
BASE >= gVirtBase + gMemSize                         (above the identity map)
BASE >= TREE_INFO_VA_BASE + MEMORY_INFO_VA_SIZE      (above the device-tree info slot)
BASE + SIZE <= MANAGED_BASE (0xC0000000)             (below the managed map)
BASE + SIZE <= RAM_CONSOLE_BASE (0xde500000)         (below the console)
BASE + SIZE <= LIVE_CONSOLE_ALIAS_BASE (RAM_CONSOLE_BASE + 0x01000000 = 0xDF500000, entry_stubs.c:2096)
```

plus a **full L1-slot walk** of `[BASE, BASE+SIZE)` simulating `l2_cache_to_range`
(`arm_vm_init.c:239-270`) that must touch **no occupied slot** — and, when the arm is a 484 MB arm, the
walk must model the **string-mapped** managed map (`MAKEPMAP_ENTRY`) or the window can land on the live
console alias (silent). The refusal therefore takes the arm's `gMemSize` as an input, not a constant.

### 3.3 The SIZE is an explicit, named OPEN INPUT (not papered over)

**The largest single low-bank DMA footprint is NOT derivable from this device.** 964 §4 measured it: the
DT has **no `reserved-memory` node**; the low-bank carveouts are Android `/proc/iomem` spans and the SMEM
partition table, not a DMA descriptor. v1 asserted 16 MiB *as if derived*; v2 records:

```
STAGE90_XNU_BOUNCE_SIZE  := OPEN INPUT (design input; NOT derivable from device evidence)
STAGE90_XNU_BOUNCE_BASE  := computed from the arm's resolved gMemSize + the L1-slot walk, NOT hard-coded
```

Proposed default 16 MiB (`0x01000000`; 4096 pages = 16 L2 tables) — matching the project's 16 MiB
granularity and the console alias step. **A proposal, not a derivation.** A build refusal (§6.4) makes a
*caller exceeding* the window a build error; a *too-small* window is a runtime failure that no build
refusal can catch — that is the residual risk, stated.

### 3.4 On-demand mapping

The window is a page-slot cache, not a linear map. A helper maps a target low-bank page into
`[BASE, BASE+SIZE)` via `l2_cache_to_range` into the TTB the MMU walks, then flushes by MVA
(`pmap_flush_tlbs`, `pmap.c:1275`; single-MVA primitive `flush_mmu_single`,
`machine_routines_asm.s:293`). Consumers use the **window accessor**, never raw `phys_to_virt` (§6.5).

### 3.5 The visualizer dependency — the window is not pressable without it

An arm communicates to the payload's visualizers through one `ENTRY_WINDOW` string field
(`scripts/build_975.sh:35-39`, `src/entry/build_entry.sh:1357`). The window's `(BASE, SIZE)` must be added
as a **second field**, NOT by widening `ENTRY_WINDOW` (widening moves `memSize` → `gMemSize` → the
956/970 ceiling). Until that field exists the bounce window is invisible to every consumer → a silent
boot, so **it is not pressable**; only the `#ifdef STAGE90_XNU_REGIONS`-gated byte-neutral code and the
DT injection (inert for the boot pair) may be built and observed.

## 4. Forced order of rungs

The order is forced by: no rung may hand a low-bank page to a consumer before (a) the carveout spans are
excluded and (b) a reachable managed VA exists; and the window is meaningless before the allocator can
produce low-bank pages.

1. **Rung 0 (host).** Region type + accessor + `pmap_mem_regions[]` population + the L1-slot-walk refusal
   + the DT-shape refusals. Gated `#ifdef STAGE90_XNU_REGIONS`, byte-neutral off.
2. **Rung 1 — inject (inert). APPEND the low bank.** `memory_reg[]` →
   `{RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE, 0x00000000, 0x60000000}` (`src/stage90_main.c:49-51`, emitted
   `:883`). Boot pair first; no reader reads reg[2..3] yet → genuinely inert.
3. **Rung 2 — carveout reconciliation FIRST.** Reconcile the low bank's `/proc/iomem` spans against the
   SMEM partition table (`entry_smem.c:245-248`) → the **allocatable low-bank sub-intervals**. v1 deferred
   this past the press, so its first low-bank handout could be a firmware page. This rung allocates nothing.
4. **Rung 3 — predicates + second allocator interval.** Region-aware `pmap_valid_page` /
   `pmap_zero_page` / `pmap_copy_page`; add `[avail_start2, avail_end2)` and drain-then-second in
   `pmap_next_page`; register `region[]`. A low page **is** reachable 1:1 (§3.1), so rung 3 alone can
   *emit* low pages; the window is required only for a **managed-side consumer**. So rung 3 is
   pressable **only** for a scoped observation of `avail_remaining` counting the low bank, with a refusal
   (§6.5) naming the missing window for any consumer path ([[mi4-a-lower-rungs-side-effect-poisoned-the-rung-above]]).
5. **Rung 4 — tables.** Per-region PV/pv_head/pv_lock tables + the global pmap object delta (§2.3);
   make `pai_to_pvh`/`lock_pvh_pai`/`pmap_grab_page`/`pmap_create` region-aware. Makes a low page's
   bookkeeping safe.
6. **Rung 5 — bounce window + visualizer field.** Reserve `[BASE, BASE+SIZE)` (§3.2), add the on-demand
   mapper + MVA flush + window accessor (§3.4), add the visualizer's second field (§3.5). First rung that
   makes a low-bank page usable by a **managed-side** consumer.
7. **Rung 6 — first press (OPERATOR'S).** On the full stack only.

## 5. Build refusals (each stops the build)

1. **`check_d13_memory_total.py` fact 5 (unchanged).** `mem_size`/`sane_size`'s terminal RHS is exactly
   `gMemSize`; a region-sum port is refused. Selftest mutations exist.
2. **ONE region table.** `mem_region_t` defined once; the only `mem_region_t[]` is `pmap_mem_regions[]`
   (`pmap.h:303-305`); refuse any second one.
3. **`gPhysBase`/`gMemSize` have ONE assignment each.** The region block assigns **neither** them nor
   `max_mem`/`mem_size`/`sane_size`; and `region[boot].base == gPhysBase`, `region[boot].size == gMemSize`.
4. **Bounce window: named, non-colliding, refused path.** Resolve `STAGE90_XNU_BOUNCE_BASE`/`_SIZE`;
   refuse unless (a) the five inequalities hold for the **arm's** `gMemSize`; (b) the full L1-slot walk
   touches no occupied slot; must fail the build if the window accessor is called while unsatisfied.
   (Cannot reject a too-small SIZE — recorded as open input.)
5. **No raw `phys_to_virt` on a LOW page.** NEGATIVE check over `pmap.c`: `pmap_zero_page`,
   `pmap_copy_page`, `pmap_grab_page`, `pai_to_pvh`, `lock_pvh_pai`, the PV-table sizing (`:3498,3522-3533`)
   must name the region accessor.
6. **Predicates region-aware (positive).** `pmap_valid_page`/`pmap_zero_page`/`pmap_next_page` reference
   the region accessor; the second interval `[avail_start2, avail_end2)` exists.
7. **DT `/memory/reg` shape — boot pair FIRST.** Even length, `len >= 2`, `memory_reg[0] == RAM_PHYS_BASE
   && memory_reg[1] == RAM_BOOT_BANK_SIZE`; extra pairs are additional banks; refuse low-bank-first order
   and odd length.
8. **One reader.** Every `/memory/reg` reader routes through the one accessor with the pair-0 base-match;
   refuse a re-implementation of `reg_value(...,n,0)`.
9. **`pmap_add_physical_memory` not on the path.** Refuse if the region path calls it while `pmap.c:4492`
   is still the panic.
10. **Existing guards unchanged.** `check_d13_managed_base.py` (`MANAGED_BASE == 0xC0000000`,
    `gMemSize = args->memSize`) and `check_d13_vm_init_staged.sh` still pass.
11. **Entry-window ceiling intact.** `build_entry.sh` still refuses a D13 window `>= 0x1e500000`
    (`:29525`) and `>= 0x40000000` (`:29500`); the port must not raise `STAGE90_XNU_ENTRY_SIZE`.
12. **The visualizer anchor cannot claim a window the code did not install.** For a bounce-window arm the
    `ENTRY_WINDOW` record must name BOTH the entry window AND the bounce-window second field.

## 6. Residual risks, and what the FIRST PRESS would observe

**Residual risks.**
- **Bounce-window SIZE is an OPEN INPUT** (16 MiB is a proposal; a too-small window is a runtime failure).
- **Low-bank carveout reconciliation is unmeasured at rung-2 time** (open input #2).
- **PV-table sizing**: an rung-4 edit that forgets a table silently under-sizes it (refusal §5.5 covers the
  named sites only).
- **TLB coherence**: the on-demand remap needs an MVA flush ([[mi4-idle-exit-l2-line]]).
- **975-lineage overlay hole**: if the 484 MB arm's managed map is string-mapped, the live-console alias
  at 0xDF500000 is inside the managed VA and §3.2's refusal must model it. Not yet proven.
- **No smaller safe slice than the whole region port** (964 §3): rung 3 hands low pages to consumers only
  safely after rung 5; the rung-3 scoped observation is the only bounded observable.

**What the FIRST PRESS would observe.** A boot that reaches `arm_vm_init`'s map construction, prints the
boot region and the region count, initialises the pmap PV tables per region, produces `avail_remaining`
**including the low bank** (the second interval), and succeeds in a low-bank `pmap_next_page` +
`pmap_zero_page` through the region accessor without a data abort. The verdict is
`wdt_pets > 0` resident ([[mi4-911-resident-nonreturn-is-not-a-wedge]]) plus a console record naming the
low-bank page count; a non-return alone is not a failure.

## 7. Status

- Clause 「正确识别 3 GB」: **met** (958 + 911c). Clause 「16/32 GB」: **met** (911d).
- Clause 「the low bank allocatable」 (915-B): **designed here, corrected v2; NOT built.** Two open inputs
  (bounce-window size; SMEM span reconciliation) block a full build; the window is not pressable until the
  visualizer field exists (§3.5). Per 964 §5 it must be built on a *pressed* 958.

**PRESS IS THE OPERATOR'S.**

*Provenance: `arm_vm_init.c:81/142/234-277/331-347/373-391/434`, `pmap.c:1938/1955/2017-2031/2152/488/509-510/2767/3498/3522-3533/3658/4492`,
`pmap.h:192-193/296-305`, `src/xnu_entry_jump.c:141-142/161`, `src/stage90.h:57`, `src/stage90_main.c:49/883/1331`,
`src/pe_state.c:38-39/78-79`, `src/pexpert.c:65-69`, `out/stage90/device-reads/mi4-iomem-and-mem.txt`, and
`out/stage90/xnu_arm_entry.h` read by value this session; produced by an adversarial design pass (4 verifiers,
16 defects). Follows [[mi4-915-multibank-region-list-design]], [[mi4-964-915b-corrected-against-958]],
[[mi4-958-3gb-rides-the-report]].*