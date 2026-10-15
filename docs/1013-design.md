# 1013 — THE LEAF-VALUE WITNESS: what leaf does the pmap ACTUALLY write?

**Status:** design + code staged, not built/pressed. Predecessor: `records/1012-press-read.txt`
(arm `armed-d13-ffdc8ba1`). **Pure measurement — 1013 changes no kernel behaviour.**

---

## 0. The frontier 1013 walks into, and the one question it answers

1012 MEASURED the *input* to the leaf builder and the table *identity*:

    xnu_live_abt_pmpprot = 0x00000003   (READ|WRITE arrives at pmap_enter_options)
    xnu_live_abt_pmppmap = 0x81a86000   (the pmap's own pm_l1_phys, struct pmap offset 0)
    xnu_live_abt_pmpttbr = 0x81a8605b   (the LIVE TTBR0; low byte 0x5b = the cpufunc-v7 attribute)
    -> pmppmap & 0xFFFFFF00 == pmpttbr & 0xFFFFFF00        (the pmap writes the table the MMU walks)

and `docs/1012-upstream-diff.md` §4 added the second, decisive by-value fact:

> `pmap_enter_options` dispatches to `leo_enter` **only** under
> `#if BOARD_CONFIG_QSD8250_LEO` (`osfmk/arm/pmap.c:3181`). The D13 kernel is built with
> `-DBOARD_CONFIG_MSM8974_CANCRO=1` and **nothing in the tree, the build, or the per-file define
> derivation ever defines `BOARD_CONFIG_QSD8250_LEO`**. `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`
> carries **zero** `leo_*` symbols. D13 runs Apple's stock `pmap_enter_options` body.

So every step is *correct by value* — `prot` carries WRITE, the source is upstream-identical, the
table base agrees, and `pmap_flush_tlbs` flushes — yet the USER store permission-faults
(DFSR=0x0000080f, FS=15 level-2, WnR=1) ~2.1 M times and the machine freezes.

**The unknown that decides it: what leaf value does the pmap ACTUALLY write on device, and does the
MMU's own walk find that same location?** 1012 measured the input. 1013 measures the OUTPUT.

---

## 1. The three probes

Three probes plus one key-setter. All four are pure observation (they read, publish, and change
nothing), gated by a NEW switch
`STAGE90_XNU_PTE_WITNESS` (default 0). It carries the full 1010 ladder (ABT_FAR/ABT_PC/ABT_PTE) and
PMP_ENTER, exactly as 1012 does; **it does NOT require VME_PROT** (a small relaxation of 1012's set,
stated so OFF/ON diffs are readable).

### (W-APEX) — inside `__wrap_pmap_enter_options`, AFTER `__real_...` returns

`src/entry/entry_trace.c`. The wrapper 1012 added becomes (still a pure pass-through: `__real_...`
into a NAMED result first, act after, `return kr`):

    uint32_t kr = __real_pmap_enter_options(pmap, va, pa, prot, fault_type, flags, wired, options);
    entry_note_abt_pmp(va, prot, pa, pm_l1_phys);                 /* 1012, when PMP_ENTER=1 */
    entry_note_abt_wleaf(va, pmap, pa, fault_type);               /* 1013 W-APEX, when PTE_WITNESS=1 */
    return kr;

**W-APEX takes the PMAP, not the pre-call `pm_l1_phys` copy, and re-reads `*pmap` inside the
recorder.** `pmap_expand` → `pmap_expand_ttb` can REPLACE the L1 table and change `pm_l1_phys` *inside*
`pmap_enter_options` (`pmap.c:3084-3086`), and the enter then writes the leaf into the NEW table; a
read-back that reused the wrapper's pre-call local would read the RETIRED table and manufacture a
false reading (`docs/1013-leaf-readback-verify.md` §0.2, §6.2). (For the storm VA `far=0x2e000` this
hazard cannot fire — `tte_offset = 0`, so `pmap_expand_ttb` returns immediately, `pmap.c:3044` — but
the wrapper serves every VA, so the re-read is required for the cell to be correct in general.)

The recorder (`entry_stubs.c`) walks **the pmap's own table** from `pm_l1_phys` — the same base
Apple's `pmap_pte()` walks, with the same index math (`pmap.c:1786-1824`; `tte_offset`/`pte_offset`,
`pmap.h:198-199`) — and publishes:

| key | value |
|---|---|
| `xnu_live_abt_wwn` | the ladder ordinal (`n <= 4 \|\| power-of-two`, the SAME rule (A)/(B)/(E′)/[PMP] use) |
| `xnu_live_abt_wwl1` | the raw L1 descriptor at `pm_l1_phys + (((va>>20)&0xfff)<<2)` |
| `xnu_live_abt_wwl2pa` | the L2 TABLE base the L1 descriptor points at (`l1 & 0xfffffc00`) — the **identity** cell |
| `xnu_live_abt_wwleaf` | the raw leaf at `l2pa + (((va&0xfff)>>12)<<2)` — **THE CELL** |
| `xnu_live_abt_wwapx` | the decoded APX bit of that leaf: 1 = AP[2] set = **user READ-ONLY**, 0 = writable |
| `xnu_live_abt_wwnoproof` | 1 = `pm_l1_phys`'s base is NOT the live TTBR0's base (the V4 honesty cell) |
| `xnu_live_abt_wwva` | **the enter's OWN first argument** — the provenance key (see below) |
| `xnu_live_abt_wwft` | the `fault_type` the enter was called with (`0x3` = READ\|WRITE = the storm's WRITE fault) |
| `xnu_live_abt_wwpatbl` | 1 = the entered page (`pa << 12`) IS the pmap's own L1 table or the L2 table this VA resolves through — i.e. the enter is installing a **page-table page**, not the faulting page |
| `xnu_live_abt_wwpa` / `_wwpaok` | the PA page number the enter was given, and whether the leaf's own address field names it (the adjudication cross-check) |

and the change-count `xnu_live_abt_wwnx` (distinct `wwleaf` values, written WHERE IT CHANGES).
The L1 slot and the leaf are each **window-bounded** against `[gPhysBase, gPhysBase + mem_size)`
before they are dereferenced — the same bound the walker uses — and a pointer that leaves the window
is left unread (leaf 0), never dereferenced.

**W-APEX is measured with BOTH OBSERVATIONS, and the L1-only reading is a first-class outcome.**
When `pm_l1_phys` equals the live TTBR0 base (1012's measured case) the L1 slot is directly
readable and the whole probe resolves from `wwn`…`wwapx` alone. When 1012's identity cell were ever
to read *unequal* — the one by-value contradiction 1013 must not walk into silently — the L1 slot is
not the live table and the reader says so in its own words (`wwnoproof`): the value is published as a
NULL CONTROL, never silently re-attributed.

**THE PROVENANCE GATE (the 1012 apex defect W-APEX must not inherit).** A static verification of the
1012 cell (`docs/1012-probe-keying.md` §1.4/§1.6) established two independent defects that together
make 1012's "the write reaches the apex for the faulting VA" reading **unproven**:

1. **The key is written AFTER the handler.** `__wrap_sleh_abort` calls `__real_sleh_abort` and only
   then `entry_note_abt_pte(far_)` sets `g_abtpte_pmpva`. The page-in that settles one abort runs
   `pmap_enter_options` from **inside a later abort's handler**, so 1012's apex filter is still
   holding the *previous* abort's `far`. Under a fixed-VA storm the two coincide — which is why the
   cell looked clean — but coincidence is not identity, and the row cannot reveal which enter it
   observed.
2. **Every 1012 apex row carries the same `pa = 0x8421a` (PA `0x8421a000`)**, the shape of a
   page-table-page install, NOT the storm's own small page. So even under the fixed VA the object
   observed may be a *table page*, and "the write reaches the apex" may be true of the wrong page.

W-APEX therefore keys on the CURRENT fault and publishes its OWN provenance. `entry_note_abt_cur(far_)`
is called from `__wrap_sleh_abort` **before** `__real_sleh_abort`, so `g_abtpte_curva` is an *identity*
with the fault this handler is servicing; `entry_note_abt_wleaf` filters on it (`cur == 0 || va != cur
⇒ return`) rather than on the post-handler `g_abtpte_pmpva`. And each published row **self-identifies**:
`_wwva` is the enter's own first argument, `_wwft` the `fault_type` it was called with, `_wwpa` the
page number, `_wwpatbl` whether that page is a table page. A row is a proof that the apex observed
**the storm's own fault** only when

    wwva == far   AND   wwft == 0x3 (READ|WRITE)   AND   wwpatbl == 0

A row failing any of the three is a **NULL CONTROL**, not a "write reaches the apex" reading; and if
every tail row fails, the verdict is `APEX_OBSERVED_A_TABLE_PAGE` (the 1012 cell's real shape),
`APEX_WRONG_VA`, or `APEX_WRONG_FAULT_TYPE`, with V1–V3 **withheld**. (The gate is item **V0** in
§3, applied before V1–V3, exactly as the control is.)

### (W-WALK) — at the ASYNC abort, walking the LIVE TTBR0 for the storm's `far`

`src/entry/entry_trace.c`'s `__wrap_sleh_abort` already calls `entry_note_abt_pte(far_)` (1010). 1013
adds, right beside it, `entry_note_abt_walk(far_, fsr)` (`entry_stubs.c`), which walks the LIVE TTBR0
(not a saved pmap) and publishes **location tags** plus the leaf:

| key | value |
|---|---|
| `xnu_live_abt_wkn` | the ladder ordinal |
| `xnu_live_abt_wk` | a location tag: **0** = `stage90_read_l2_pte` did not resolve a valid small-page leaf for this VA (out of window / not COARSE / alias refused), **1** = it did |
| `xnu_live_abt_wktb` | the LIVE TTBR0 the walk used (raw; low byte = the attribute) |
| `xnu_live_abt_wkl1` | the raw L1 descriptor the walk read |
| `xnu_live_abt_wkl2pa` | the L2 table PA the L1 descriptor points at |
| `xnu_live_abt_wkleaf` | the raw leaf the MMU walks to |
| `xnu_live_abt_wkleaf2` | the **same L2 word read a SECOND time, after a `dsb`** (the double-read) |
| `xnu_live_abt_wkstable` | 1 = the two reads AGREE (L1 descriptor, L2 base and leaf all identical) ⇒ the value is STABLE and a race with a concurrent table write is dead; 0 ⇒ the table moved between the reads and the leaf is a NULL CONTROL |
| `xnu_live_abt_wkapx` | the APX bit of that leaf (1 = user READ-ONLY) |
| `xnu_live_abt_wkoow` | 1 = a table pointer lay OUTSIDE the window (the walk refused to deref) — from the **FIRST** walk, latched before the double-read re-assigns the global |
| `xnu_live_abt_wkfsr` | the FSR this abort was taken on (the FS=15 / WnR=1 identity, `0x80f` at the 1009 storm) |
| `xnu_live_abt_wkapex` | 1 = this walk's leaf EQUALS the (A) walk's own recorded `xnu_live_abt_ptel2` (the cross-check; see below) |

and the change-count `xnu_live_abt_wknx` (distinct `wkleaf` values, written WHERE IT CHANGES).

**The window-bounding is the project's own, not a re-invention.** `entry_stubs.c` already carries
`stage90_read_l2_pte()` with the `g_abtpte_walk_oow`/`g_abtpte_walk_winbase` asterisks and the
out-of-window L2 alias mechanism (`entry_mmio_section`, the 911c `entry_smem.c` trick). W-WALK
**reuses that exact function** — no second walker is written. That matters because re-spelling a
walker is precisely how 1010 and 1010b each produced a defective reading; the walk is one function
and 1013 is its third caller.

**The (A) walk's `ptel2=0` HARD anomaly in the 1012 capture is handled explicitly, not repeated.**
`entry_note_abt_pte` latches its own resolved leaf into `g_abtpte_ptel2rec` (1013's one source edit
inside the (A) recorder), and W-WALK publishes the cross-check
`xnu_live_abt_wkapex = (tag && leaf is a SMALL PAGE && leaf == ptel2rec ? 1 : 0)` — guarded on the
tag, so "both walks resolved nothing and both read 0" is never published as agreement:
* `wkapex = 1` — the two walks resolved the SAME leaf ⇒ the (A) anomaly was **not** a walk table
  defect, and W-WALK's leaf is trustworthy.
* `wkapex = 0` — they disagree, so **at least one walk is wrong**, and the leaf value is published as
  a null control.
The reader therefore NEVER has to reconcile a disagreement by hand. (The (A) walk and W-WALK resolve
through the same function on the same TTBR0; a disagreement is a *state* difference, i.e. the table
moved between the two reads — a reading in itself.)

**THE DOUBLE-READ — `ptel2=0` disentangled from a race.** The 1012 static verification also
established (`docs/1012-probe-keying.md` §2.3a/§2.4, §3.5) that the (A) walk's tail `ptel2 = 0` is a
**real read of a raw 0x0 word** (the L1 base mask is correct and the apex-mapped table was reached —
3 of 22 rows return valid small-page leaves with `pteu = 1`). But a raw 0x0 at a COARSE L1's L2 slot
**contradicts FS=15**: with APX already deny-user-write, the MMU must resolve the level-2 descriptor
first, so a truly ABSENT one would have produced FS=5/6/7 (`translation fault`/`access flag`), not a
permission fault. A 0 under FS=15 therefore means either (a) the table moved between the read and the
fault, or (b) the walk read a table the MMU does not walk. W-WALK's double-read separates (a): it
re-walks the same VA after a `dsb` and publishes `_wkleaf2` and `_wkstable = (l1b == l1 && l2b == l2
&& ub == u)`. `wkstable = 1` at the tail ⇒ the zero is **STABLE**, a concurrent table write cannot
explain it, and the leaf value is real (which makes (b) the live hypothesis, decided by the V3
location comparison); `wkstable = 0` ⇒ the table moved and the leaf is a NULL CONTROL, never a
verdict. The reader decides by value — it never infers stability.

**The double-read must re-walk with the SAME `allow_alias`.** `stage90_read_l2_pte()` **assigns**
(never ORs) `g_abtpte_walk_oow`, and on this storm's own VA the L2 table is **out of window** (the
1010b/1010c case, PA `0x46300000`). A second read with `allow_alias = 0` would therefore **fail
open** and return `0` while the first returned the real word, so `_wkstable` would be `0` on *every*
storm row — an **instrument artifact that disqualifies the very reading the double-read exists to
validate** ([[mi4-measurement-defects]]). The second walk is therefore called with the same
`onladder` the first was. For the same reason `_wkoow` is **latched into a local before the second
walk** (`oow = g_abtpte_walk_oow;`) and published from that local — otherwise the double-read's own
(reset) reading would be reported as the first walk's out-of-window flag. Same VA, same walk
arguments, one `dsb` between; `_wkoow` describes the walk the other cells describe.

### (W-SYNC) — the CONTROL, at a KNOWN-GOOD mapped page

`entry_note_abt_wsync(va)` (`entry_stubs.c`) walks the live TTBR0 for **the kernel's own managed base
VA**, which the boot has already mapped writable by `l2_cache_to_range`/`l2_map_linear_range`
(`arm_vm_init.c:239`/`174`; note `*ptv |= L2_ACCESS_PRW` and the APX bit is NOT OR'd in):

    va = gPhysBase + 0x00080000          /* 512 KB into the identity/managed window */

chosen as a **byte offset inside `[gPhysBase, gPhysBase + mem_size)`**, where `mem_size` is the
kernel's own window global — the SAME base + length `stage90_read_l2_pte` already bounds its own
loads with (`g_abtpte_walk_winbase = gPhysBase`, `g_abtpte_walk_winlen = mem_size`,
`entry_stubs.c:2302-2303`) — and `0x00080000` is a **byte offset far inside that window**, never the
first ~256 MB a low identity map might leave absent. It is
mapped by the identity section/leaf the boot itself built; it is RAM; no device may answer it; it is
TRUNK (not a boot range or kmem region) inside a MAPPED window.

Publishes `xnu_live_abt_wcnt` (the ordinal), `xnu_live_abt_wcva` (the exact VA read),
`xnu_live_abt_wck`, `xnu_live_abt_wcl1`, `xnu_live_abt_wcl2pa`, `xnu_live_abt_wcleaf`,
`xnu_live_abt_wcapx`, `xnu_live_abt_wcoow`.

It is called **at the same abort as W-WALK, right beside it** — so it is the control for the
*mechanism* (does "walk TTBR0 for a known-mapped VA and read its leaf" produce a correct writable leaf
on THIS device?), read by the same code path and library as W-WALK, moments apart. Two deliberate
properties of the call: it is **throttled to the ladder ordininals** (the same `n <= 4 ||
power-of-two` rule, checked BEFORE anything is read), because the storm is ~2.1 M aborts and a control
per abort would buy nothing; and it passes **no alias** to the walker, so a control VA that is not
in-window reads a covered refusal rather than installing a temporary section. It is a fixed, cheap,
safe read that never touches the faulted address.

---

## 2. Why the index math is safe (the fact the whole probe rests on)

The two walks are in different *address spaces*, and that is the point:

* **W-APEX reads PHYSICALLY.** `pm_l1_phys` is a physical address, built for **TTBR0** — under this
  port's `TTBCR.N = 2` (`arm_vm_init.c:475 set_mmu_ttbcr(2)`) TTBR0 owns `[0, 0x40000000)` and
  `pm_l1_phys = 0x81a86000` is inside it. Every address W-APEX derives stays inside
  `[0, 0x40000000)`: the L1 slot (`0x81a86000 + 12 bits`) *and* the L2 table base (the coarse
  descriptor base is a low-RAM PA, this boot's own example being `0x46300000`, which the (A) walk
  already reaches through the alias mechanism). **`TTBR1` is never needed and is never assumed.**
* **W-WALK / W-SYNC read through TTBR0 with `stage90_read_l2_pte`**, which picks its table by
  `TTBCR.N` (`entry_ttbr_base`) and refuses a VA above the split.

**The one honest caveat, stated because the alternative is a stale zero.** On this device
`TTBR0 == TTBR1` (`entry_smem.h:53`, the live channel's `xnu_live_ttbr0 == xnu_live_ttbr1`), but the
kernel *managed* window is `MANAGED_BASE = 0xC0000000` (`arm_vm_init.c:142`) — above the `0x40000000`
split, i.e. **TTBR1 territory**. So W-SYNC's leaf is only guaranteed readable if the kernel's managed
tables share the walked base or another TTBR0-resident mapping answers at that VA. W-SYNC therefore
**fails open** and says so: `xnu_live_abt_wcoow = 1` + `wck = 0` is the published reading
"TTBR0 does not answer for the managed base", and that outcome is a configured null control, not a
crashed probe. (The read is bounded to `[gPhysBase, gPhysBase + mem_size)`; a table pointer outside
it is refused, never dereferenced — the 1010b `FS=5` death.)

**Which key is which — the keyspace, fixed here once so the checker, the build and the press cannot
drift** ([[mi4-one-value-two-definitions]]): `w* = W-APEX`, `wk* = W-WALK` (the lower-case `k` is the
async abort's own walk), `wc* = W-SYNC` (the control), `ww*` = the apex ordinal + nucleus. No two
probes share a key.

---

## 3. THE PRE-REGISTERED VERDICT TABLE

The decision reads the **TAIL rows' own values** (any ladder ordinal `>= 0x400`; the storm settles by
~0x200, so `>= 0x400` is the storm's own steady state) — **never a whole-run `max()`/`first()`**
(the 1008/1009 measurement defect, banned; [[mi4-measurement-defects]]). The reader compares the
apex leaf to the walk leaf **by LOCATION** (`wwl2pa`/`wwleaf` vs `wkl2pa`/`wkleaf`) and flags a
location mismatch.

| # | W-APEX leaf (`wwleaf`/`wwapx`) | W-WALK location (`wkl2pa`) | verdict | what it means / what follows |
|---|---|---|---|---|
### The gate that precedes them — V0, THE PROVENANCE GATE

| # | condition | verdict | what it means |
|---|---|---|---|
| V0 | the tail W-APEX rows are present, but **every one** carries `wwpatbl = 1` (the entered page is a page-**table** page) | **APEX_OBSERVED_A_TABLE_PAGE** | the enter W-APEX observed installed a page-table page (the pmap's own L1 table, or this VA's L2 table), NOT the storm VA's own small page — so "the write reaches the apex **for the faulting VA**" is **not proven**. **This is the 1012 cell's real shape**: every 1012 apex row carried `pa=0x8421a` (PA `0x8421a000`). REPORT it; V1–V3 are withheld. |
| V0 | every tail row carries `wwva != far` (the storm's tail `far`, from the 1008 ladder) | **APEX_WRONG_VA** | the apex observed a different VA's enter — exactly what a stale post-handler key looks like, now READABLE instead of assumed. V1–V3 withheld (NULL CONTROL). |
| V0 | every tail row carries `wwft != 0x3` | **APEX_WRONG_FAULT_TYPE** | the apex observed a non-WRITE enter; the storm's WRITE fault did not produce this leaf. V1–V3 withheld. |

A row that fails ANY of the three is a NULL CONTROL (reported individually); the class above fires
only when **every** tail row fails that one test, so a single stray row cannot manufacture a verdict.
V0 is applied **after** the control and **before** V1–V3, on the same tail rows.

| # | W-APEX leaf (`wwleaf`/`wwapx`) | W-WALK location (`wkl2pa`) | verdict | what it means / what follows |
|---|---|---|---|---|
| V1 | APX = **0** (WRITABLE) at W-APEX's own location | same L2 table as W-APEX | **LEAF_WRITABLE_STILL_FAULTS** | the leaf IS writable in the table the pmap wrote and the MMU walks yet the store still faults ⇒ a **STALE TLB** / a second table the TLB holds / the flush did not reach this CPU. Next rung instruments `pmap_flush_tlbs` + the ASID at the fault. |
| V2 | APX = **1** (READ-ONLY) at W-APEX's own location | same L2 table as W-APEX | **F1a_CONFIRMED_LEAF_DEFECT** | the pmap's write did **not stick**, or a second writer re-RO'd the leaf: the apex input is correct but the output is read-only. Next rung instruments the second writer (`pmap_page_protect` etc.). |
| V3 | any, readable | **a DIFFERENT L2 table** (`wkl2pa != wwl2pa`) | **IDENTITY_FAULT_L2** | the apex writes one L2 and the MMU walks another. 1012 measured the L1 bases EQUAL, so the divergence is at the **L2** (an `pmap_expand` hoist / a `pm_l1_virt` vs `pm_l1_phys` L2 mismatch). |
| V4 | `wwn` **present but `wwnoproof` = 1** (`pm_l1_phys != live TTBR0 base` at that row) | any | **APEX_NOT_PROOF** | the pmap's own table is not the live table at that measurement, so the apex leaf cannot be paired with the walk. A by-value CONTRADICTION with 1012 — report it, do not conclude from it. |
| V5 | **absent** (`wwn` == 0) yet the storm ran | any | **NO_APEX_WITNESS** | the enter never reached the wrapper. The fault is not a leaf defect at this apex; fall back to the tree instrument. |
| V6 | apex leaf **absent/0** while `wwl1` is a COARSE descriptor **and `wwl2pa != 0`** | any | **HARD (stale 0)** | a present L2 with no leaf at the fault VA is impossible for a correct walk — a MEASUREMENT defect, never a value. The same class covers a published `_xxapx` that is not the APX bit of the published leaf, in any of the three ladders. |
| V0′ | the tail W-WALK row carries **`wkstable = 0`** (the double-read disagreed) | any | **WALK_UNSTABLE_NULL** | the L2 word was NOT the same on a second walk of the **same VA** after a `dsb`: the table **moved** between the reads, so the location comparison V1/V2/V3 rest on was taken over a table that does not hold still. NULL CONTROL — V1–V3 withheld. A moving table under a faulting VA is itself a finding (a concurrent writer, or the `pmap_expand` hoist). |
| — | **W-SYNC control wrong** (`wcoow=1` **or** `wck=0` **or** `wcapx != 0`) | any | **WITNESS_METHOD_BROKEN** | the witness method itself is broken on this device (or the managed window is TTBR1 territory, which is the EXPECTED null-control outcome). Report the measurement defect. **Do NOT conclude.** |

**V1/V2/V3 are the three real outcomes; V0/V4/V5/V6/WITNESS_METHOD_BROKEN are the ways the
measurement can refuse to be a verdict.** The control (W-SYNC) is read FIRST: a wrong control
short-circuits every one of V1–V3 into **WITNESS_METHOD_BROKEN**, exactly as the task requires. The
provenance gate **V0** is applied NEXT, before V1–V3: an apex row that is not provably THIS storm's
own fault (a table page, a different VA, a non-WRITE fault type) withholds V1–V3 into
`APEX_OBSERVED_A_TABLE_PAGE` / `APEX_WRONG_VA` / `APEX_WRONG_FAULT_TYPE` — the 1012 defect made a
refusal instead of an inference.

**A second pre-verdict gate — V0′ — guards the WALK side.** Before the location comparison that
decides V1/V2/V3, the double-read must have AGREED: `wkstable = 0` at the W-WALK tail means the L2
word moved between the two walks (one `dsb` apart), so the location the comparison reads is not a
fixed quantity. That withholds V1–V3 into **WALK_UNSTABLE_NULL** — a NULL CONTROL, reported, never a
verdict. The gate is applied on the same tail row, after V0 and before V1–V3; a **missing** `wkstable`
(a pre-double-read capture) does **not** gate, because such a log has no verdict to withhold.

**The W-WALK endpoint refinement (documented so V3 is not over-read).** W-APEX knows the table base
`pm_l1_phys`; W-WALK resolves the table base from the LIVE TTBR0 by the same rule. Those two bases
are the SAME cells 1012 already compared (`pmppmap` vs `pmpttbr`) and measured EQUAL. Therefore
`wkl2pa` vs `wwl2pa` compares the **L2 entry under one agreed base**, which is the sharpest form
available: the two probes read this VA's L2 table through their own base. A divergence means the L2
itself differs (V3). Two sub-shapes are distinguishable and the reader prints both:
`wk`/`wkv` present with `wkl2pa == wwl2pa` is the strong V1/V2 pair; if W-WALK cannot resolve at all
(`wkoow`/`wk = 0`) while W-APEX can, the reader reports the apex leaf alone and **withholds V3**
(a location comparison over a missing location is not a verdict).

---

## 4. Build side (the project's hard-won rules, followed exactly)

**Switch `STAGE90_XNU_PTE_WITNESS`** (default 0). Its two consumers are `entry_stubs.c` (the four
recorders + the two walk helpers) and `entry_trace.c` (the four call sites: `entry_note_abt_cur`
before the handler, the W-WALK/W-SYNC pair after it, and W-APEX in the enter wrapper), both compiled
by `build_entry.sh`. Plumbing required, all four sites or the build refuses:

1. `ENTRY_ARM_KEYS` — `STAGE90_XNU_PTE_WITNESS` joins the roster.
2. the record-`case` block — `STAGE90_XNU_PTE_WITNESS) _v=$PTEWIT ;;`, read from the resolved
   `$PTEWIT` validated `0/1` at its own assignment.
3. **an explicit `echo "STAGE90_XNU_PTE_WITNESS=$PTEWIT"` in the record writer** — the record is
   written by hand-written `echo` lines (`build_entry.sh:~39900`), NOT from the array; the writer is
   the one site a name-keyed diff does not visit and the only site the gate ever reads. 678's
   two-way check after the file is written is what makes forgetting it a refusal rather than a commit.
4. `STUB_DEFINES` (for `entry_stubs.c`) and `STUB_DEFINES_TRACE` (for `entry_trace.c`).

**Refusals** (mirroring PMP_ENTER's, `build_entry.sh:1314-1332`): `PTE_WITNESS=1` requires
`ABT_FAR=1`, `ABT_PC=1`, `ABT_PTE=1` **and** `PMP_ENTER=1` — a witness for an apex that is not
instrumented measures nothing. **Interlock and empty build: with the switch OFF the record's value is
`0`, which NAMES the honest empty build** ("this arm carries no sub-measurements"), and the build
never refuses it — a hard refusal would make the switch's own OFF arm unbuildable (the
`STAGE90_XNU_ABT_FAR` precedent, which is a soft `echo`).

**No `--wrap` is added.** 1013 rides 1012's `--wrap=pmap_enter_options` (and the existing
`--wrap=sleh_abort`); the wrapper grows ONE call and stays a PURE PASS-THROUGH. **OFF must be
BYTE-IDENTICAL** — `src/` only, and nothing writes an image except `build_entry.sh`; `src/` is
proven by `git diff --stat` (on build, or on `git status --porcelain=2` when the working tree
carries other agents' uncommitted work).

**By-value assertions (`scripts/build_1013.sh`):**
* the new key literals are present, each once, as **exact whole-line** `strings` matches;
* the set **INCLUDES** `xnu_live_abt_wwnx` — the exact-literal count is *live*, and would otherwise
  pass while the change-count key was missing entirely;
* `entry_note_abt_wleaf` / `entry_note_abt_walk` / `entry_note_abt_wsync` / `entry_note_abt_cur` are
  DEFINED `T/t`;
* exactly ONE `b|bl` to each, image-wide;
* **the provenance ordering**, asserted by LINE NUMBER: `entry_note_abt_cur(far_)`'s line is
  STRICTLY BEFORE `__real_sleh_abort(regs, type);`'s line (the key must be set before the handler);
  and `entry_note_abt_wleaf` keys on `g_abtpte_curva` (`cur = g_abtpte_curva;` +
  `if (cur == 0u || va != cur)`), NOT on 1012's post-handler `g_abtpte_pmpva`;
* `__wrap_pmap_enter_options` still calls `<pmap_enter_options>` (the pass-through) **and** both
  recorders — the wrapper is not hollowed out;
* the **anti-inline assertion**: `__wrap_pmap_enter_options`'s body does NOT contain
  `bl <entry_note_abt_walk>` (which would mean W-WALK's recording was moved into a call the abort
  path never reaches — [[mi4-hooks-live-at-calls]]);
* the **expand-hazard refusal**: no `entry_note_abt_wleaf` call anywhere in `src/entry/*.c` passes a
  `pm_l1_phys` value, and the recorder does not declare a `uint32_t pm_l1_phys` parameter — either
  spelling would let a `pmap_expand_ttb` that moved the L1 table produce a reading of the RETIRED
  table (`docs/1013-leaf-readback-verify.md` §0.2/§6.2);
* the FROZEN 1012 elf (where present) as the **NEGATIVE CONTROL**: the four symbols are `x0` and none
  of the provenance/double-read literals (`wwva`/`wwft`/`wwpatbl`/`wkleaf2`/`wkstable`) is present.
  A reader that cannot fail certifies nothing. **When the frozen control is ABSENT the OFF
  byte-identity check is REQUIRED** — and `build_1013.sh` says which one it took.
* the fresh record carries `STAGE90_XNU_PTE_WITNESS=1`.

---

## 5. Honest scope

* 1013 installs **no fix**; the run is EXPECTED to freeze at the same (b2) shape (~24.39 s). The
  verdict is a successful READ (exit 0); the only HARD paths are a broken recorder and the V6
  stale-zero.
* W-SYNC's expected-null outcome (managed base in TTBR1 territory) is stated up front; if the
  control is null the run reports **WITNESS_METHOD_BROKEN** and concludes nothing. That is the
  designed behaviour, not a bug to be tuned away.
* The (A) walk's remaining `ptel2 = 0` anomaly is not re-derived here; 1013 publishes
  `wkapex = (ptel2 == wkleaf)` **and** the double-read pair `wkleaf2`/`wkstable`, so (i) the two
  walks' agreement and (ii) whether the value was STABLE across a `dsb`-separated second read are
  both **readings**; a disagreement or an unstable read is a null control rather than a silent
  re-attribution.
* **The provenance caveat travels with V1-V3.** When the gate passes, the verdict is stated with the
  apex cell's provenance attached (`wwva == far`, `wwft == 0x3`, `wwpatbl == 0`); when it does not,
  the run reports the table-page / wrong-VA / wrong-fault-type class and concludes nothing. A future
  rung that needs the storm VA's OWN page-in observed must gate the apex on the enter's `pa` matching
  that VA's object page — a strictly stronger cell than `_wwnoproof` (which only compares table
  bases, `docs/1012-probe-keying.md` §2.2/§3.2/§6).
* All line numbers are the working tree at `HEAD` (1013 staged on top of `dfc1904`).