# 1012 — NAME the F1a strip site: the PTE-build apex + the four strip sites (design)

**Status:** design + code staged, not yet built/pressed. Predecessor: `records/1011-press-read.txt`
(arm `armed-d13-fce83837`), which MEASURED **F1a** and found the walk-mask defect (fixed, `07914cf`).

---

## 1. What 1011 decided, by value, and what it left open

1011's (E′) ladder read every storm tail row as

    xnu_live_abt_vmekr   = 0   (KERN_SUCCESS)
    xnu_live_abt_vmeprot = 0x3 (VM_PROT_READ|VM_PROT_WRITE)   <- the vm_map_entry GRANTS write
    xnu_live_abt_vmefault= 0x3 (a WRITE fault)
    xnu_live_abt_vmeva   = 0x61000

`out_prot` IS `entry->protection` (`vm_map.c:9061/9167`), and `vm_map_lookup_locked` returns
`KERN_PROTECTION_FAILURE` (`vm_map.c:9087`) when it does NOT grant the requested `fault_type`. It
returned SUCCESS with `prot=0x3`, so **the entry GRANTS write** and the accept arm's
`kr = KERN_SUCCESS` (`vm_fault.c:2478`) is the EXPECTED result. **F1b is falsified by value.**

**Hence F1a: the write is LOST between `prot` and the PTE.** The store re-faults ~1 M times at the
same pc/far/fsr. What 1011 did NOT name is *where*.

### 1.1 The apex fact (this is what makes the bisection one cell)

This tree builds `BOARD_CONFIG_QSD8250_LEO` (`external/.../osfmk/arm/PlatformConfigs.h`; verified with
`grep`). Therefore `pmap_enter_options` dispatches to `leo_enter` (`arm/pmap.c:3181`), and `leo_enter`
rebuilds the leaf **UNCONDITIONALLY** from the `prot` it is handed:

    *pte = leo_pte_build(map, va, pn, prot, wired);     /* pmap.c:1668 and :1680 */

and `leo_pte_build` (`pmap.c:~1640`) calls `pmap_vm_prot_to_page_flags` (`pmap.c:1095`), which CLEARS
the APX bit (writable) iff `prot & VM_PROT_WRITE`. Every `leo_enter` path ends in
`pmap_flush_tlbs(map, va, va+PAGE_SIZE)`. So:

> **A `prot` that carries VM_PROT_WRITE at `pmap_enter_options` MUST produce a writable leaf.**

Since the leaf is NOT writable (the store faults), exactly one of these holds:

- **(b)** `prot` at the apex LACKS WRITE — a **strip site fired** above the PMAP_ENTER (or the
  PMAP_ENTER was SKIPPED, by the codesign early-return), **or**
- **(c)** `prot` at the apex CARRIES WRITE — the leaf IS writable in the table the pmap wrote, but the
  MMU walks a DIFFERENT table (a pmap/L1/TTBR0 identity fault).

## 2. What is REFUTED by value (do not press to find these)

Read at the sites this run:

| site | text | verdict |
|---|---|---|
| `vm_fault.c:1881` | `*protection &= (~VM_PROT_WRITE);` inside `if (object != first_object) { if (fault_type & VM_PROT_WRITE) {...} else <HERE> }` | **REFUTED**: the `else` requires `(fault_type & VM_PROT_WRITE) == 0`; the storm's `fault_type=0x3` (measured) |
| `vm_fault.c:1904` | `*protection &= ~VM_PROT_WRITE;` inside `if ((fault_type & VM_PROT_WRITE) == 0) { ... }` | **REFUTED**: same bit test, `fault_type=0x3` |
| `vm_fault.c:2353` | `prot &= ~VM_PROT_WRITE;` gated on `(fault_type & VM_PROT_WRITE) == 0` (vm_fault_enter) | **REFUTED**: same |
| `vm_map.c:8940` | `prot = submap_entry->protection & ~VM_PROT_WRITE;` | **REFUTED for this row**: the (E′) row's `vmeprot=0x3` IS `out_prot`, set from `prot` at `vm_map.c:9061` AFTER the submap block; a strip at `:8940` reaches `:9061` only if it assigns the FINAL `prot`, and it does not (the submap COW case `entry = submap_entry` is taken instead) — see §3 |

The 1011 capture's RAW console line `ttbcr=0` is not the storm's TTBCR: `arm_vm_init.c:475` sets
`TTBCR.N=2` (`set_mmu_ttbcr(2)`) at the storm, so TTBR0's base is bits[31:12], a 4 KB table. The (A)
cell's `xnu_live_abt_ptethr = 0x81a86000` (the faulting thread's `pmap->pm_l1_phys`) is 4 KB-aligned,
consistent with N=2, and equals the live TTBR0 masked to bits[31:12] in the capture.

## 3. What survives (the live candidates)

1. **The RetryFault block**, `vm_fault.c:3752-3856`:
   - `:3770` the verify RE-LOOKUP deliberately passes `fault_type & ~VM_PROT_WRITE` (=1, READ only);
     its product `retry_prot` can therefore carry only READ;
   - `:3845` `prot &= retry_prot;`  → strips WRITE if `retry_prot` lacks it;
   - `:3855` `prot &= ~VM_PROT_WRITE;` when `m->object->copy != old_copy_object` (the copy object
     changed while the map was unlocked) → a DIRECT strip.
   - **Why (E′) cannot see this**: 1011's (E′) latch (`g_abtpte_vmelatch`) is CONSUMED by the FIRST
     lookup and never re-armed within the same `vm_fault`, so the verify re-lookup at `:3769`
     publishes NO `_vme` row. The `vmeprot=0x3` rows are the FIRST lookup; they say nothing about
     `retry_prot`. **These are the prime suspects.**
2. **The codesign early-return**, `vm_fault.c:2421` `return KERN_CODESIGN_ERROR;` — the
   switch-protected immutable-page case. It does NOT strip; it SKIPS the PMAP_ENTER. If it fires,
   the leaf for `0x61000` keeps whatever it had (possibly a read-only boot/commpage leaf) — a
   plausible F1a shape.
3. **The apex identity fault (c)** — if `prot` at the apex carries WRITE, the fault is that
   `pmap_pte(map,0x61000)` wrote a table the MMU does not walk.

## 4. The rung — one press, a two-way bisector

**Switch `STAGE90_XNU_PMP_ENTER`** (default 0). Two halves, both entry-toolchain:

- **`entry_trace.c`**: `--wrap=pmap_enter_options` (added to `TRACE_LDFLAGS` only when the switch is
  on). The wrapper is a PURE PASS-THROUGH; it records BEFORE the real call.
- **`entry_stubs.c`**: `entry_note_abt_pmp(va, prot, pa, pm_l1_phys)`, gated by the SAME latch chain
  `entry_note_abt_pte` arms (`g_abtpte_pmpva` = the abort's own far). Publishes the ladder rows:

      xnu_live_abt_pmpn      - the ordinal (ladder rule n<=4 || power-of-two)
      xnu_live_abt_pmpprot   - the `prot` handed to pmap_enter_options   <- THE APEX CELL
      xnu_live_abt_pmppa     - the PA page number entered
      xnu_live_abt_pmppmap   - the pmap's OWN pm_l1_phys (offset 0 of struct pmap, pmap.c:1280)
      xnu_live_abt_pmpttbr   - the LIVE TTBR0 (mrc p15,0,..,c2,c0,0) read in the same call

**The verdict the press decides:**

| tail `_pmpprot` | tail `_pmppmap` vs `_pmpttbr` | conclusion |
|---|---|---|
| `& WRITE == 0` | any | **(b) — a strip SKIPPED the write**: `prot` lost WRITE above the apex. The fix is at the strip site; the F4 retry-bound (`tools/patch_d13_abt_retry_bound.py`) is the safety net. |
| `& WRITE != 0` | `bits[31:12]` EQUAL | **(c) — the apex agrees with the MMU-walked base** but the leaf is still read-only ⇒ the loss is INSIDE `leo_enter`/`leo_pte_build` (read `pmap.c:1640-1682` by value with the measured `prot`) |
| `& WRITE != 0` | `bits[31:12]` DIFFER | **(c) — an identity fault**: the pmap writes one L1, the MMU walks another (a `pmap_expand_ttb` hoist not followed by a TTBR0 load, or a stale `pm_l1_phys`) |
| NO tail row | — | the storm's enter did NOT reach the wrapper — the wrapper is not on the storm's path; fall back to a tree instrument |

The `_pmppmap`/`_pmpttbr` pair ALSO closes the walk-mask question for good: they are the two bases
the (A) walk compares, now read at the apex instead of from a saved frame.

## 5. The companion tree probe (`tools/patch_d13_wrstrip.py`, switch `STAGE90_XNU_WRSTRIP`)

If the apex rows do not name the strip (or to NAME the exact strip line for a fix), the tree probe
inserts `entry_note_abt_wr(site, va, prot)` at the strip sites and at the PMAP_ENTER macro. It is a
PURE MEASUREMENT (control flow and values unchanged), each probe wrapped in
`#if defined(STAGE90_XNU_WRSTRIP) && STAGE90_XNU_WRSTRIP ... #endif` so the OFF tree is byte-identical,
and it REFUSES a half-applied tree (the `patch_d13_abt_retry_bound.py` rule). Honest scope: the two
`vm_fault_page` `*protection` sites have no VA parameter, so their probes pass `0u` and the entry side
(the abort-VA keyed latch) will not publish for them — a KNOWN limitation, stated at the site.

## 6. Build refusals / by-value checks

- The switch's guard is the conjunction `ABT_FAR && ABT_PC && ABT_PTE && PMP_ENTER`; `build_entry.sh`
  REFUSES `PMP_ENTER=1` unless the full 1010 ladder is on (it keys on the latch that chain arms).
- OFF control: with `PMP_ENTER=0`, `entry_stubs.o` and `entry_trace.o` are BYTE-IDENTICAL to HEAD
  (proven this session: `cmp` of the two objects).
- `--wrap=pmap_enter_options` is added ONLY when the switch is on, so the OFF image carries no such
  wrap.

## 7. F4 stands as the SAFETY NET

`tools/patch_d13_abt_retry_bound.py` (`STAGE90_XNU_ABT_RETRY_BOUND`) bounds the silent retry at 8
consecutive identical `(pc,far,dfsr)` and escalates by SETTING `exception_type=EXC_BAD_ACCESS`
(commit `1174497` fixed the fall-out-with-0 bug). It converts the freeze to a panic; it does not
advance the boot. Whether to include it in the 1012 press is a separate call — this rung's cell
(`PMP_ENTER`) is a measurement and does not need it, and a panic would end the run before the storm
ladder filled.