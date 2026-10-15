# 1012 — STATIC F1a forensics: does any source line strip VM_PROT_WRITE above the apex?

**Status:** static (no device, no build). Reads done on the working tree
`external/xnu-hd2-darwin13/xnu/` at HEAD (`dfc1904`). Companion to `docs/1012-design.md` (§3 lists
the RetryFault block + the codesign early-return as "the live candidates"). **This report REFUTES
both by value and moves the verdict to (c).**

---

## 0. Verdict (read this first)

**No source line strips VM_PROT_WRITE off `prot` on the fault_type = 0x3 (WRITE) path.** On that
path `prot` carries 0x3 all the way to `PMAP_ENTER_OPTIONS`, and `leo_pte_build` therefore builds a
**writable** leaf. So the design's case **(b) is FALSIFIED by source reading** — and the measured
`vmfret = 0` (KERN_SUCCESS) in `records/1011-press-read.txt` independently falsifies it too, because
both callers of `vm_fault_enter` RETURN its error verbatim (§1.3), so a strip site that also skipped
the PMAP_ENTER (the codesign early-return, the design's §3.2) would have returned 50, not 0.

**The verdict is (c): the write reaches the apex.** The loss is below `pmap_enter_options` — either
inside `leo_enter`/`leo_pte_build`, or (prime) an identity/flush fault: the apex writes a leaf into a
table the MMU does not walk, and `pmap_flush_tlbs` (`pmap.c:1275`) then SKIPS the local invalidation
for that pmap. §5 gives the mechanism and its one-cell falsifier, which is exactly the cell the 1012
press already carries (`xnu_live_abt_pmppmap` vs `xnu_live_abt_pmpttbr`).

Confidence: **high** on "no strip" (it is a complete, by-value sweep, §2). **Medium** on which (c)
sub-mechanism; §5 names the deciding measurement.

---

## 1. Control-flow trace for fault_type = 0x3, apex reached

### 1.1 Entry

`trap.c:657-661` is the only caller that matters:

    657   code =
    658       vm_fault(map, vm_map_trunc_page(dfar),
    659                (dfsr & 0x800) ? (VM_PROT_READ | VM_PROT_WRITE)
    660                : (VM_PROT_READ), FALSE, THREAD_UNINT, NULL,
    661                vm_map_trunc_page(0));

fsr = 0x80f ⇒ bit 0x800 set ⇒ `fault_type = 0x3`; `change_wiring = FALSE` (6th arg);
`caller_pmap = NULL` (7th arg). So inside `vm_fault` every `caller_pmap` branch is dead.

### 1.2 vm_fault, first lookup

    vm_fault.c:2808   if (fault_type & VM_PROT_WRITE) object_lock_type = OBJECT_LOCK_EXCLUSIVE;
    vm_fault.c:2847   fault_type = original_fault_type;         /* 0x3 */
    vm_fault.c:2855   kr = vm_map_lookup_locked(&map, vaddr, fault_type, object_lock_type,
    vm_fault.c:2856                     &version, &object, &offset, &prot, &wired, ...);

`vm_map_lookup_locked` sets `out_prot` at `vm_map.c:9167 *out_prot = prot` from
`vm_map.c:9061 prot = entry->protection`. The only demotions before that are
`vm_map.c:9067` (nx OVERRIDE — adds EXECUTE, never removes WRITE),
`vm_map.c:9070` (`mask_protections` — FALSE here: `trap.c` passes no `VM_PROT_IS_MASK`), and
`vm_map.c:9139 prot &= (~VM_PROT_WRITE)` (needs_copy, READ arm — §2 refutes). 1011 MEASURED
`vmeprot = 0x3` here, confirming `prot = 0x3`.

`wired` is FALSE (the entry is not wired), so `vm_fault.c:2876-2894` (`fault_type = prot | WRITE`)
does NOT fire. `fault_type` stays 0x3.

### 1.3 The two callers of vm_fault_enter BOTH propagate its error

This is the load-bearing fact for the "no strip site skipped the enter" argument.

Slow path, `vm_fault.c:3935-3956`:

    3935   kr = vm_fault_enter(m, pmap, vaddr, prot, fault_type, ...);
    3947   if (kr != KERN_SUCCESS) {
    3948       /* abort this page fault */
    ...
    3955       goto done;

Fast path (`FastPmapEnter`), `vm_fault.c:3297-3364`:

    3297   kr = vm_fault_enter(m, pmap, vaddr, prot, fault_type, ...);
    ...
    3364   goto done;

and `done` (`vm_fault.c:4078-4091`) returns the local `kr`. On the fast path there is **no `kr`
assignment between 3297 and `goto done` at 3364**; `done` therefore returns exactly what
`vm_fault_enter` returned. `vm_fault_enter` returns KERN_CODESIGN_ERROR (50) at `vm_fault.c:2443`
and at the taint-reject arm `vm_fault.c:2495`. **Since 1011 measured `vmfret = 0` at the storm
ordinal, neither of those returns was taken** — `vm_fault_enter` ran through to
`vm_fault.c:2590 PMAP_ENTER_OPTIONS(...)` and returned the accept-value `kr = KERN_SUCCESS`
(`vm_fault.c:2499`, returned at `:2742`). **The codesign early-return (design §3.2) is thereby
FALSIFIED — it cannot have skipped the enter.**

Also worth stating: `vm_fault_enter`'s other early exit, `vm_fault.c:2355 return KERN_SUCCESS` (guard
page `m->phys_page == vm_page_guard_addr`), WOULD return 0 — but it sits before the PMAP_ENTER and
would leave the page unmapped; it is a guard-page-only case and not the shape of a shared-cache
store.

### 1.4 Reaching the fast path vs the slow path (both reach the apex with prot = 0x3)

`vm_fault.c:2940` `if (object->copy_strategy == MEMORY_OBJECT_COPY_DELAY && object->copy != NULL &&
(fault_type & VM_PROT_WRITE)) goto handle_copy_delay;` — only if the top object has a COPY_DELAY
copy object (COW). A dyld `__TEXT` file mapping is shared and has no copy object, so this is not
taken.

Then the fast loop `vm_fault.c:2946`: `m = vm_page_lookup(cur_object, cur_offset)`.

* **Page NOT resident (first fault):** the loop breaks and the SLOW path runs
  (`vm_fault.c:3683 kr = vm_fault_page(object, offset, fault_type, ..., &prot, ...)`). Inside
  `vm_fault_page` the ONLY writes to `*protection` are `:1893` and `:1925`, both under
  `(fault_type & VM_PROT_WRITE) == 0` (§2). `*protection` returns 0x3.
* **Page resident (every later fault):** `vm_fault.c:3241 if (object == cur_object &&
  object->copy == VM_OBJECT_NULL) goto FastPmapEnter;` — `prot` is the untouched 0x3.

Either way `vm_fault_enter(m, pmap, vaddr, prot=0x3, fault_type=0x3, ...)` is called and reaches

    vm_fault.c:2590   PMAP_ENTER_OPTIONS(pmap, vaddr, m, prot, fault_type, 0,
    vm_fault.c:2591                     wired, PMAP_OPTIONS_NOWAIT, pe_result);

with `prot = 0x3` (the sole intervening write is `vm_fault.c:2578 prot &= ~VM_PROT_EXECUTE`, which
does not touch bit 0x2).

### 1.5 Apex

`osfmk/vm/pmap.h:399-414` expands `PMAP_ENTER_OPTIONS` to `pmap_enter_options(__pmap,
va, __page->phys_page, protection, fault_type, flags, wired, options)`.
`pmap.c:3179-3182`:

    3179 kern_return_t pmap_enter_options(pmap_t pmap, vm_map_offset_t va, ppnum_t pa, vm_prot_t prot, ...)
    3180 {
    3181 #if BOARD_CONFIG_QSD8250_LEO
    3182     return leo_enter(pmap,va,pa,prot,wired);

`leo_enter` (`pmap.c:1649-1682`) rebuilds the leaf unconditionally:
`*pte = leo_pte_build(map,va,pn,prot,wired)` at `pmap.c:1668` and `pmap.c:1680`, and
`leo_pte_build` (`pmap.c:1639-1645`) calls `pmap_vm_prot_to_page_flags` (`pmap.c:1095-1121`), which
at `pmap.c:1110-1113` clears the APX (bit 9) iff `prot & VM_PROT_WRITE`. `prot = 0x3` ⇒ **writable
leaf**. Every `leo_enter` exit flushes: `pmap.c:1661/1669/1673/1681 pmap_flush_tlbs(map,va,va+PAGE_SIZE)`.

**Therefore, by value, a prot of 0x3 at the apex MUST produce a writable leaf in `map`'s L1.** The
storm contradicts this ⇒ the leaf is built in the wrong table, or the flush does not reach the MMU.
That is (c).

---

## 2. Complete sweep of every place `prot` / `*protection` / `*out_prot` is written

`grep -nE 'prot *=|prot &=|prot \|='` over `vm_fault.c` and `vm_map.c`, each read at its guard:

| site | text | gate | fires for fault_type=0x3? |
|---|---|---|---|
| `vm_fault.c:1893` | `*protection &= (~VM_PROT_WRITE);` | inside `if (object != first_object) {` `if (fault_type & VM_PROT_WRITE) {...}` **`else`** (`:1887`) | **NO** — else arm needs `(fault_type & WRITE)==0` |
| `vm_fault.c:1925` | `*protection &= ~VM_PROT_WRITE;` | `vm_fault.c:1920 if ((fault_type & VM_PROT_WRITE) == 0) {` | **NO** |
| `vm_fault.c:2374` | `prot &= ~VM_PROT_WRITE;` | `vm_fault.c:2362 } else if ((fault_type & VM_PROT_WRITE) == 0) {` | **NO** |
| `vm_fault.c:2578` | `prot &= ~VM_PROT_EXECUTE;` | not WRITE | n/a (bit 0x4) |
| `vm_fault.c:3874` | `prot &= retry_prot;` | `vm_fault.c:3777 if ((map != original_map) \|\| !vm_map_verify(map,&version))` | only on a retry, see §3 |
| `vm_fault.c:3888` | `prot &= ~VM_PROT_WRITE;` | `vm_fault.c:3879 if (m->object->copy != old_copy_object)` | only on a retry, §3 |
| `vm_map.c:9061` | `prot = entry->protection;` | always | sets 0x3 (the source of the write) |
| `vm_map.c:9067` | `prot \|= VM_PROT_EXECUTE;` | `override_nx && prot` | ADDS, never strips |
| `vm_map.c:9139` | `prot &= (~VM_PROT_WRITE);` | `vm_map.c:9103 if (entry->needs_copy) {` **`else`** at `:9133` | **NO** — the write arm is `:9112 if ((fault_type & VM_PROT_WRITE) \|\| *wired){ vm_object_shadow(...); entry->needs_copy = FALSE; }`; the strip is the READ arm |
| `vm_map.c:9167` | `*out_prot = prot;` | always | publishes 0x3 |

No other `prot`/`protection` write exists in the fault path (the remaining hits — `vm_map.c:3672,
7906, 8355, 8609, 8940, 11226, 11313`, `vm_user.c:2368`, `vm_pageout.c:4428`, `vm_object.c:3623/3709`,
`kern_mman.c:813` — are map-enter/pageout/`mprotect` paths, not the fault path, except the submap COW
case `vm_map.c:8940`, whose reachability is addressed in §4).

**Result: on the WRITE path nothing strips WRITE.**

---

## 3. The RetryFault block (`vm_fault.c:3762-3913`) — DECIDED, and refuted as the fixed point

**Entry condition.** `vm_fault.c:3777`:

    3777   if ((map != original_map) || !vm_map_verify(map, &version)) {

`original_map` is set once at `vm_fault.c:2801 real_map = map; vm_map_t original_map = map;` and
`map` is only ever reassigned back to `original_map`/`map` inside this block. So the block is entered
**only** when the lookup redirected into a submap (`map != original_map`, set in
`vm_map_lookup_locked` at `vm_map.c:8805/8816/8823` `*var_map = entry->object.sub_map` and
`vm_map.c:8974` `map = cow_sub_map_parent`) **or** when the map's timestamp changed while the object
lock was dropped (`!vm_map_verify`).

**If entered:**

    3794   kr = vm_map_lookup_locked(&map, vaddr,
    3795             fault_type & ~VM_PROT_WRITE,            /* READ only */
    3796             OBJECT_LOCK_EXCLUSIVE, &version, ... &retry_prot, ...);
    ...
    3874   prot &= retry_prot;

`retry_prot` is a READ-only lookup's `out_prot`: with `fault_type = 0x1`, `vm_map.c:9112` is FALSE so
the needs_copy arm runs `:9139 prot &= ~VM_PROT_WRITE` unconditionally — `retry_prot` can carry only
READ. Then `3874 prot &= retry_prot` **strips WRITE** off `prot`, and `vm_fault_enter` builds a
READ-only leaf from it. `3888 prot &= ~VM_PROT_WRITE` is a direct strip when
`m->object->copy != old_copy_object`.

**Why it is NOT the storm's fixed point.** The block lives ONLY on the slow path — after `vm_fault_page`
at `:3683`, before `vm_fault_enter` at `:3935`. It is never reached on the fast path (the fast path
`goto done`s from `:3364`, before `:3762`). Now trace two consecutive faults of the storm:

* Fault #N (page becomes resident): slow path; if the map changed, `prot` is stripped to READ,
  `vm_fault_enter` writes a READ-only leaf, the store re-faults.
* Fault #N+1: the page is now resident, so the fast loop takes `vm_fault.c:3241 goto FastPmapEnter`
  with the freshly re-looked-up `prot`... which is again 0x3 (the first lookup of THIS fault, not
  the retry's). It writes a WRITABLE leaf. The storm would end.

A one-shot retry-strip therefore **cannot** sustain 2.1M identical re-faults. For the storm to be a
fixed point the leaf must be wrong on EVERY fault, i.e. the fast path (which reaches the apex with
prot=0x3 and builds a writable leaf) must ALSO be failing to make the MMU see the write. Same
conclusion as §1: (c). (The retry block remains a real candidate for a *one-off* extra fault, not
for the storm.)

*Falsifier for §3:* if a future capture shows the faulting thread's `map` carrying a `use_pmap`
submap at VA 0x61000 (`vm_map.c:8790`, needs the `VM_MEMORY_SHARED_PMAP` alias — §4), the `map !=
original_map` entry can be permanent, and this block becomes live again. The 1012 press's
`_pmppmap` vs `_pmpttbr` cell decides whether the apex pmap is the submap's or the thread's.

---

## 4. Reachability of the submap COW strip (`vm_map.c:8940`) for VA 0x61000

`vm_map.c:8940 prot = submap_entry->protection & ~VM_PROT_WRITE;` sits inside the submap COW arm
`vm_map.c:8880 if (((fault_type & VM_PROT_WRITE) && cow_sub_map_parent))`. It leads to the
`entry = submap_entry` / `prot = entry->protection` line at `:9061` only in that arm, and the arm
requires a nested **needs_copy** `use_pmap` submap. `use_pmap` is set only when
`alias == VM_MEMORY_SHARED_PMAP` (`vm_map.c:2065`, set by `vm_shared_region.c:1326/1840`). That alias
is applied to the **commpage** (`vm_shared_region.c:1837-1840`) and to the shared-region base; the
ARM shared-region base is `SHARED_REGION_BASE_ARM = 0x30000000` (`shared_region.h:67`), NOT a low VA.
VA 0x61000 is a low user VA (a Mach-O slice offset), so a submap at that VA is not expected — this
strip is **not** on the storm's path, matching 1011's own refutation. (It is, however, the reason the
`map != original_map` retry entry exists at all — §3.)

---

## 5. What the storm's fixed point actually is — (c), and its falsifier

Two by-value mechanisms remain. Both are "(c)" and both are decided by ONE measurement the 1012
press already carries.

### (c1) wrong-table identity + suppressed flush  — PRIMARY

If `real_map->pmap` (the pmap reached by `vm_fault.c:2865 pmap = real_map->pmap;`, where
`real_map = map = the top map`) is NOT the pmap whose base is in TTBR0, then:

* `leo_enter` → `pmap_pte(map, va)` (`pmap.c:1655`) writes a leaf into `map->pm_l1_virt` — a table
  the MMU does not walk. `leo_enter` returns `KERN_SUCCESS` (it built a perfectly valid writable
  leaf, in the wrong L1).
* `pmap_flush_tlbs` (`pmap.c:1275`) then finds no CPU to flush:

      1292   for (cpu = 0, cpu_bit = 1; cpu < real_ncpus; cpu++, cpu_bit <<= 1) {
      1293       if (!cpu_datap(cpu)->cpu_running) continue;
      1295       uint32_t ttbr_pmap = armreg_ttbr_read() & 0xFFFFFF00;
      1297       /* Current pmap is active, flush it. */
      1298       if ((ttb == ttbr_pmap) || (pmap_is_shared)) {

  With `ttb = pmap->pm_l1_phys` (`:1280`) ≠ `ttbr_pmap` and a non-shared user pmap, `flush_self`
  stays FALSE and **no local TLB invalidation happens**. The `:1325` panic
  ("pmap == kernel_pmap && flush_self != TRUE") does NOT fire because `pmap != kernel_pmap`.

Result: every fault rebuilds a correct writable leaf in a table the MMU ignores, flushes nothing, and
returns KERN_SUCCESS. The store re-faults with the identical pc/far/fsr — a perfect 2.1M fixed point,
and exactly the measured `vmfret = 0`.

The 1011/1010 capture is consistent: `xnu_live_abt_pteu = 0`, `ptel2 = 0` — the debugger-side walk
found no valid leaf, and 1011 §3 showed the walk's base mask was wrong so it could not be trusted
either way.

*Deciding cell (already built):* `xnu_live_abt_pmppmap` (`pmap->pm_l1_phys`, offset 0 of struct
pmap, read at the apex) vs `xnu_live_abt_pmpttbr` (live `armreg_ttbr_read()`), both masked
`& 0xFFFFFF00`. EQUAL ⇒ (c1) false, fall to (c2). DIFFERENT ⇒ **(c1) confirmed: an identity fault.**

### (c2) ASID-limited flush misses a stale entry

ASID is enabled in this boot (`capture:4003 All ASID/asids enabled: real_ncpus: 1,
pmap_asid_ncpus: 1`), so `pmap_flush_tlbs` takes the `pmap.c:1314-1319` branch:

    1314       if (pmap_asid_ncpus) {
    1315           pmap_asid_validate_cpu(pmap, my_cpu);
    1316           if (pmap_is_shared) arm_tlb_flushID();
    1317           else arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);

For a non-shared user pmap the flush is ASID-scoped to `pmap->pm_asid`. If the ASID in the CPU (set by
`pmap_asid_activate`, `pmap_asid.c:296 tpmap->pm_asid = new_asid`) differs from `pmap->pm_asid` at
flush time, the stale read-only translation survives and the fault repeats. This is secondary
because `pmap_asid_validate_cpu` also clears the coherency bit.

*Falsifier for (c2):* the same `_pmppmap` vs `_pmpttbr` row that decides (c1); if they are EQUAL,
(c1) is out and (c2)/(a leaf built from a prot other than 0x3 inside `leo_enter`) is the remaining
space.

### What the source decisively rules OUT

* **(b) any strip above the apex** — §2 is exhaustive; §1.3 kills the "skipped the enter"
  sub-case via the measured `vmfret = 0`.
* **The codesign early-return** (`vm_fault.c:2443`, design §3.2) — returns 50, and `vmfret = 0`.
* **The taint-reject arm** (`vm_fault.c:2495`) — returns 50; `vmfret = 0`.
* **The RetryFault strips** (`vm_fault.c:3874/3888`) as the *fixed point* — §3 (slow-path only,
  one-shot).
* **`vm_map.c:9139`, `:8940`, `vm_fault.c:1893/1925/2374`** — all gated to the READ arm (§2, §4).

---

## 6. The single-cell question this hands the 1012 press

The press already publishes the row that decides (c1) vs (c2): at the storm tail (ordinal >= 0x400),

    xnu_live_abt_pmpprot   == 0x3        (the apex received WRITE — consistent with §1; this is NOT news)
    xnu_live_abt_pmppmap   & 0xFFFFFF00  vs  xnu_live_abt_pmpttbr & 0xFFFFFF00
        EQUAL     => (c2) or leo_enter internals: read pmap.c:1639-1682 by value with prot=0x3
        DIFFERENT => (c1) identity fault: the apex writes another L1 and pmap_flush_tlbs:1298 skips
                     the flush. Fix locus: whatever makes real_map->pmap != the TTBR0 pmap.

The 1012 design's table lists `_pmpprot & WRITE == 0 => (b)`. **This report predicts the tail
`_pmpprot` will read 0x3 (i.e. the (b) row will NOT fire)** — `_pmpprot` carrying WRITE is the
expected outcome, not a negative result, because the strip sites it was meant to catch cannot fire.
The DISCRIMINATOR this run's analysis says to read is the `_pmppmap` vs `_pmpttbr` pair, not
`_pmpprot`.

---

## 7. Honest scope

* All line numbers are the working tree at `dfc1904`; the tree carries the 1012 WRSTRIP probes
  (`vm_fault.c:2586-2588, 3870-3872, 3884-3886`, `1888-1898`, `1919-1926`), which are pure
  observation and change no value.
* The trace assumes the entry at VA 0x61000 maps a dyld `__TEXT` vnode object with no copy object
  (fast path) and no submap (no `VM_MEMORY_SHARED_PMAP` at a low VA). Both are measurement-supplied
  by 1011; if either is wrong, §3/§4 reopen.
* (c1) vs (c2) is NOT decided by this static read — it needs the `_pmppmap`/`_pmpttbr` row. This
  report's contribution is narrowing the apex question to that one pair and REFUTING the strip hunt.