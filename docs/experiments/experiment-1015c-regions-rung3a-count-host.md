# Experiment 1015c — 915-B rung 3a (HOST HALF): the DT region list becomes the pmap's table, the COUNT half

**Status:** 🛠 **BUILT (HOST-SIDE ONLY), VERIFIED by stage/check/revert round-trip; NOT built for the device,
NOT pressed, NOT committed.** The device is **DARK**. `external/xnu-hd2-darwin13/xnu` is left **UNSTAGED**
(measured: `pmap.c` md5 `f8c6345bd4fba0513f5dff45b6bdc4e5`, unchanged). R3a has **no device-side
reader yet** — the first device event is reminder-gated and press-gated (see §6). Follows
`docs/915b-next-rung.md` §2 (R3a) on `docs/experiments/experiment-964-the-915b-region-port-corrected.md`
and 976/978. **PRESS IS THE OPERATOR'S.**

Deliverables: `tools/patch_d13_regions_r3a.py` (the edit), `tools/stage_d13_regions_r3a.sh` (the driver),
`tools/check_d13_regions_r3a.py` (11 by-value facts + a NULL CONTROL, `--selftest`).

---

## 1. What R3a is, and what it deliberately is NOT

915-B v2 §2.4 makes the DT `/memory/reg` a **REGION LIST** (boot pair first, the low bank appended by
976's rung 1 as words 2/3). Rung 3 — *"the kernel acts on the region list"* — **splits in two**, and the
split is forced by a **measurement**, not a preference:

- **R3a (THIS RUNG) — the count half.** Publish the region list into the tree's ONE canonical table
  (`pmap_mem_regions[]`/`pmap_mem_regions_count`, declared at `osfmk/arm/pmap.h:303-305` and, MEASURED,
  **defined nowhere** in `osfmk/arm/` today), make `pmap_valid_page` a **region span-set** (carveout-
  excluding), and make the free-page accounting (`avail_remaining`, `pmap.c:2723`, exposed as
  `pmap_free_pages()` and the `vm_page_bootstrap: N free pages` printf) **include the low bank**. R3a
  emits nothing to a mapped consumer.
- **R3b (NOT this rung) — the emit half.** Handing low pages to consumers. **Gated on R5's bounce window.**

The rung lives **entirely behind `STAGE90_XNU_REGIONS`** — a switch MEASURED to be threaded into the
kernel corpus already (`tools/build_xnu_arm_kernel.sh:243-259`, always defines it 0 or 1, refuses any
other value). At 0 the tree's bytes compile unchanged; the arms the ladder has already run do not move.

## 2. The reachability gate — why R3a is only the count half (§2.2, the design's catch (a))

`arm_vm_init.c:475` sets `TTBCR.N = 2` (`set_mmu_ttbcr(2)`), so **TTBR0 owns `[0, 0x40000000)`** and
TTBR1 owns the rest. The kernel's identity/managed tables are built into the ONE `cpu_ttb` that TTBR1
points at, and that build covers `[gPhysBase, gPhysBase + gMemSize)` = **`0x80000000`-and-up only**
(`l2_map_linear_range`, `arm_vm_init.c:400`). A general low DRAM page `p < 0x40000000` therefore has **no
mapped kernel VA**. The moment such a page is **handed to a consumer and dereferenced** (e.g. `vm_pages[i]`
in `vm_page_init`, or `bzero` in `pmap_zero_page`) it is unreachable. R3a's whole safety rests on
**draining low pages only into `pmap_startup`'s own structures**, never into a mapped consumer — R3b.

The gate is expressed as data: `pmap_mem_regions_reach2 = (1u << (32u - 2u))` (the split constant) and the
count's skip `if (start >= pmap_mem_regions_reach2) continue;` — the boot region (base == the split) is
excluded from the low count, and every low region is admitted. `check_d13_regions_r3a.py` F7 makes both a
**refusal**.

## 3. THE CORRECTION R3a MAKES TO THE DESIGN TEXT — the count is NOT separable from the drain

**MEASURED by value** in `pmap_startup` (`osfmk/vm/vm_resident.c:799-821`):

```c
tmpaddr = (addr64_t)pmap_free_pages() * PAGE_SIZE;
npages  = tmpaddr / (PAGE_SIZE + sizeof(*vm_pages));
vm_pages = pmap_steal_memory(npages * sizeof(*vm_pages));   /* steals npages PHYSICAL pages */
for (i = 0; i < npages; i++) { if (!pmap_next_page(&phys_page)) break; vm_page_init(...); }
```

`pmap_free_pages()` **sizes** `vm_pages[]`, and `pmap_steal_memory` then **steals that many physical
pages** through the **same** single-interval `pmap_next_page`. So the design's pre-registered criterion
(§2.3.3: `avail_remaining == boot_pages + 0x58300`) **with `pmap_next_page` unchanged is UNSAFE**: a count
raised by `0x58300` while the drain still stops at `avail_end` would **not** hand out a low page — it would
make `pmap_steal_memory` consume ~`0x58300` EXTRA **boot** pages for the bigger `vm_pages[]`, then possibly
under-run into `panic("pmap_steal_memory")` (`vm_resident.c:762`). **That is a boot that dies before its own
console record.**

R3a therefore stages the count **together with** a region-aware, carveout-skipping **drain** in the same
switch (`pmap_second_cursor`, seeded in `pmap_bootstrap`, drained in `pmap_next_page`'s exhaustion arm via
the SAME `pmap_valid_page`). `low_pages` (the count) **equals** the pages the drain can return — both are
the region-table membership sum over the same table. `check_d13_regions_r3a.py` F9 refuses a count
without its drain seed.

## 4. The edit (two files, all inside `#if STAGE90_XNU_REGIONS`)

| # | site | what |
|---|------|------|
| 1 | `pmap.c` after `uint32_t avail_start, vm_first_phys;` (211) | the second interval `avail_start2`/`avail_end2`, the drain cursor, and the **ONE DEFINITION** of `pmap_mem_regions[]`/`pmap_mem_regions_count` + `pmap_mem_regions_usable_end[]` + `pmap_mem_regions_reach2` |
| 2 | `arm_vm_init.c` before `arm_vm_init` | `entry_xnu_register_regions(args)`: walk the DT `/memory/reg` through the tree's OWN API (`DTLookupEntry`/`DTGetProperty`), run the host accessor's refusals raised to `panic`, publish the table + the reachability split |
| 3 | `arm_vm_init.c` after Apple's 958 `#endif` (338) | the ONE call — a `static` function with no call site is a build error |
| 4 | `pmap.c` `pmap_valid_page` (2207) | region-aware span-set; at switch OFF the whole body is Apple's two-line expression, byte for byte, inside `#else` |
| 5 | `pmap.c` `pmap_bootstrap` (2723) | the count + its console record + the drain seed; Apple's `avail_remaining = (avail_end - first_avail) >> PAGE_SHIFT;` stays byte-identical |

`pmap_zero_page`/`pmap_copy_page`/`pai_to_pvh`/`lock_pvh_pai` are **NOT touched** (R4/R5's job). The
registration **assigns neither** `gPhysBase`/`gMemSize` **nor** `max_mem`/`mem_size`/`sane_size` (964;
C1/C2) — it only READS the boot globals and CHECKS `region[0]` against them.

`pmap_mem_regions[i].phys_table` stays NULL — the per-region PV base is R4's, and no code path may
dereference it until R4.

## 5. The two catches the design flagged (§2.2 / §2.3), and how each is honoured

**(a) The reachability gate** → F7, above. Stated the way the tree can support it: TTBR0 owns
`[0, 2^(32-N))` under `TTBCR.N = 2`, and the kernel build covers `[gPhysBase, gPhysBase + gMemSize)`.
The check asserts the BOUND is what the count uses; whether the hardware walks that bound is the press's
reading, not the check's.

**(b) The vacuous `vm_page_init` `#if DEBUG` clause — STRUCK, not passed.** `osfmk/vm/vm_resident.c`
guards `panic("vm_page_init: non-DRAM phys_page 0x%x\n", phys_page)` with `#if DEBUG`. **MEASURED:** the
D13 kernel is composed from Apple's **`RELEASE`** configuration (`tools/build_xnu_arm_kernel.sh:274`,
`CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}`), whose define set carries **no `-DDEBUG`**
(`tools/xnu_defines`/`make_defines.sh RELEASE`), and nothing in the kernel build path defines it. So
`#if DEBUG` is `#if 0`, the panic is **compiled out**, and a criterion reading *"the panic does not fire"*
would be satisfied by a boot that never had the code. F10 encodes the strike as a **measurement**: it
refuses if a `-DDEBUG` ever enters the kernel build path (then the clause would be LIVE and the strike
wrong). The record does **not** rest on the clause.

## 6. The NULL CONTROL (§2.5)

R3a's instrument is the low-bank count record, keyed **`1015b-regions R3a: low-bank regions ...`** and
printed from **inside** `#if STAGE90_XNU_REGIONS`. The null control is the `STAGE90_XNU_REGIONS=0` arm,
where the key is **ABSENT, not 0** (the project's one-way absent-key idiom). F11 makes that structural:
the key must occur **exactly once** and its nearest enclosing `#if` must be the regions switch — at 0 the
emit is not compiled and the key cannot appear.

## 7. What was MEASURED (this session), and what is UNMEASURED

**MEASURED against the real tree:**
- The switch is threaded into the kernel corpus (`build_xnu_arm_kernel.sh:243-259`); staging refuses a
  second channel (`XNU_KERNEL_EXTRA_DEFINES` carrying it → rc 2).
- `mem_region_t` is typedef'd exactly once, at `pmap.h:295`; no `mem_region_t[]` **definition** exists in
  `osfmk/arm/*.c` before R3a (F2's premise).
- `gPhysBase` is assigned once (`:331` = `args->physBase`, the `:81` line is a file-scope initializer, not
  the boot latch); `gMemSize` once (`:333` = `args->memSize`).
- `mem_size`/`sane_size`'s only assignments are the 958 triple `= gMemSize` (`:338`, `:346`).
- `avail_end = gPhysBase + gMemSize;` at `arm_vm_init.c:467`; the `physical region` print at `pmap.c:2727`;
  `avail_remaining = ...` at `:2723`.
- Full **stage → check → idempotent → revert** round-trip in a scratch tree: `--check PASS`,
  `--anchors PASS`, every anchor unique 1×, revert restores `pmap.c` to md5
  `f8c6345bd4fba0513f5dff45b6bdc4e5` **identical** to the untouched tree.
- `check_d13_regions_r3a.py --selftest`: clean staged tree passes all 11 facts; each of the 11 mutations
  (F1–F11) is caught.
- `bash -n tools/stage_d13_regions_r3a.sh` OK; no unescaped backticks in double-quoted strings.

**UNMEASURED (flagged, not resolved):**
- **No compiler has run.** `struct xnu_mem_region` is supplied by the tree's own
  `src/regions/xnu_memory_regions.h`; whether its `-I` path resolves in the **kernel** corpus (vs the host
  check) is **unverified**. This is the first thing the first build will prove.
- The exact `pmap_startup` boot-page consumption and the first build's `avail_end`/`available_low` value.
- `ram_console`/`LIVE_CONSOLE_ALIAS` overlap with the low spans (`0x0fa00000` SMEM identity section).
- Whether the kernel's TTBR0 equals `cpu_ttb` at `arm_vm_init` time (design §2.2's honest caveat).

## 8. The exact command sequence a human runs (LATER, once past the gate)

Gate **G1** (`docs/915b-next-rung.md` §3) is still **closed**: the 958 arm was parked, not pressed; R3a
must ride a **pressed** 958. The device is dark. When the gate opens:

```sh
cd /mnt/data/mi4-ios6
# 1. stage the edit (idempotent; refuses a second switch channel)
tools/stage_d13_regions_r3a.sh
# 2. re-run the by-value check (refuses an unstaged or half-applied tree)
python3 -I tools/check_d13_regions_r3a.py
# 3. build the kernel corpus AND the payload with the ONE switch exported
export STAGE90_XNU_REGIONS=1
tools/build_xnu_arm_kernel.sh
STAGE90_XNU_REGIONS=1 STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh
# 4. PRESS (the operator's). Do NOT flash.
```

A press is not run by this session; `out/stage90` holds the frozen `1015b` arm
(`out/stage90/frozen/armed-d13-16fe8885/`) and is **not** rebuilt here.

## 9. Relation to the goal

This serves the goal's **"recognise 3 GB"** clause on the only path left: the raised entry window cannot
exceed **1 GiB** on this tree (memory `mi4-956`), so the low bank must be booked by the region model. R3a
books the **count**; the consumers that would use it are R4/R5.