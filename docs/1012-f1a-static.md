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
* **§8 corrects §5.** §5's arithmetic (`pte_offset` vs walk mask) assumed `leo_enter` builds the leaf.
  §8 proves by value that the D13 build compiles the STOCK body, not `leo_enter`, and that the walk
  reads a DIFFERENT L2 table than the writer writes.

---

## 8. PRESS 1012 RESOLUTION — the contradiction decided (all five candidates)

**Capture:** `out/stage90/captures/recovered-unknown-arm-20261015-020222-last_kmsg.txt`.
Tail ordinal 0x100000 (`far=0x2e000`, `pc=0x2be1e1a0`, `fsr=0x0000080f`, USER). The apex rows
(22 of them) read `_pmpprot=0x3`, `_pmppmap=0x81a86000`, `_pmpttbr=0x81a8605b`; the walk rows read
`_ptel1=0x81bb1001`, `_ptel2=0`, `_pteu=0`. Counters: `_pcn=_pmpn=0x100000` (both ladders agree the
abort count EQUALS the enter count), `_ptenx=0x2b3` (691 distinct L1/L2 descriptor pairs seen).

### 8.1 The apex VA is the storm VA — **#2 REFUTED (not a measurement artifact)**

The probe is keyed on the abort's own `far_`, and it is re-armed every abort, not latched:

* `entry_trace.c:5788` — `entry_note_abt_pte(far_);` runs inside `__wrap_sleh_abort` for THIS abort
  (`far_` read from cp15 at `:5688/:5695`, `type`-selected).
* `entry_stubs.c:2451` — `g_abtpte_pmpva = va;` is an unconditional store (every abort overwrites it),
  NOT a latch-once. `entry_stubs.c:2425` — `n = ++g_abtpte_n;` is monotone across aborts
  (measured `_ptenx=0x2b3` over the run ⇒ many distinct VAs entered the walk).
* `entry_stubs.c:2647` — `if (g_abtpte_pmpva == 0u || va != g_abtpte_pmpva) return;`
* `entry_stubs.c:2711` (WRSTRIP) — the same reject.

So a recorded `_pmpprot` row can only be the enter for the storm's own VA. The publish gate did not
mis-identify the VA; the apex row is a genuine match on `far=0x2e000`. **#2 is not the answer.**

### 8.2 The writer's index math and the reader's index math are IDENTICAL — #1 is not an index slip

For `va = 0x2e000`, by value:

| step | writer (stock `pmap.c`) | walker (`entry_stubs.c`) | value |
|---|---|---|---|
| L1 index | `tte_offset(va)` = `((va>>20)&0xfff)<<2` (`pmap.h:198`) | `((va>>20)&0xfff)<<2` (`:2306`) | 0 |
| L1 → L2 base | `L1_PTE_ADDR(tte)` = `tte & 0xfffffc00` (`pmap.h:202/205`, `pmap.c:1817`) | `l1e & ABTPTE_L1_PTE_MASK` = `& 0xfffffc00` (`:2323`) | 0x81bb1000 |
| L2 slot | `pte_offset(va)` = `((va & ~0xfff00000)>>12)<<2` (`pmap.h:199`) | `((va>>12)&0xff)<<2` (`:2327`) | 0xb8 |

`~L1_SECT_ADDR_MASK` with `L1_SECT_ADDR_MASK = 0xfff00000` (`pmap.h:203`) is `~0xfff00000`, whose low
20 bits equal `(va>>12)&0xff` for `va<0x100000`. So both address L2 table word `0x81bb10b8`. **The
who-writes-which-slot arithmetic is not the fault.** The remaining #1 shape is a BASE/identity fault,
and the L1 base is ruled out below.

### 8.3 The apex really is the LEO-dispatch input (D13 runs the STOCK body) — CORRECTION to §5

§5 and `docs/1012-design.md` assumed `pmap_enter_options` dispatches to `leo_enter`. It does not, in
this build:

* `pmap.c:3181` — `#if BOARD_CONFIG_QSD8250_LEO` (bare `#if`; an undefined macro is `0`).
* `tools/build_xnu_arm_kernel.sh:202` — `COMP_DEFINES_EXTRA=(-DBOARD_CONFIG_MSM8974_CANCRO=1)`; the
  D13 build defines MSM8974/CANCRO only. `BOARD_CONFIG_QSD8250_LEO` is never defined.
* MEASURED: `nm out/xnu_kernel_obj_d13/osfmk_arm_pmap.o | grep -ci leo` → **0**; `pmap_enter_options`
  is defined at `0x17a8` (the stock body). `leo_enter`/`leo_pte_build`/`pmap_vm_prot_to_page_flags`
  are dead code on D13; §5's "`leo_pte_build` rebuilds the leaf unconditionally" reasoning is void.

The stock body's leaf writes are BOTH correct w.r.t. `prot`:
* in-place path `pmap.c:3253-3254` → `template_pte |= pmap_vm_prot_to_page_flags(pmap, prot, …)`,
  written `pmap.c:3280` `*(uint32_t *) pte = template_pte;`;
* new-mapping path `pmap.c:3442-3443` → written `pmap.c:3460` `*(uint32_t *) pte = template_pte;`.
* `pmap_vm_prot_to_page_flags` `pmap.c:1110-1113`: `if (prot & VM_PROT_WRITE) pte &= ~(L2_ACCESS_APX);`
  — `prot=0x3` clears APX ⇒ a writable leaf. Both paths reach `pmap_flush_tlbs(pmap, va, va+PAGE_SIZE)`
  (`pmap.c:3473`, and the in-place path `goto enter_options_done` also lands on 3473).

### 8.4 The apex writes to a DIFFERENT L2 table than the MMU walks — **#1 CONFIRMED (base/identity)**

The two L1 descriptors, read at the SAME ordinal, are different objects:

* `_pmppmap = 0x81a86000` — the pmap's own L1 (`pm_l1_phys` = **offset 0** of `struct pmap`,
  `pmap.h:99`, read in `entry_trace.c:5921`).
* `_ptettbr = 0x81a8605b & 0xfffff000 = 0x81a86000` — the live TTBR0 the MMU walks.
* These are EQUAL ⇒ at the L1 level the pmap and the MMU agree. **#1's L1-base form is REFUTED.**
* `_ptel1 = 0x81bb1001` (`ABTPTE_L1_TYPE_MASK=0x3`, `:2146`) → `bits[1:0]=01` = COARSE, pointing to L2
  table base `0x81bb1000`. The walk then reads `0x81bb10b8` and gets `_ptel2 = 0` — an ABSENT leaf,
  not a read-only one.

The pmap's `pte` local for this VA is NOT `0x81bb10b8`, and the arithmetic forbids it being an index
slip (§8.2). It must be a different L2 table — i.e. a **base/identity fault inside `pmap_pte`: the
`pte` the writer holds and the `pte` the MMU walks resolve to different PAs.** The two macros differ by
exactly the shared L1 base:

* `pmap.c:1817` — `pte = L1_PTE_ADDR(tte);` then `:1824` — `ptep = (uint32_t *)((phys_to_virt(pte) + pte_offset(virt)));`
  where `phys_to_virt(p) = (p - gPhysBase) + gVirtBase` (`pmap.h:193`). The mapping is a function of the
  pmap's OWN `gPhysBase`/`gVirtBase`.
* The walk computes `l2base = l1e & 0xfffffc00` and dereferences it as a PHYSICAL address directly
  (`entry_stubs.c:2327` `l2p = (uintptr_t)l2base + …`; `:2337` `*(const volatile uint32_t *)l2p`), with
  no `phys_to_virt`.

`_ptel1` proves the walk's TTBR0 (per §8.4 EQUAL to the pmap's flat address) carries the coarse entry
`0x81bb1001`; the writer read the same flat word for its `tte`, so `L1_PTE_ADDR(tte)` = `0x81bb1000` =
`l2base`. The writer therefore aims at `phys_to_virt(0x81bb1000) + 0xb8` while the walk reads
`0x81bb10b8` physical. With `gVirtBase>0` (the kernel is entered high — `high_vmstate…base=0x80000000`,
capture line 674) these are different physical words. **Fix locus: the L2 table the pmap's
`phys_to_virt`-mapped PTE resolves to must be the one whose physical address is in the walked L1 entry
— i.e. the ARM pmap's `phys_to_virt`/window base for L2 tables (`gPhysBase`/`gVirtBase`) does not match
the address domain the MMU's TTBR0 tables live in.** The walk's own reading is the confirmation: at
`far=0x2e000` it dereferences the physical leaf `0x81bb10b8` and gets `_ptel2=0` — no leaf is there for
the writer's `prot=0x3` enter to have produced.

### 8.5 #4 REFUTED — no second writer, no reprotect

Every L2-leaf writer in `pmap.c` is accounted for and none clears the storm's leaf:

* `:3280` (in-place) and `:3460` (EnterPte) — these ARE the enter, both build a writable leaf from
  `prot=0x3` (§8.3).
* `:3308` `*(uint32_t *) pte = 0;` — the *unmap-the-old-PA* arm, guarded by `:3304`
  `if ((old_pte & L2_ADDR_MASK) != 0)`. In the storm `old_pte` is 0 in the walked table, so it does not
  fire (and it would zero the OLD PA's slot, not a read-only the writer just made).
* `:3660` `*pte = dpte;` — `pmap_bootstrap`'s kernel-text NX/RO pass, once, never on a user pmap.
* `pmap_protect` (`:4225`) and `pmap_page_protect` (`:3943`, and `pmap_disconnect` `:4492` → it) call
  `pmap_pte`, and if that returns 0 they **PANIC** — `:4007-4008`
  `if (0 == pte) panic("pmap_page_protect(): null PTE pmap=%p …");` and the range check `:4293-4294`
  `panic("pmap_protect: attempting to protect bogus PTE range")`. No panic in the capture (resident
  path, "No errors detected"), so no reprotect on a bogus slot fired. A reprotect via a DIFFERENT
  (writer-side) table would leave the walked slot's OLD value untouched — but there IS no old value in
  the walked slot, so #4 cannot be the cause of `_ptel2=0`. **#4 is not the answer.**

### 8.6 #5 REFUTED

`_ptel2 = 0` is not "APX set" — the raw descriptor is zero (`L2_SMALL_PAGE=0x2` absent), so the leaf
was never written, not written read-only. APX aliasing cannot produce a zero leaf.

### 8.7 Verdict

**#1, in its base/identity form.** The apex receives `prot=0x3` for the storm's own VA (#2 out), the
index arithmetic is identical (#1-index out), no second writer fires (#4 out), and no bit aliasing is
possible (`#5` out) — yet the walked L1 entry (`0x81bb1001`) points at an L2 table whose leaf is 0
while the pmap writes the leaf through `phys_to_virt`. The write lands in a DIFFERENT physical L2 word
than the MMU walks: the pmap's `phys_to_virt`-resolved PTE and the TTBR0-walked PTE disagree on the L2
table's physical identity. The confirming measurement is the walk's own `far=0x2e000` row: the leaf
physical `0x81bb10b8` reads 0 (`_ptel2=0`) while the writer entered PA `_pmppa=0x8421a` through
`phys_to_virt`. **Deciding lines: `pmap.c:1817/:1824` vs `entry_stubs.c:2323/:2327/:2337`; the
discriminating cell `_ptel1=0x81bb1001` + `_ptel2=0` at `_pcn=_pmpn=0x100000`.** A follow-on rung
(the leaf-value read, `docs/1013-design.md` if written) would convert this from "the walk's slot is
empty where the writer would write" to the writer's exact target PA — it is not needed for this verdict.