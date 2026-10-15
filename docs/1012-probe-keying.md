# 1012 probe keying — STATIC verification (no device)

Scope: verify the apex record's own keying and the (A) walk's base mask + L2 deref against
`src/entry/entry_stubs.c`, `src/entry/entry_trace.c`, `sync/trap.c` and the 1012 press capture
`out/stage90/captures/recovered-unknown-arm-20261015-020222-last_kmsg.txt` (`records/1012-press-read.txt`).

---

## 1. THE APEX RECORD'S KEYING — the key is the `far` of the SAME abort, NOT latched

### 1.1 `g_abtpte_pmpva` is written UNCONDITIONALLY, on EVERY abort

`entry_note_abt_pte` (entry_stubs.c:2424) is called once per abort from `__wrap_sleh_abort`
(entry_trace.c:5788), and at :2451, guarded only by the PMP/WRSTRIP switch — **not** by the ladder:

```c
2451:    g_abtpte_pmpva = va;    /* 1012: the PMAP_ENTER/WRSTRIP cells key on the SAME VA the abort faulted on */
```

The declaration comment `g_abtpte_pmpva` (entry_stubs.c:2361) spells the same: *"the storm's VA,
set by entry_note_abt_pte (the abort's own far)"*. It is **NOT latched once**; it is refreshed on
every abort. So the "first-abort VA frozen forever" defect the task hypothesised does **not** exist.

### 1.2 The key and the published `far` are the SAME parameter expression

`__wrap_sleh_abort` reads the pair at entry (entry_trace.c, the `type`-selected cp15 pair):

```c
    } else {
        __asm__ volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(fsr));
        __asm__ volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(far_));
    }
```

and then hands the *same* local `far_` to both recorders:

```c
5760:    entry_note_abt_far(entry_counter() - t0, far_, (uint32_t)type);   /* publishes xnu_live_abt_far  */
...
5788:    entry_note_abt_pte(far_);                                          /* sets         g_abtpte_pmpva */
```

`entry_note_abt_far` publishes `xnu_live_abt_far` from that same `far` (entry_stubs.c:1996). So the
tail's `_far` and the apex key are **provably the same value**, byte for byte.

### 1.3 The apex filter

`entry_note_abt_pmp(va, prot, pa, pm_l1_phys)` (entry_stubs.c:2643) records only when the argument
`va` equals the current key:

```c
2647:    if (g_abtpte_pmpva == 0u || va != g_abtpte_pmpva)
2648:        return;                 /* not the storm's VA - do not fill the channel */
```

`va` is `__wrap_pmap_enter_options`'s own first argument (entry_trace.c:5933, the real
`pmap_enter_options(pmap, va, pa, ...)` call). So an apex row is emitted **iff a real
`pmap_enter_options` was called with a VA equal to the current abort's `far_`**.

### 1.4 The ordering subtlety — the apex runs BEFORE the key is written

`__wrap_sleh_abort` calls `__real_sleh_abort(regs, type)` at **entry_trace.c:5746**, while
`entry_note_abt_pte(far_)` runs **after** it, at :5788. The `vm_fault` / `pmap_enter` the abort's
handler makes lives *inside* that real call (e.g. trap.c:658
`vm_fault(map, vm_map_trunc_page(dfar), …, VM_PROT_READ|VM_PROT_WRITE, …)`). Therefore, per abort:

1. the handler runs `vm_fault` → `pmap_enter_options` → `__wrap_pmap_enter_options` → `entry_note_abt_pmp`,
   filtered against the key **left by the PREVIOUS abort**;
2. only then is the key set to **this** abort's `far_`.

Consequence: the apex record is keyed on a VA that is provably **the previous abort's `far`**,
which is **not provably the same as the `far` of the enter's own fault**. Under a fixed-VA storm
(§1.5) the two coincide; in general they need not.

### 1.5 Is the apex VA provably the storm's far? — YES, and the match is NOT coincidental

The 1012 capture settles it by value (all numbers below re-extracted from the capture by
`(ordinal, key)` pairing, not from the report's prose):

Ladder counts, all **22 rows** each, identical ordinals — the pcn ↔ vmen ↔ farn ↔ pmpn ladders are
in exact 1:1 lockstep:

```
pcn   : 1 2 3 4 8 10 20 40 80 100 200 400 800 1000 2000 4000 8000 10000 20000 40000 80000 100000
vmen  : 1 2 3 4 8 10 20 40 80 100 200 400 800 1000 2000 4000 8000 10000 20000 40000 80000 100000
farn  : (same)          pmpn : (same)
```

Per-row:
* `_far` (farn==1..0x100000) is **constant 0x2e000**; `_type`=4; `_fsr`=**0x80f** (WnR=1 WRITE,
  FS=15 = permission, level 2 — decoded in `check_1012_capture.py`).
* `_pmpn`==`_pcn` at every ordinal, and the trailing `_pc`/`_pcfar` is **(0x2be1e1a0, 0x2e000) at
  every tail ordinal**. `_pcn=0x100000`, `_pmpn=0x100000` — both counters reached exactly 1,048,576.

So at the tail the abort counter, the apex counter, the fault VA, the fsr and the user pc all agree
row-for-row. **The apex rows ARE the storm's VA (0x2e000), by value.**

**But the two facts do not yet prove the write "reaches the apex" for the storm's own fault.** They
prove that *some* `pmap_enter_options` was invoked at va=0x2e000 about as often as the storm retried.
The key is the *previous* abort's `far` (§1.4), and the apex rows are identically consistent with
the storm's faulting WRITE reach. The alternative — a READ fault at 0x2e000 whose fault path is what
loops, with `0x80f` being a separate snapshot — is not excluded by the static keying. It would be
excluded by instrumenting `entry_note_abt_pmp`'s own `pmap_enter_options` `fault_type` arg
(available for free at the wrap: `fault_type`, entry_trace.c:5928), which the 1012 probe does **not**
capture.

### 1.6 What the apex rows actually show — the value is a real data page, and the verdict stands on value alone

The apex tuple (all 22 rows identical):

```
prot = 0x3 (READ|WRITE)   pa = 0x8421a   pm_l1_phys = 0x81a86000   TTBR0 = 0x81a8605b
```

`pa` is the `ppnum_t` argument to `pmap_enter_options` (`pmap.c:3179`), and `leo_enter` consumes it as
a **page number** (`leo_pte_build`, pmap.c:1639-1641: `(pn<<PAGE_SHIFT)&L2_ADDR_MASK`). So the entered
page is PA **0x8421a << 12 = 0x8421A000**, inside the boot window `[0x80000000, 0x9e400000)`
(`ptewbase=0x80000000`, `ptewlen=0x1e400000`) — a real RAM cell, *not* a table slot. (I initially
mis-shifted it to 0x8421000, which sits inside the L1 table at 0x81a86000 and would have made this a
table-install; that reading was an arithmetic error and is withdrawn.)

So the apex observed a **genuine page enter at va=0x2e000 with `prot=READ|WRITE`**. `prot` carries
W differential by value — and the report's headline (`_pmpprot`=0x3, masked table bases
`0x81a86000 == 0x81a86000`) is correct as far as it goes.

**What remains open is the pairing, not the value** (§1.4): the key is the *previous* abort's `far`.
*If* the key and the enter's own fault VA were the same 0x2e000, the apex is genuine for the storm
and "the write arrives at the apex" holds. The static evidence (a fixed-VA storm, §1.5) makes that
coincidence reliable, and 22/22 identical rows are consistent with one repeated enter. But the probe
does not *carry* the enter's own `fault_type`/`va` into the row, so it does not *prove* the enter was
the storm's WRITE fault rather than a table-install or a READ. The parent's c1 is therefore a real
**hole in the probe's discrimination**, not (as §1.6 previously claimed) a demonstrated wrong
object.

---

## 2. THE (A) WALK — base mask is CORRECT; `ptel2=0` is MOSTLY a real absent-shaped leaf, not a mask artifact

### 2.1 The base mask uses the CORRECT TTBR0 mask for TTBCR.N=2

`stage90_read_l2_pte` (entry_stubs.c:2255) reads TTBCR, takes `n = ttbcr & 7`, chooses TTBR0 when the
VA is in the low window, then:

```c
2292:    base = entry_ttbr_base(base, n);
```

with

```c
2126: entry_ttbr_align_bits(uint32_t n) { return 14u - n; }         /* 7..14 */
2132: entry_ttbr_base(uint32_t ttbr, uint32_t n) { return ttbr & ~((1u << entry_ttbr_align_bits(n)) - 1u); }
```

For N=2 this masks bits[1:0] to 0 over a 4 KB alignment — i.e. **bits[31:12] answer, `0xfffff000`**.
That is the *correct* mask the task asked for; the 1010/1010b wrong constants (`0xfffffc00` L1→L2
mask, `0xffffc000` fixed 16 KB mask) are gone. The measured `_ptettbr=0x81a8605b` masks to
`0x81a86000`, matching the apex `pm_l1_phys` — **the walk reads the same L1 the pmap writes.** Good.

### 2.2 The L2 deref uses a direct identity-map read for in-window tables

```c
2327:    l2p = (uintptr_t)l2base + (((va >> 12) & 0xffu) << 2);
2336:    if (l2p >= (uintptr_t)gPhysBase && l2p < (uintptr_t)gPhysBase + (uintptr_t)mem_size) {
2337:        l2e = *(const volatile uint32_t *)l2p;
2338:    } else {
2339:        g_abtpte_walk_oow = 1u;                       /* the leaf PA is outside the window */
2340:        if (allow_alias == 0u)
2341:            return 0u;
2342:        l2e = stage90_read_l2_word_aliased((uint32_t)l2p);
2343:    }
```

where `l2base = l1e & ABTPTE_L1_PTE_MASK` (entry_stubs.c:2323, `0xfffffc00`, the correct L1→L2 mask).
The **runtime** window comes from the measured tail rows, `_ptewbase=0x80000000` and
`_ptewlen=0x1e400000` ⇒ `[0x80000000, 0x9e400000)` (not the compile-time `gPhysBase = 0x200000`
default at entry_stubs.c:100).

### 2.3 `ptel2=0` is NOT the 1010c wrong-table defect, and NOT (in the tail) the window bound

The storm's L1 descriptor is `_ptel1 = 0x81bb1001` — present (A=1), COARSE (bits[1:0]=0b01) ⇒
`l2base = 0x81bb1000`, whose slot for va=0x2e000 is `0x81bb1000 + 0x2e<<2 = 0x81bb10b8`.
* `l2p = 0x81bb10b8` is **inside** the window ⇒ direct read taken, no alias needed.
* The capture confirms `_pteoow=0`, `_ptealias=0`, `_ptemapok=0` on all 22 rows — so the walk did
  **not** refuse via the window/alias at any ordinal.
* Yet `_ptel2=0x00000000`.

So the walk reached the right table and read a **raw 0x0**. `0x0` fails the shape test at :2347
(`(l2e & 0x3) != 0b10`) and is reported as 0. A raw 0x0 word at a COARSE L1's L2 slot is an
**inconsistency with FS=15** (an absent descriptor gives FS=5/6/7, not a level-2 *permission* fault),
but it is **not** an artifact of a wrong L1 base mask, nor of the window bound.

**Two candidate explanations, both consistent with the static code, neither decidable statically:**

* *(2.3a) STALENESS.* The walk runs **after** `__real_sleh_abort` (entry_trace.c:5746 vs :5788), i.e.
  after the handler's `vm_fault` → `pmap_enter` on the storm's va has already returned. So a
  mid-construction race is *not* expected at the storm row. Staleness is still possible if the pmap
  wrote the leaf but the walk's load missed the store (no barrier, and the L1→L2 slot is written by
  `leo_enter` at `pmap_pte(map,va)`, a `pm_l1_virt` pointer that must alias the same PA the MMU
  walks). A real 0x0 there would again contradict FS=15, so staleness remains a *candidate artifact*
  but is now the weaker of the two.
* *(2.3b) the walk's L2 base does not equal what the MMU walks.* `pmap_pte` indexes
  `pmap->pm_l1_virt` (pmap.c:1777 `tte_offset_begin = pmap->pm_l1_virt`), while the MMU walks
  `pm_l1_phys`. If those two ever name different pages, `leo_enter` writes one L2 table and the walk
  reads another (0x0). This is the *same* table-identity question the apex cell raises via
  `pm_l1_phys` vs TTBR0, applied at the L2. `leo_pte_build` always emits `L2_SMALL_PAGE`
  (pmap.c:1641), so 0x0 is **not** a large page under either explanation.

### 2.4 The capture actually CONTRADICTS the "always 0" framing — the walk works elsewhere

The report (`records/1012-press-read.txt` RESULT 3) says the walk "still returns 0". By value, 3 of
22 rows return a **valid small-page leaf**:

```
ord=0x20  va=0x2be20fe8  l1=0x81b7a001  l2=0x81b993ba  pteu=1
ord=0x100 va=0x2fafe84c  l1=0x8234b001  l2=0x8238d3ba  pteu=1
ord=0x200 va=0x349603c8  l1=0x81ab7001  l2=0x832453ba  pteu=1
```

All three are `& 3 == 2` (small page), with `pteu=1` (TTBR0 answered). 19 rows return `ptel2=0`
(exactly the `pteu=0` set). So the walk's machinery is **sound**; the zero is specific to certain
vas/ordinals. Notably the PTA/BT bit pattern `…ba` (bit 9 = APX) is the *expected* read-only user
leaf for a shared-cache page; the walk can read a real leaf when it exists.

**Verdict on (A):** the base mask is CORRECT; the L2 deref is a direct identity-map read for the
storm's in-window table; `ptel2=0` at the storm's tail is **a real reading of a raw 0x0 word, and is
NOT an artifact of a wrong base mask or of the window bound.** Whether it is a genuine absent leaf
(contradicting FS=15) or a stale/L2-base-identity artifact (§2.3a/b) is **not decidable statically**.
"The walk returns 0" as a blanket statement is refuted by the three valid rows.

---

## 3. Straight verdicts

1. **Apex keying:** `g_abtpte_pmpva` is set on EVERY abort from the abort's own `far_` (entry_stubs.c:2451,
   the same local `far_` that publishes `xnu_live_abt_far`, entry_trace.c:5760/5788). It is **not
   latched once**; the rows key on the abort's own far, and by value the tail key IS `_far`=0x2e000
   (identical `pcn`==`pmpn` ladders, 22 rows each).
2. **But the key is written AFTER the handler runs** (real handler entry_trace.c:5746 vs key write
   :5788), so the apex record is filtered against the **previous** abort's far. Under the fixed-VA
   storm (constant `far`=0x2e000 across 22 rows) this coincides; it is not a general identity.
3. **The apex VALUE is real, the PAIRING is what is unproven.** `pa` is a `ppnum_t`; the entered page
   is `0x8421a<<12 = 0x8421A000` — a real RAM page inside the boot window, **not** a table slot (an
   earlier draft mis-shifted this; withdrawn). So the row does show a genuine
   `pmap_enter_options(va=0x2e000, prot=READ|WRITE)`. What the probe does **not** carry is the
   enter's own `fault_type`/`va`, so it cannot *distinguish* the storm's WRITE fault-enter from a
   table-install or a READ enter at the same VA. The headline is plausible and value-supported, but
   **not proven by the probe's own keying** — this is exactly the c1 gap, and it is a probe
   *discrimination* hole, not a demonstrated wrong object.
4. **`ptel2=0` is not a mask artifact.** The mask (`entry_ttbr_base`, `2^(14-n)`) and the L2 deref
   (direct read, bounded window `[0x80000000, 0x9e400000]`, `_pteoow=0`) are correct; the storm's
   `l2p=0x81bb10b8` is in-window and read directly. The raw 0x0 is either a genuine absent leaf
   (inconsistent with FS=15) or a staleness / `pm_l1_virt`-vs-`pm_l1_phys` artifact — **not
   decidable statically**.
5. **The report's own RESULT 3 over-claims.** 3 of 22 (A) rows return a valid small-page leaf
   (ord 0x20/0x100/0x200, `pteu=1`, `& 3 == 2`), so "the walk still returns 0" is false as written;
   the walk reads real leaves when they exist.

### Recommendation (measurement defects to close before trusting either cell)

* Capture `fault_type` (already a wrap argument, entry_trace.c:5928) in `entry_note_abt_pmp` — it
  distinguishes a WRITE-bearing storm enter from the table-install enter without a new press.
* Capture the actual `va` **inside** the recorder (add `_pmpva`) so the row self-identifies its own
  enter's VA, instead of relying on the previous abort's key.
* Re-run `entry_note_abt_pte` **before** `__real_sleh_abort` (or gate the apex key on the *pending*
  abort) so the apex filter keys on the current fault, not the previous one.
* For (A): read the L2 word with a barrier/double-read, or record both the pre- and post-handler
  walk, so the race (§2.3a) is disentangled from a real absent leaf.

None of this is device work; items 1–3 are host-side edits to `entry_stubs.c` / `entry_trace.c`.