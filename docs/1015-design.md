# 1015 — THE DECISIVE TEST: is the recurring FS=15 a STALE GLOBAL TLB ENTRY, with the permission-inputs witness riding along

**This is a rung design, not a build.** 1014 (`armed-d13-29ec9e48`, pressed, RESIDENT) read the leaf
**VALUE** and the **flush**: the leaf the MMU walks is user-writable, the ASID-scoped flush ran under
the matching ASID — and the store still takes `FS=15` permission faults at a constant `far`/`pc`,
~2^20 times. The decision the CPU makes **is not the leaf's AP bits**.

Two by-value findings (this session) name the leading cause and make 1015 an **experiment** rather
than another passive read:

* **Every user leaf is GLOBAL.** `L2_NG_BIT 0x800` (`pmap.h:231`) is **never ORed anywhere in the
  tree** — `grep -rn L2_NG_BIT --include=*.c --include=*.h .` returns its definition and nothing else.
  The storm's leaf `0x841e91bb` has bit11 (`non_global`, `pmap.h:137`) = **0** ⇒ GLOBAL.
* **The steady-state flush cannot evict a GLOBAL entry.** `pmap_asid_configure` issues a full
  `arm_tlb_flushID()` **once at boot** (`pmap_asid.c:161`, called from `pmap_init`, `pmap.c:3723`).
  The only steady-state flush is `arm_tlb_flushID_ASID` (`pmap.c:1349`, = 1014's measured
  `_wfkind=2`). On ARMv7 **TLBIASID invalidates only ASID-tagged (`nG=1`) entries; a GLOBAL entry
  survives it** — only TLBIALL (`c8,c7,0`) evicts it.

⇒ **H-G (STALE_GLOBAL_TLB)**: a stale **GLOBAL** translation of `0xe6000` — installed when the page
was mapped read-only by earlier activity — survives every ASID flush and **shadows** the newer
user-RW leaf. That is exactly a *constant* `far`/`pc`, FS=15 **write** re-fault on a page whose
current leaf is writable. **1015 tests it by making the storm's own flush a full TLBIALL and asking
one question: does the storm STOP?**

Everything below is cited to a line **read by value this session** (`HEAD` = `0ef1949`). The new
names grep to 0 tree-wide: `STAGE90_XNU_USER_FULL_FLUSH`, `STAGE90_XNU_PERM_WITNESS`,
`xnu_live_abt_leafng`, `xnu_live_abt_wd*`.

---

## 0. THE 1014 READING, RESTATED BY VALUE (no other file needed)

Capture `out/stage90/captures/recovered-unknown-arm-20261015-092217-last_kmsg.txt`, device
`4a2fe00b`, **no panic**, `xnu_live_wdt_pets=0x61` ⇒ RESIDENT. The storm's own tail rows
(`_pcn` `0x400`..`0x40000`) all carry:

```
_pc=0x2bed61a0  _fsr=0x0000080f  _abtcpsr=0x60000030  _pcfar=0x000e6000
_ptel1=0x81bb2001  _ptel2=0x841e91bb  _ptettbr=0x81a8605b  _pteu=1
_wkl1=0x81bb2001   _wkleaf=0x841e91bb _wktb=0x81a8605b
_wwleaf=0x841e91bb _wwva=0x000e6000  _wwft=0x00000003
```

### 0.1 `0x841e91bb` decoded against the tree's OWN struct (`pmap.h:128-142`)

```
0x841e91bb = 0b1000 0100 0001 1110 1001 0001 1011 1011
  bit1 (valid)       = 1   => present ; L2_SMALL_PAGE 0x2 (pmap.h:226)
  bit0 (nx)          = 1   => L2_NX_BIT (pmap.h:227)
  bits[5:4] (ap)     = 0b11 => bit4 PRW = 1  AND  bit5 USER = 1  (pmap.h:218/:221) => USER READ|WRITE
  bit9  (apx)        = 0   => AP[2] clear, writable  (pmap.h:222)
  bits[8:6] (tex)    = 0b110 ; bit3 (C) = 1 ; bit2 (B) = 0
  bit11 (non_global) = 0   => **GLOBAL**   <-- THE H-G CELL
  bits[31:12] = 0x841e9 => PA = 0x841e9000
```

`pmap_vm_prot_to_page_flags` (`pmap.c:1104`) produced it: `if (pmap != kernel_pmap) pte |=
L2_ACCESS_USER;` (`:1113`), `pte |= L2_ACCESS_PRW;` (`:1114`), `if (prot & VM_PROT_WRITE) pte &=
~(L2_ACCESS_APX);` (`:1120`), `if (!(prot & VM_PROT_EXECUTE)) pte |= L2_NX_BIT;` (`:1128`). **It never
sets `L2_NG_BIT`** — so the leaf is GLOBAL by omission, and so is every other user leaf.

### 0.2 `0x81bb2001` decoded (`pmap.h:117-125`)

```
bits[1:0] = 0b01 => is_coarse (L1_TYPE_PTE 1, pmap.h:210) => an L2 table follows
bits[8:5] (domain) = 0     <-- the domain the DACR must mark client  (passive witness, §2)
bits[31:10] (pfn)  = 0x206ec8 => L2 base 0x81bb2000
```

### 0.3 Three probes agree; the flush cell (F2); the control

`_wwl1 == _ptel1 == _wkl1 == 0x81bb2001` and all three leaves are `0x841e91bb`; the apex's own
`_pmppmap = 0x81a86000` equals `_ptettbr & 0xFFFFFF00` (the identity cell). **`_ptethr` is absent on
the storm rows in this capture** (the `VME_PROT`-gated store at `entry_stubs.c:2572` did not reach
the tail) — 1015 must not depend on it; it re-reads the lineage itself. Flush: `_wfkind=0x02`,
`_wfself=1`, `_wfasid=0x02 == _wctx=0x02`, `_wfttb == _wflive == 0x81a86000`. Control (W-SYNC,
`entry_stubs.c:3125`) came back SOUND: `_wck=1 _wcl1=0x80a08001 _wcl2pa=0x80a08000 _wcleaf=0x8008021a
_wcapx=1 _wcoow=0`.

### 0.4 The tail is NOT uniform (a by-value finding 1014's summary omits)

| `_pcn` | `_pc` | `_pcfar` | `_abtcpsr` | `_ptel2` | `_ptel1` |
|---|---|---|---|---|---|
| `0x400`..`0x40000` (9 rows) | `0x2bed61a0` | `0x**000e6000**` | `0x60000030` USER | `0x841e91bb` | `0x81bb2001` |
| `0x80000` | `0x38413ad0` | `0x3a1141e8` | `0x00000010` USER | `0x842461bb` | `0x842c5001` |
| `0x100000` | `0x3840ea3e` | `0x3a0138e0` | `0x60000030` USER | `0x841a71bb` | `0x841c2001` |

The last two are inside dyld's own image (`far` ≈ `0x3a0xxxxx`) — a *different* process from the
1013-era dyld (`pc=0x2be…`). Still FS=15 WnR=1 USER, but **not the storm's `far=0xe6000`**. 1015's
verdicts key on `_far == 0xe6000` **and** the tail ordinal, never a whole-run aggregate
([[mi4-measurement-defects]]).

---

## 1. THE QUESTION, AND THE HYPOTHESES

> **PRIMARY (decisive).** Does the storm stop when the flush that should invalidate the storm's own
> translation is a **full TLBIALL** instead of an ASID-scoped TLBIASID?

* **H-G STALE_GLOBAL_TLB** (leading). A GLOBAL translation of `0xe6000` survives TLBIASID and
  shadows the RW leaf. **Confirm:** the storm stops when the flush is widened to `arm_tlb_flushID()`.
  **Kill:** it persists.
* **H-P PERMISSION_INPUTS** (fallback). The permission inputs themselves are wrong — domain/DACR
  (`_wddom`, `_wddacr`), TTBCR routing (`_wdttbcr`), the wrong pmap/table (`_wdthrmap`, `_wdttbr0`),
  or the leaf is not what 1014 decoded. **These are all CORRECT-looking in 1014** (a coarse L1 in
  domain 0, a present writable small page, TTBR0 live), so this is the ladder to run **if H-G dies**.
* **H-X ALL_INPUTS_CORRECT_STILL_FAULTS.** Every table input is right and the store still faults ⇒
  the port's model of the leaf→permission step is wrong. Next rung reads the CPU's own state (SCTLR.M
  / a vector-time DFSR shadow), never another table read.

**Ranking.** H-G leads because it is the only candidate that explains all three of 1014's cells *at
once* — writable leaf, ASID-matching flush that ran, **and** a recurring fault — and because the two
by-value facts in the header (never-`nG`, ASID-only steady-state flush) make it mechanically
available on this port. H-P is not displaced; it is the SAME ARM's second half (§2b) and the whole
ladder if H-G is refuted.

---

## 2. WHAT 1015 MEASURES — TWO HALVES IN ONE PRESS

### 2a. THE ACTIVE HALF: `STAGE90_XNU_USER_FULL_FLUSH` (the experiment)

**The change.** In `pmap_flush_tlbs`, inside `if (flush_self)` (`pmap.c:1343-1352`), when the switch
is on **and `pmap != kernel_pmap`**, take the **full** arm instead of the ASID arm:

```c
1343    if (flush_self) {                                  /* current */
1344        if (pmap_asid_ncpus) {
1345            pmap_asid_validate_cpu(pmap, my_cpu);
1346            if (pmap_is_shared)
1347                arm_tlb_flushID();
1348            else
1349                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);   /* <-- the only steady-state flush */
1350        }
1351        else
1352            arm_tlb_flushID();
1353    }
```

guarded `defined(STAGE90_XNU_USER_FULL_FLUSH) && STAGE90_XNU_USER_FULL_FLUSH && pmap != kernel_pmap`
⇒ `arm_tlb_flushID()` (TLBIALL `c8,c7,0`, `cpufunc-v7.s:82`), leaving the kernel pmap's behavior and
the `flush_self` decision **byte-identical**. **The `flush_self` gate stays** — 1014 measured it TRUE
at the storm (`_wfself=1`), so the widened arm is reached; if it had been FALSE no flush runs at all
and this experiment would be vacuous (§5).

**The read-out is the STORM, not a cell:** `_pcn` (the abort ordinal, `entry_stubs.c:2048`) reaching
~2^20 with `_pcfar == 0xe6000` is the 1014 shape; the storm "stopped" iff the tail no longer carries
`far=0xe6000` rows at high ordinals (§3's V-G1/V-G2 define this precisely).

**This is the ONE kernel-behaviour change the project has made since 986** — it is both a MEASUREMENT
(does H-G hold?) and the candidate FIX (§7). Stated plainly so the departure is not discovered late.

**Where it rides.** `pmap.c` is a **kernel-pool** file (`out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`),
and `pmap_flush_tlbs`'s 13 call sites are all **intra-object** (1014 measured this), so a `--wrap`
cannot fire. So the switch goes to `tools/build_xnu_arm_kernel.sh`'s `DEFINES` (where 1012's
`WRSTRIP`/1014's `FLUSH_WITNESS` go) and the tree text is staged by **extending
`tools/patch_d13_flushwitness.py`** with a second, independently-gated insertion (its own marker,
`1015: USERFULLFLUSH`) — same idempotence / whitespace-refusal / `--check` discipline.

### 2b. THE PASSIVE HALF: `STAGE90_XNU_PERM_WITNESS` (so one press answers both)

Read in the abort context (`__wrap_sleh_abort`, `entry_trace.c:5732`) **before** the handler, so the
values describe the fault as taken. **No new walker** — the L1 and leaf come from the ONE walker
`stage90_read_l2_pte` (`entry_stubs.c:2293`). All keys grep to **0** tree-wide.

| key | width | value | read at (file:line) | in |
|---|---|---|---|---|
| `xnu_live_abt_wdn` | 32 | the ladder ordinal (opens the row) | — | recorder |
| `xnu_live_abt_wdva` | 32 | the abort's `far` this row is keyed on | `g_abtpte_curva` | recorder |
| `xnu_live_abt_**leafng**` | 32 | **(T1) the leaf's bit11 — GLOBAL vs ASID-tagged, published RAW** | `(leaf >> 11) & 1u`, the W-WALK leaf read (`entry_stubs.c:3043`) | recorder |
| `xnu_live_abt_wdempty` | 32 | 1 iff the walk returned 0 (present-test refused) | `entry_stubs.c:2417` | recorder |
| `xnu_live_abt_wddacr` | 32 | **DACR raw at the abort** — `mrc p15,0,Rt,c3,c0,0` | new cp15 read (spelling at `entry_stubs.c:3824`) | recorder |
| `xnu_live_abt_wddom` | 32 | the L1 descriptor's domain field `(l1>>5)&0xF` | `out_l1_desc`, `entry_stubs.c:2380` | recorder |
| `xnu_live_abt_wdttbcr` | 32 | **TTBCR raw at the abort** — `mrc p15,0,Rt,c2,c0,2` | new read (`proc_reg.h:511`) | recorder |
| `xnu_live_abt_wdsplit` | 32 | `1u << (32u - (ttbcr&7u))`; `n==0` ⇒ published 0 | derived | recorder |
| `xnu_live_abt_wdttbr0` | 32 | live TTBR0 raw (`c2,c0,0`) | as `entry_stubs.c:2308` | recorder |
| `xnu_live_abt_wdttbr1` | 32 | live TTBR1 raw (`c2,c0,1`) | as `entry_stubs.c:2333` | recorder |
| `xnu_live_abt_wdl1` | 32 | the raw L1 word (pairs with `_ptel1`/`_wkl1`) | walk out-param | recorder |
| `xnu_live_abt_wdleaf` | 32 | the leaf from **ONE read** — all decodes below come from this local | walk return | recorder |
| `xnu_live_abt_wdap` | 32 | `(leaf>>4)&3u` | derived | recorder |
| `xnu_live_abt_wdapx` | 32 | `(leaf>>9)&1u` | derived | recorder |
| `xnu_live_abt_wdtex` | 32 | `(leaf>>6)&7u` | derived | recorder |
| `xnu_live_abt_wdcb` | 32 | `(leaf>>2)&3u` | derived | recorder |
| `xnu_live_abt_wdsmall` | 32 | `(leaf>>1)&1u` | derived | recorder |
| `xnu_live_abt_wdpcpu` | 32 | TPIDRPRW at the abort | `entry_trace.c:5754` reads the same register | recorder |
| `xnu_live_abt_wdthrmap` | 32 | the aborting thread's own pmap L1 base: TPIDRPRW → `thread+STAGE90_ACT_MAP` (`entry_saved_state.h:102`) → `+STAGE90_MAP_PMAP` (`:103`) → `+STAGE90_PMAP_L1_PHYS` (`:115`) | the 1011 chain, `entry_stubs.c:2487` | recorder |
| `xnu_live_abt_wdmapok` | 32 | **1 iff every deref landed in `[0x80000000, 0xFFFEFFFF]`** — the lineage's own null control | window tests at `entry_stubs.c:2493-2500` | recorder |
| `xnu_live_abt_wdseen` | 32 | **1 iff the cells were actually produced** (the `_wfseen` shape, `entry_stubs.c:3308`) | set at publish | recorder |
| `xnu_live_abt_wdctl` | 32 | **THE NULL CONTROL**: the control VA's domain + DACR bits at the SAME abort | beside W-SYNC's `_wcva` (`entry_stubs.c:3153`, published `:3170`) | recorder |
| `xnu_live_abt_wdctlok` | 32 | **1 iff the control read was taken**; **0 ⇒ withhold every passive verdict** | — | recorder |

**(T2) is ALREADY MEASURED — no new flush probe.** `xnu_live_abt_wfkind` (1014, `pmap.c`'s probe,
`entry_stubs.c:3275`) is 0=none / 1=full TLBIALL / 2=ASID-scoped. 1015 asserts it is **2** at the
storm (F2, 1014's measured value) and re-uses it unchanged. The 1014 arm's probe stays; 1015 adds
no second flush witness.

**(T3) is the experiment itself.** There is no TLB-read instruction, so the honest discriminator is
the hypothesis test: leaf present+writable (`_wdleaf`/`_wdapx`), flush ASID-only (`_wfkind==2`), leaf
GLOBAL (`_leafng==0`) — and then **poke**: take the full flush and see whether the fault recurs. That
is §3's V-G table; it is a *behaviour* read, not a cell read, and §5 says so.

**P5 (WnR/type/mode) already exists** — `xnu_live_abt_fsr` (`0x0000080f`: bit11 WnR = **1**; status
`0b01111` = `FAULT_PERM_P 0x0f`, `proc_reg.h:405`), `xnu_live_abt_type` (`4`),
`xnu_live_abt_abtcpsr` (`0x60000030`; `&0x1f = 0x10` = USER) — all published at the same ordinal as
`_pcn` (`entry_stubs.c:2048-2052`). No duplicate is added.

---

## 3. PRE-REGISTERED VERDICT TABLES

### 3a. PRIMARY — the experiment (decided by the STORM, on the tail)

The tail is read as: rows with `_pcn >= 0x400`; the storm exists iff any of them has
`_pcfar == 0x000e6000`. "Stopped" is a **definition**, not an eyeball: **fewer than 3 tail rows carry
`far=0xe6000`, AND no `far=0xe6000` row appears at `_pcn >= 0x4000`** (the 1014 storm's own rows run
to `0x40000`).

| # | reading on the 1015 arm | verdict | what it means |
|---|---|---|---|
| **V-G1** | `_pcfar=0xe6000` rows **STOP** at low ordinal, and `_wfkind == 1` is measured for the storm's pmap | **H-G CONFIRMED — STALE_GLOBAL_TLB** | the widened TLBIALL evicted the stale entry; the current RW leaf then served the store. The mechanism is the never-`nG` leaf + ASID-only flush. The fix is §7. |
| **V-G2** | the storm PERSISTS (`far=0xe6000` at `_pcn >= 0x4000`) with `_wfkind == 1` | **H-G REFUTED** | a full TLB invalidate ran for this pmap and the store still faults ⇒ no stale TLB entry is the cause ⇒ run the passive ladder (§3b). |
| **V-G3** | `_wfseen = 0` or `_wfkind = 0` on the storm's row | **EXPERIMENT VACUOUS** — no flush reached the widened arm (the `:1298` gate was FALSE) | the poke never happened; **no verdict** — rebuild (the `flush_self` precondition was not met on this run, unlike 1014's F2). |
| **V-G4** | the storm MOVES (`far` changes, or a *different* FS=15 VA appears at high `_pcn`) | **SYMPTOM_MOVED, NOT FIXED** | the invalidate changed the fault's address ⇒ the entry was TLB-resident but there are siblings; the next rung enumerates them. Not a confirmation of H-G as *the* cause. |
| **V-G5** | the run no longer reaches the storm at all (panics or stops earlier, no `_pcfar`) | **MEASUREMENT LOST** | the widened flush broke a different path ⇒ rebuild with the flush scoped **narrower** (§7's alternative) rather than concluding. |

**The NULL CONTROL for this table** is V-G3: a row where the widened arm was never reached
(`_wfkind != 1`) **cannot** be read as V-G2. The design refuses to conclude "not stale TLB" unless a
`_wfkind == 1` row is the *dominant* tail row for this VA — "the flush was refused, so of course it
still faults" is a different sentence from "a full flush ran and it still faults".

### 3b. SECONDARY — the passive ladder (same press; decides H-P only if V-G2)

Read on tail rows with `_wdn >= 0x400` and `_wdva == 0xe6000`. Withheld unless `_wdctlok=1`,
`_wdseen=1`, and (for the lineage cells) `_wdmapok=1`.

| # | condition | verdict |
|---|---|---|
| **P-CTL** | `_wdctlok=0` or `_wdseen=0` | **NO_PERM_WITNESS** — rebuild; no verdict |
| **P-D1** | `_wddom != 0` | **L1_DOMAIN_NONZERO** — the faulting L1 names a nonzero domain |
| **P-D2** | `_wddom == 0` and `(_wddacr & 3u) != 1u` | **DACR_NOT_CLIENT** — H-P CONFIRMED (the only DACR writer is `src/mmu.c:4921`) |
| **P-D3** | `_wddom == 0` and `(_wddacr & 3u) == 1u` | **DACR_CLIENT** — H-P's domain form KILLED |
| **P-T1** | `(_wdttbcr & 7u) != 2u` | **TTBCR_NOT_N2** — every `entry_ttbr_base` derivation (`entry_stubs.c:2150`) must be re-read |
| **P-T2** | `(_wdttbcr&7u)==2u` and `_wdva >= _wdsplit` | **ROUTED_TO_TTBR1** |
| **P-T3** | `_wdttbr0` (masked `2^(14-n)`) != `0x81a86000` | **TTBR0_MOVED_AT_ABORT** |
| **P-M1** | `_wdmapok=1` and `_wdthrmap` != `_wdttbr0` | **THREAD_PMAP_NE_TTBR0** — the aborting thread's pmap is not the walked table |
| **P-L1** | `_wdsmall=1` and (`_wdapx!=0` or `_wdap!=0b11`) | **LEAF_NOT_USER_WRITABLE** — contradicts 1014's stable double-read |
| **P-L3** | `_wdsmall=0` | **NOT_A_SMALL_PAGE** |
| **P-X1** | all of P-D3/P-T1..P-T3/P-M1/P-L1/P-L3 killed **and** V-G2 | **INPUTS_ALL_CORRECT_STILL_FAULTS** — the next rung reads the CPU's own state |

**A note on `_leafng` in this table.** `_leafng == 0` (GLOBAL) is **not** by itself a verdict — it is
the *precondition* of H-G, and it is published so the reader can see it without decoding the packed
leaf. `_leafng == 1` (non-global) would **kill H-G by value** before any press: if the leaf were
ASID-tagged, TLBIASID *would* evict its stale entry. So the checker asserts `_leafng == 0` at the
storm as the H-G precondition (the 1014 leaf's bit11 is 0, §0.1).

---

## 4. HOW 1015 RIDES THE EXISTING SWITCH SET

**Inherited VERBATIM — the 1014 set** (`scripts/build_1014_v1.sh`): entry side `ENTRY_TRACE` plus
`STAGE90_XNU_{ABT_SPAN,ABT_FAR,ABT_PC,ABT_PTE,VME_PROT,PMP_ENTER,WRSTRIP,PTE_WITNESS,FLUSH_WITNESS}`,
arm environment `STAGE90_XNU_{HFS,SP_ALIGN,VMHINT_GUARD,SUBMAP_PMAP_SKIP,COMMPAGE_ALIAS,IOS7_TRAPS,
DYLD_WITNESS,SHREG_PROBE,SHREG_WIRE,SHREG_GEOM,RESIDENT,USVC,STALL,CADENCE,KEXTB_LOGIN,ABORT_TRACE,
FREEZE_WITNESS}=1`.

**TWO new switches**, one per half:

* **`STAGE90_XNU_PERM_WITNESS`** (default 0) — **entry-toolchain only** (the passive half). Consumers:
  `entry_stubs.c` (the recorder) and `entry_trace.c` (the call). No `--wrap`, no pool edit. Guard =
  the full conjunction `ABT_FAR && ABT_PC && ABT_PTE && PMP_ENTER && PTE_WITNESS && PERM_WITNESS`
  (it keys on `g_abtpte_curva`, which only `PTE_WITNESS` writes — `build_entry.sh:1431`'s lesson).
* **`STAGE90_XNU_USER_FULL_FLUSH`** (default 0) — **kernel-pool** (the active half), exactly like
  1014's `FLUSH_WITNESS`: a `tools/build_xnu_arm_kernel.sh` `DEFINES` entry, tree text staged by
  `tools/patch_d13_flushwitness.py` (extended), and an assertion that
  `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o` carries the new literal and the new branch. Refusals:
  value `0|1` only; `USER_FULL_FLUSH=1` requires `FLUSH_WITNESS=1` (the experiment's read-out is
  `_wfkind`, which only that probe publishes) and the full 1010 ladder.

**The five plumbing sites per switch — all, or the press preflight REFUSES the arm:**

1. **`src/entry/entry_stubs.c`** — the `wd*` recorder; offsets from `entry_saved_state.h:102-115`,
   never a literal.
2. **`src/entry/entry_trace.c`** — the extern + ONE call in `__wrap_sleh_abort`, after
   `entry_note_abt_cur(far_)` (`:5774`), beside `entry_note_abt_wctx(far_)` (`:5784`), **before**
   `__real_sleh_abort`.
3. **`src/entry/build_entry.sh`** — (a) `ENTRY_ARM_KEYS` (append after `:1978`); (b) the record
   `case` arm (beside `:2249`); (c) an **explicit `echo "STAGE90_XNU_PERM_WITNESS=$PERMWIT"` /
   `echo "STAGE90_XNU_USER_FULL_FLUSH=$UFF"`** in the record writer (beside `:39978` — the one site a
   name-keyed diff does not visit and the only site the gate reads); (d) `STUB_DEFINES` +
   `STUB_DEFINES_TRACE` (`:334`, `:1392` precedent); (e) the REFUSALS above.
4. **`scripts/preflight_boot_check.sh`** — add **both** to `ENTRY_CFG_KEYS` (list at `:716-759`); the
   converse check (`:1181-1183`) refuses a record carrying a key the gate does not print.
5. **`scripts/build_1015.sh`** (new; shaped on `build_1014_v1.sh`) — `EXPORTS+=("STAGE90_XNU_PERM_
   WITNESS=1" "STAGE90_XNU_USER_FULL_FLUSH=1")`; a stale-arm refusal naming `armed-d13-29ec9e48`
   (`build_1013.sh:607-618`); and the assertions: image-wide **`(b|bl)`** count to `entry_note_abt_wd`
   == 1 (**`(b|bl)`, never `bl`** — the 1013 tail-call reader defect); each published `wd*` expression
   contains `_wdleaf` (single-read, the 1013 V6 rule); **OFF byte-identity** for BOTH entry objects
   **and** `osfmk_arm_pmap.o` (rebuild-with-0 + `cmp`, `build_1013.sh:1938-1939`); and a **negative
   control**: the frozen 1014 elf (`out/stage90/frozen/armed-d13-29ec9e48/xnu_arm_entry.elf`) carries
   **zero** `wd*`/`leafng` literals and **zero** `entry_note_abt_wd`, and the pre-patch
   `osfmk_arm_pmap.o` carries **zero** `STAGE90_XNU_USER_FULL_FLUSH`.

**Also owed (1014 §6), and free here:** `entry_note_abt_wleaf` (`:2928`) publishes `_wwnoproof` from a
`pm_l1_phys` re-read **after** the enter (`:2930`, the deliberate `pmap_expand_ttb` hazard). Publish
both pre- and post-call bases (`_wwpre`/`_wwpost`) so `_wwnoproof` is a reading, not a bare 1.

---

## 5. FALSIFICATION — what proves this rung wrong, and where it falls back

1. **A single variable, deliberately.** The arm carries BOTH halves but the ACTIVE clause is the only
   behaviour change: **the passive `wd*` cells change nothing the kernel does**, so a "storm stopped"
   reading is attributable to the flush widening and nothing else. Arming a *second* poke (e.g. an
   entry-side one-shot TLBIALL, §7's alternative) **in the same build** would make the stop
   unattributable and is therefore **forbidden** here — one variable per press
   ([[mi4-one-value-two-definitions]]).
2. **H-G is refuted by V-G2** (storm persists with `_wfkind==1`) — and then the passive ladder (§3b)
   is already measured in the same capture, so the fallback costs no second press.
3. **H-G dies before the press if `_leafng==1`** — a non-global leaf *would* be evicted by TLBIASID.
   1014's leaf is `bit11=0`, so this is a documented precondition, not a hope.
4. **V-G3 makes the experiment vacuous**: if no flush reached the widened arm (`_wfkind != 1`), the
   reading is "the poke never landed", **not** "stale TLB is refuted". The table refuses the
   conclusion in that case — the 1013 `WITNESS_METHOD_BROKEN` discipline.
5. **If V-G1 fires**, H-G is confirmed **for this VA on this run**; it does **not** by itself prove the
   mechanism is the *only* cause — V-G4 (the symptom moving) is the cell that catches siblings.
6. **The honest gap the lead named (T3).** There is no TLB-read instruction. This rung cannot *show*
   the stale entry; it shows that removing global translations for this pmap removes the fault. That
   is a strong, falsifiable, but indirect identification — and it is exactly why V-G2 (persist) is a
   real possible outcome and the passive ladder is armed alongside rather than after.

---

## 6. HONEST SCOPE

* **1015 is no longer a pure measurement** — `STAGE90_XNU_USER_FULL_FLUSH` changes what the kernel
  does (one branch, non-kernel pmaps only). Every rung from 998 to 1014 changed nothing; this one
  does, and the departure is deliberate because a passive witness of already-correct inputs cannot
  distinguish stale-TLB from anything else. If V-G1 fires the arm doubles as the fix (§7).
* **It proves the mechanism, not the full fix.** V-G1 says "a full invalidate for user pmaps removes
  the fault"; whether the *right* long-term fix is (a) `nG` tagging or (b) the widened flush is §7's
  trade-off, not this rung's result.
* **The passive half installs nothing** and is the same read-only witness class as 1014.
* **It does not settle §0.4's three interleaved faults**; the verdicts are about `far=0xe6000`.
* **It cannot read the hardware's abort status** (the `fsr` is Apple's `arm_ctx->fsr`, `trap.c:537`;
  `sleh_abort` never re-reads `c5,c0,0`). P-D2 catches a domain abort by *consequence*, not by
  reading the register.

---

## 7. CANDIDATE FIX TO PRE-SPECIFY (NOT built in 1015)

If V-G1 holds, the fix is one of two, and 1015 does **not** implement the choice — it reports which
of V-G1/V-G2 fired and leaves the selection open.

**(a) Emit `L2_NG_BIT` on every user leaf** — add `L2_NG_BIT` to the user branch of
`pmap_vm_prot_to_page_flags` (`pmap.c:1113`, guarded `pmap != kernel_pmap`), and to the other user
OR sites (`:3302`/`:3494`, both guarded by `BOARD_CONFIG_* || STAGE90_XNU_COMMPAGE_ALIAS` and needing
their own review). Then every user leaf is ASID-tagged and TLBIASID evicts it.

* **For:** targets the cause (the never-emitted bit) rather than papering over it; keeps TLBIASID's
  cheapness; matches what ARM expects for per-process mappings.
* **Against (wider blast radius):** it changes the *lifetime semantics* of every user mapping and
  interacts with ASID allocation/reuse — a recycled ASID with stale non-global entries is a new
  failure class (`pmap_asid.c` would need auditing). It also does not retire entries already GLOBAL
  in the live TLB.

**(b) Make `pmap_flush_tlbs` take `arm_tlb_flushID()` for non-kernel pmaps** — the `STAGE90_XNU_
USER_FULL_FLUSH` branch itself, made permanent.

* **For:** one branch, one gate, no change to PTE encoding or ASID semantics; it is exactly the
  experiment that proved the hypothesis, so confirmation and fix are the same code. It also sweeps
  *any* stale global entry, including ones already in the TLB.
* **Against:** a full TLBIALL on every user page-table update is coarser (performance), and it
  invalidates kernel-visible global translations too (the whole TLB, not just this pmap's ASID).

**Recommendation (stated, not decided): (b) is the more conservative first fix** — it is the smaller
diff, it changes no encoding, and it is the experiment already validated. If measurements then show
the cost, (a) is the principled follow-on. **Neither is built in 1015.**

*(Sources read by value this session: `pmap.c` `:1104-1129`, `:1343-1352`, `:2562-2563`, `:3296-3304`,
`:3488-3496`, `:3723`; `pmap.h` `:117-142`, `:207-231`; `pmap_asid.c` `:150-172`; `proc_reg.h`
`:385-405`, `:511`, `:530`; `cpufunc-v7.s` `:49-87`, `:166-170`; `trap.c` `:510-560`; `arm_vm_init.c`
`:475`; `src/mmu.c:4921`; `src/entry/entry_saved_state.h` `:102-115`; `src/entry/entry_stubs.c`
`:2131-2148`, `:2281-2425`, `:2464-2510`, `:2560-2574`, `:2880-2980`, `:3024-3060`, `:3125-3180`,
`:3275-3360`, `:3817-3934`; `src/entry/entry_trace.c` `:5690-5800`, `:5960-6030`;
`src/entry/build_entry.sh` `:334`, `:1361-1449`, `:1928-1979`, `:2246-2249`, `:39977-39978`;
`scripts/preflight_boot_check.sh` `:716-759`, `:1181-1183`; `scripts/build_1014_v1.sh` `:291-530`,
`:750-830`; `scripts/build_1013.sh` `:607-618`, `:1840-1946`; `docs/1014-design.md`;
`records/1014-press-read.txt`; capture
`out/stage90/captures/recovered-unknown-arm-20261015-092217-last_kmsg.txt` `:33475-34368`.)*