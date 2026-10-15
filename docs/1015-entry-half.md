# 1015 — the ENTRY-TOOLCHAIN half (HALF B): the passive permission-inputs witness

**Lane:** `src/entry/entry_stubs.c`, `src/entry/entry_trace.c`, `src/entry/build_entry.sh`,
`scripts/preflight_boot_check.sh`. **No device action was taken. Nothing under `external/` was read or
edited.** The ACTIVE half (the widened flush, the kernel pool) is the other lane's and is not touched
here.

**Switch:** `STAGE90_XNU_PERM_WITNESS` (0|1). **Recorder:** `entry_note_abt_wd(uint32_t va)`.
**Guard:** the FULL CONJUNCTION, spelled `defined()`-first —
`ABT_FAR && ABT_PC && ABT_PTE && PMP_ENTER && PTE_WITNESS && PERM_WITNESS`.

---

## 1. The edits, by site

### 1.1 `src/entry/entry_stubs.c` (recorder + the owed 1014 §6 fix)

| What | Where | Notes |
|---|---|---|
| `uint32_t g_abtpte_wleaf_pre;` under the PERM_WITNESS guard | `:2912` | the PRE-call `pm_l1_phys` sample `entry_trace.c` fills |
| the `_wwpre`/`_wwpost`/`_wwmoved` block inside `entry_note_abt_wleaf` | `:3004-3013` | the owed fix, §1.3 |
| the recorder region (comment, width macros, `g_abt_wdn`, the function) | guard `:3467`, `#endif` `:3804` | one contiguous guarded span |
| `entry_note_abt_wd` definition | `:3497-3804` | 23 publishes, `:3736-3773`; then the 5-key AT group, `:3798-3802` |

The recorder keys on `g_abtpte_curva` exactly as `entry_note_abt_wleaf` (`:2941`) and W-WALK
(`:3037`) do — a foreign VA returns before the ordinal advances, so `_wdn` counts only THIS fault's
aborts. Throttle is the lineage's `n <= 4u || (n & (n - 1u)) == 0u`.

**The ONE walker.** The L1 and the leaf come from the existing `stage90_read_l2_pte` (reached through
the same walk the W-LEAF region uses). No walker is re-spelled. All six derived cells are computed
from the SINGLE local `wdleaf` (the 1013 V6 rule) — `grep` confirms `wdleaf` is named 12× and every
derived cell reads it (`wdleaf` is the name `docs/1015-design.md` §2 and `build_1015.sh:2490` require;
an earlier revision of this file used a bare `leaf` local, corrected to match).

**Offsets.** The 1011 (A) chain (`TPIDRPRW → thread+STAGE90_ACT_MAP → map+STAGE90_MAP_PMAP →
pmap+STAGE90_PMAP_L1_PHYS`) cites the named macros in `src/entry/entry_saved_state.h:102-115`; no
literal offset appears. `_wdmapok` is that chain's own null control (`1` iff every deref landed in
`[0x80000000, 0xFFFEFFFF]`). `_wdctl`/`_wdctlok` read the control VA's domain + DACR bits through the
same walker; `_wdctlok = 0` withholds every verdict.

**The DACR** is READ (`mrc p15,0,Rt,c3,c0,0`), never written: `grep -c 'c3, c0, 0' src/mmu.c` is
still **2** (its own read + its one write, unchanged), and `pmap.c` is **0**.

### 1.2 `src/entry/entry_trace.c`

| What | Where |
|---|---|
| the guarded `extern void entry_note_abt_wd(uint32_t va);` | `:5726-5728` |
| the guarded `entry_note_abt_wd(far_);` call | `:5808-5810` |
| guarded `extern uint32_t g_abtpte_wleaf_pre;` | `:6027` |
| guarded PRE-call store `g_abtpte_wleaf_pre = pm_l1_phys;` | `:6047` |

Call order inside `__wrap_sleh_abort` (by value in the file): `entry_note_abt_cur(far_)` `:5784` →
`entry_note_abt_wctx(far_)` `:5794` → `entry_note_abt_wd(far_)` `:5812` → `recover = …` `:5828` →
`entry_note_sleh(…)` `:5830` → `__real_sleh_abort(…)`. The 1007 read-before-the-anchor rule is
respected: 1015 inserts ABOVE the contiguous `recover = …; entry_note_sleh(…); __real_sleh_abort(…);`
span that `tools/check_fault_recovery.py` mutates, so that anchor is byte-identical.

### 1.3 The owed 1014 §6 fix (`_wwnoproof`)

`entry_note_abt_wleaf` published `_wwnoproof` from a `pm_l1_phys` re-read AFTER
`__real_pmap_enter_options` — a POST-call sample compared against another POST-call word, which
cannot disagree with itself about whether the enter MOVED the table. Now:

- `entry_trace.c:6047` samples `*pmap` into `g_abtpte_wleaf_pre` from the SAME word the existing
  `pm_l1_phys` local was read from, BEFORE the call, entirely under the PERM_WITNESS guard;
- `entry_stubs.c:3004-3013` publishes `_wwpre` (PRE), `_wwpost` (POST) and `_wwmoved` (1 iff the two
  4 KB-aligned bases differ). `_wwnoproof` is left publishing `proof` UNCHANGED — a reading ADDED
  beside a claim, never a claim replaced by a shrug.

### 1.4 `src/entry/build_entry.sh` — the five plumbing sites

| # | Site | Line |
|---|---|---|
| 1 | `STAGE90_XNU_PERM_WITNESS="${…:-0}"` appended to `STUB_DEFINES_TRACE` | `:334` |
| 2 | `PERMWIT` validation (`0|1`) + the five cross-refusals + `STUB_DEFINES+=(-DSTAGE90_XNU_PERM_WITNESS="$PERMWIT")` | `:1452-1488` |
| 3 | `STAGE90_XNU_PERM_WITNESS` appended to `ENTRY_ARM_KEYS` after `STAGE90_XNU_FLUSH_WITNESS` | `:2016` |
| 4 | record-case arm `STAGE90_XNU_PERM_WITNESS)     _v=$PERMWIT ;;` | `:2293` |
| 5 | record writer `echo "STAGE90_XNU_PERM_WITNESS=$PERMWIT"` | `:40027` |

The five refusals name every conjunct of the guard (`ABT_FAR`, `ABT_PC`, `ABT_PTE`, `PMP_ENTER`,
`PTE_WITNESS`), each with the reason. No `--wrap`, no `TRACE_LDFLAGS` clause: `grep -cE` for a
1015-gated wrap is **0**.

### 1.5 `scripts/preflight_boot_check.sh`

`STAGE90_XNU_PERM_WITNESS` added at the tail of `ENTRY_CFG_KEYS` beside `STAGE90_XNU_FLUSH_WITNESS`
(`:760`), now **65** keys, with a comment noting the "fifteen variants / pair of counts" prose was
retired by 1005 and the count is not re-pinned. Without this the CONVERSE check at `:1194-1197`
would refuse any record carrying the key.

---

## 2. Verification (all by value, on this host; no device)

### 2.1 The keyspace was free BEFORE the edit

`git grep -c` at **HEAD**, excluding `docs/` and `records/`, returns **0** for the switch name and
for all 23 `wd*`/`leafng` keys. Nothing was overwritten.

### 2.2 Compile (the edited tree)

Both objects of both arms compile **clean** (`-Werror -Wall -Wextra`, the exact `cortex-a15`
`-marm -O2` line `build_entry.sh` uses, the arm switch set read VERBATIM from
`out/stage90/frozen/armed-d13-29ec9e48/xnu_arm_entry-config.txt`): 0 lines in all four `.err` files
for `PERM_WITNESS=1` and `PERM_WITNESS=0`.

### 2.3 OFF byte-identity — HOLDS

HEAD's `entry_stubs.c`/`entry_trace.c` (`git show HEAD:…`) compiled with `PERM_WITNESS=0` vs the
edited files compiled with `PERM_WITNESS=0`, same command line:

```
entry_stubs.o   HEAD(0) == EDITED(0)   IDENTICAL   (164572 B both)
entry_trace.o   HEAD(0) == EDITED(0)   IDENTICAL   ( 30312 B both)
```

And the switch is **not inert**: the ON objects differ from OFF (`entry_stubs.o` 167356 B,
`entry_trace.o` 30424 B).

> **A false IDENTICAL was caught and corrected.** The first attempt had no `-I <XNU_TREE>/osfmk` and
> no `-I src/entry`, so BOTH HEAD and edited compiles errored
> (`fatal error: entry_saved_state.h: No such file`), `mv` moved stale objects, and `cmp` compared
> two artifacts from an earlier run. The harness now fails loudly on a nonzero compile and the
> comparison is run only over objects whose `.err` is 0 bytes.

### 2.4 The two key lists agree with the emitted row

`tools/check_1015_capture.py`'s `WD_LADDER_KEYS` and the literal list in `scripts/build_1015.sh`
(`:2509-2514`) are the **same 23 names in the same order**. The recorder now emits them in that
order, verbatim:

```
emitted: 23  want: 23   missing []   extra []   dupes []   ORDER MATCHES: True
```

(The parser assigns by key name, so a reordering would not have broken it today — which is exactly
why the order is matched rather than relied on. The recorder carries a comment saying a change here
must be made in `WD_LADDER_KEYS` in the same edit.)

### 2.5 The plumbing checks

- `ENTRY_ARM_KEYS` ⇄ record-case two-way: **63 array entries, 63 arms, 0 keys without an arm**;
  `PERM_WITNESS` present on both sides.
- The preflight converse set-difference against a synthetic record (the frozen 1014 record's 64
  `STAGE90_*` lines + `STAGE90_XNU_PERM_WITNESS=1`): **UNSHOWN = []**.
- The other lane's `scripts/build_1015.sh` static clauses, re-run:

| clause | want | got |
|---|---|---|
| (a) PERM_WITNESS guard count, `entry_stubs.c` | 1 | **1** |
| (a) PERM_WITNESS guard count, `entry_trace.c` | 2 | **2** |
| (a) `entry_note_abt_wd(` in stubs | ≥1 | **1** |
| (a) `entry_note_abt_wd(far_);` in trace | 1 | **1** |
| (a) `wdleaf = ` in stubs | ≥1 | **1** |
| (a) `wdleaf` in stubs | ≥5 | **13** |
| (c) `STUB_DEFINES_TRACE` append | 1 | **1** |
| (c) `-DSTAGE90_XNU_PERM_WITNESS="$PERMWIT"` | 1 | **1** |
| (c) record writer echo | 1 | **1** |
| (c) the `PERMWIT -eq 1 && PTEWIT -ne 1` refusal | 1 | **1** |
| (c) a 1015-gated `--wrap` | 0 | **0** |

Brace balance of both edited files is unchanged vs HEAD (0/0).

### 2.6 The other lane's clauses — what I re-measured this session

I did **not** edit `scripts/build_1015.sh` (not my lane). I re-ran its exact greps against the
current tree, because it is the script that will certify the arm:

**RESOLVED by the other lane (their edits landed):**
- `:2504`'s symbol-typo clause is gone; it now greps the key literal `xnu_live_abt_wdctl`.
- `:2537` now reads the record-case arm **whitespace-insensitively**
  (`grep -cE 'STAGE90_XNU_PERM_WITNESS\)[[:space:]]+_v=\$PERMWIT ;;'`), so the five-space spelling
  cannot resurrect the defect. Verified: 1 hit.
- `:2504`-class clause (b2) now names the **five** AT keys including `wdctlat`.

**STILL FAILING on its own literal — the `d15_at_src` clause** (the other lane is editing this file
live: it was `:2532` when I first read it, `:2562` a moment later, so I cite the variable, not a
line):
The clause greps the **symbol** regex `entry_note_abt_wd(at(par|parpr|ctlat|ok)?|sctlr)` and wants
≥ 5 hits. **No such symbols exist anywhere in the tree** (verified: zero hits): the ONE recorder is
`entry_note_abt_wd` and `wdatpar`/`wdctlat`/… are *keys*. So the clause reads **0** and the build
would abort with `FAIL: … names only 0 of the five AT/SCTLR keys …`. Worse, even read as keys the
alternation is still short: `grep -cE 'xnu_live_abt_wd(at(par|parpr|ctlat|ok)?|sctlr)'` is **4**, not
5, because the keys are `xnu_live_abt_wdsctlr` — the `wd` prefix before `sctlr` is not in the
pattern (`…_wd(at…|sctlr)`, not `…_wd(at…)|xnu_live_abt_sctlr`). The correct grep is the key literal
set, `xnu_live_abt_wdatok|wdatpar|wdatparpr|wdctlat|wdsctlr`.

This is [[mi4-a-claim-in-a-comment-is-not-a-check]] running **backwards**: a check whose literal is
wrong, not code that is missing. The images are right — the ON object carries all five key literals
(section 2.8, verified with `strings`), and every other clause of `build_1015.sh` that I re-ran
passes (section 2.7). Reported, not worked around.

---

## 2.7 TASK E3 — re-run of the other lane's assertions against the E2 tree

I re-ran `scripts/build_1015.sh`'s own greps/clauses against the post-E2 source, verbatim. Every one
that can be evaluated without a linked image passes; the ONE exception is `:2532` (§2.6), which fails
on its own literal text, not on this code.

| clause | site | want | measured |
|---|---|---|---|
| the six-way `defined()`-first guard | `entry_stubs.c` | 1 | **1** |
| the same guard | `entry_trace.c` | 2 | **2** |
| `entry_note_abt_wd(` defined | `entry_stubs.c` | ≥1 | **1** |
| `entry_note_abt_wd(far_);` call | `entry_trace.c` | 1 | **1** |
| the leaf bound to a local `wdleaf = ` | `entry_stubs.c` | ≥1 | **1** |
| `wdleaf` named (single-read rule) | `entry_stubs.c` | ≥5 | **12** |
| `xnu_live_abt_wdctl` key published | `entry_stubs.c` | ≥1 | **3** |
| the five AT key literals in the ON object | `strings` | 5 | **5** |
| `STAGE90_XNU_PERM_WITNESS="${…:-0}"` in STUB_DEFINES_TRACE | `build_entry.sh` | 1 | **1** |
| `-DSTAGE90_XNU_PERM_WITNESS="$PERMWIT"` in STUB_DEFINES | `build_entry.sh` | 1 | **1** |
| record-case arm (whitespace-insensitive) | `build_entry.sh` | 1 | **1** |
| the explicit record `echo` | `build_entry.sh` | 1 | **1** |
| refuses `PERM_WITNESS=1` without `PTE_WITNESS=1` | `build_entry.sh` | 1 | **1** |
| `STAGE90_XNU_PERM_WITNESS` in `ENTRY_CFG_KEYS` | `preflight_boot_check.sh` | present | **present** |
| ENTRY_ARM_KEYS ↔ record-case: no record arm orphaned | `build_entry.sh` | 0 | **0** |
| preflight converse: no `STAGE90_XNU_*` arm key unshown | both | 0 | **0** |

The DACR census clauses (`src/mmu.c` ×2, `pmap.c` ×0) are in the same script and are unaffected by
E2 — 1015 only *reads* the DACR, through `proc_reg.h`, and adds no writer.

---

## 2.8 THE AT/PAR/SCTLR CELLS (the decisive hardware instrument) — added on the lead's follow-up

The design's §2b K6/K7 and §3c: the ONE cell that asks the CPU's **own translation path** instead of
reading a page table. `AT` (Address Translate) walks the VA "as if" the access were being made — and
on this core **may be satisfied from TLB entries**, which is the point — and returns the `PAR` the MMU
itself uses. `PAR` bit0 = 1 ⇒ the MMU **faults** that access; bit0 = 0 ⇒ `PAR & 0xfffff000` is the PA.

### The spellings are the tree's own, never re-written

| cell | macro | `proc_reg.h` | cp15 |
|---|---|---|---|
| `_wdatpar` | `armreg_va2pa_uw_ns_write` | `:547` | `mcr p15,0,%0,c7,c8,3` (AT, stage-1, user WRITE) |
| `_wdatparpr` | `armreg_va2pa_pr_ns_write` | `:544` | `mcr p15,0,%0,c7,c8,0` (AT, privileged READ) |
| — (both) | `armreg_par_read` | `:522` | `mrc p15,0,%0,c7,c4,0` |
| `_wdsctlr` | `armreg_sctrl_read` | `:500` | `mrc p15,0,%0,c1,c0,0` |

> **The tree spells SCTLR's macro `sctrl`, not `sctlr`.** There is no `armreg_sctlr_read`; a grep for
> that name in `proc_reg.h` returns nothing (`ARMREG_READ_INLINE(sctrl, "p15,0,%0,c1,c0,0")` at `:500`).

The sequence is `pmap.c:3039-3051`'s own: write the VA → `isb sy` (the ISB the AT/PAR pair REQUIRES,
`pmap.c:2432`'s `"xisb sy"`) → read the PAR. `par & 1 == 1` is a fault, `par & 0xfffff000` the PA —
exactly as `pmap_extract` reads it (`pmap.c:3051-3057`). Both PARs are read **once** into their own
locals and published **RAW**; no verdict is computed in the recorder.

### The include gate had to be widened — and a PERM-only arm is the case that proves it

`entry_stubs.c`'s `#include <arm/proc_reg.h>` was gated on `FLUSH_WITNESS` alone. **`PERM_WITNESS`
does not require `FLUSH_WITNESS`** (its refusal set is ABT_FAR/ABT_PC/ABT_PTE/PMP_ENTER/PTE_WITNESS),
so a legal `PERM=1 FLUSH=0` arm would reach the compiler with no `<arm/proc_reg.h>` on the path.
MEASURED on that exact arm:

```
PERM=1 FLUSH=0, no -I :  fatal error: arm/proc_reg.h: No such file or directory
PERM=1 FLUSH=0, with -I:  compiled clean
```

Two edits close it: the C include gate becomes `FLUSH_WITNESS || PERM_WITNESS`, and
`build_entry.sh` appends the SAME `-I` to the ONE `FLWIT_INCLUDES` array for `PERM_WITNESS` — but
**at `:1493`, after `$PERMWIT` is assigned and validated**, not beside the array's own `:1446` block,
because reading `$PERMWIT` at `:1446` would be reading it before its own assignment. Run through the
real gate logic:

| arm | includes appended |
|---|---|
| `PERM=1 FLUSH=0` | 1 (`-I …/osfmk`) |
| `PERM=1 FLUSH=1` | 1 (no duplicate) |
| both 0 | **0** — the OFF arm stays byte-identical |

### The AT row's write order is LOAD-BEARING, and the first attempt was wrong

The checker's `at_records` opens a row only when it sees `_wdatok`; it binds that key and every AT key
that **follows**. MEASURED by running the real parser over synthetic captures:

```
order  wdsctlr, wdatok, wdatpar, wdatparpr              ->  wdsctlr = ?          (the cell VANISHES)
order  wdatok, wdatpar, wdatparpr, wdctlat, wdsctlr     ->  wdsctlr = 0x00c5387d (correct)
order  wdatok, wdsctlr, wdatpar, wdatparpr, wdctlat     ->  wdsctlr = 0x00c5387d (correct)
```

My first cut wrote `_wdsctlr` first — the parser assigns by name, so this was **not** a parse error:
the SCTLR reading silently disappeared and §3c's re-rank would have been scored without its
CPU-state half. The recorder now emits exactly `AT_KEYS = (wdatok, wdatpar, wdatparpr, wdctlat,
wdsctlr)`, verified equal to the checker's own tuple. End-to-end, the checker's `s_at` on a synthetic
storm row now reads all **five** cells, passes T-CTL on `_wdctlat`, and reaches its verdict
(V-AT1/V-AT3 on the two fixtures; V-AT4 on the faulting-both-ways one).

### What the two PARs are AT — and the T-CTL control, now its own key

Both PARs are taken **against the storm VA** (`va`, the fault's own `far_`). Design §3c also names a
control: issue the SAME AT pair against the W-SYNC control VA (`gPhysBase + ABTWS_WINDOW_OFF`) and
require `par & 1 == 0` with `par & 0xfffff000 == 0x80080000` — "if it does not come back as expected,
**no V-AT verdict may be read**". That control is now implemented (TASK E2).

**RESOLVED — the VA and the PA are two numbers, and both are right.** The lead's follow-up says the
control VA's privileged AT "must return PA `0x80080000`"; the recorder computes `ctlva =
gPhysBase + ABTWS_WINDOW_OFF`. These looked like a mismatch until the runtime value of `gPhysBase`
was read. `entry_stubs.c:118`'s `0x00200000` is a **link-time stand-in**, guarded by
`#ifndef STAGE90_ENTRY_REAL_ARM_INIT` and replaced by XNU's own `arm_vm_init.c:351-353`, which
assigns the globals from `boot_args`; the recorder's own comment (`:2233`) states the value it
actually holds: **`gPhysBase + 0x80000 = 0x80080000`**. So at runtime `gPhysBase = 0x80000000`,
`ctlva = 0x80080000`, and the boot identity-maps it VA→PA, so the VA (0x80080000) and the PA
(0x80080000) are the same number. No discrepancy; nothing was guessed.

**The control is a SEPARATE KEY, `_wdctlat`** — the lead's 2026-10-15 ruling, already enacted in the
checker (`AT_CTL_KEY`, `AT_KEYS` is now FIVE keys). Design §3c's first draft gave `_wdatparpr` two
meanings (the storm VA's privileged read AND the control read) — [[mi4-one-value-two-definitions]] —
and the checker used to catch it indirectly, with a V-AT0 withhold when `_wdatparpr`'s PA collided
with `AT_CTL_PA`. The ruling splits them: `_wdatparpr` keeps the **storm VA's** privileged read, and
the control VA's AT is published as `_wdctlat`. The checker's `s_at` now checks `_wdctlat` **directly**
(`:854`) *as well as* the recorder folding it into `_wdatok` — "a gate published as a single bit is a
status the reader cannot audit" ([[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]).

`_wdatok` is therefore 1 **only if** all three pairs ran **and** the control came back as required
(`entry_stubs.c:3713`); the checker's V-AT0 withholds every V-AT verdict otherwise.

MEASURED end-to-end, running the REAL checker (`tools/check_1015_capture.py`) over synthetic captures
in the console's actual one-key-per-line format, with the emitted key order read verbatim from
`entry_stubs.c`:

```
[PASS]   T-CTL (`_wdctlat=0x80080000`, the W-SYNC control VA): MMU allows -> PA=0x80080000
[PASS] the AT leg RAN at the storm: user-WRITE par=0x0000080f (MMU FAULTS), priv-READ ... allows
VERDICT: V-AT1 - H-G CONFIRMED AT THE HARDWARE        (faulting-user-view fixture)
VERDICT: V-AT3 - WRONG TRANSLATION IN THE TLB          (allowing-user-view fixture)
```

and, in the collision shape the ruling removed, the checker's defensive carry-over still fires (it
refuses a `_wdatparpr` naming `AT_CTL_PA`, "the recorder has not made the change").

**The VA is computed unconditionally** (`:3615`, outside the `PTE_WITNESS` branch) and the control
walk/packing runs there too: `_wdctlat` ATs this same VA, so a VA computed only under `PTE_WITNESS`
would let a `PERM=1 PTE_WITNESS=0` arm AT address 0 and publish a control of the wrong page — a
control that cannot fail, which is worse than no control. (A `PERM_WITNESS=1` arm cannot in fact
reach that state — the build refuses `PERM_WITNESS=1` without `PTE_WITNESS=1` — but the cell does not
depend on that refusal holding.)

---

## 3. What the recorder publishes — the 23-key row, then the AT/SCTLR group

| # | key | source | line |
|---|---|---|---|
| 1 | `xnu_live_abt_wdn` | the ladder ordinal | `:3736` |
| 2 | `xnu_live_abt_wdva` | the `va` this fault published | `:3737` |
| 3 | `xnu_live_abt_leafng` | `(leaf>>11)&1` — RAW, the H-G precondition | `:3739` |
| 4 | `xnu_live_abt_wdempty` | `leaf == 0` | `:3740` |
| 5 | `xnu_live_abt_wddacr` | `mrc p15,0,Rt,c3,c0,0` | `:3743` |
| 6 | `xnu_live_abt_wddom` | `(l1>>5)&0xf` — the L1 domain | `:3744` |
| 7 | `xnu_live_abt_wdttbcr` | `mrc p15,0,Rt,c2,c0,2` | `:3745` |
| 8 | `xnu_live_abt_wdsplit` | TTBCR.N → the TTBR0/TTBR1 split | `:3746` |
| 9 | `xnu_live_abt_wdttbr0` | walk out-param | `:3747` |
| 10 | `xnu_live_abt_wdttbr1` | `mrc p15,0,Rt,c2,c0,1` | `:3748` |
| 11 | `xnu_live_abt_wdl1` | walk out-param | `:3751` |
| 12 | `xnu_live_abt_wdleaf` | walk return — THE SINGLE READ | `:3752` |
| 13 | `xnu_live_abt_wdap` | `(leaf>>4)&3` | `:3757` |
| 14 | `xnu_live_abt_wdapx` | `(leaf>>9)&1` | `:3758` |
| 15 | `xnu_live_abt_wdtex` | `(leaf>>6)&7` | `:3759` |
| 16 | `xnu_live_abt_wdcb` | `(leaf>>2)&3` | `:3760` |
| 17 | `xnu_live_abt_wdsmall` | `(leaf>>1)&1` — the TYPE bit | `:3761` |
| 18 | `xnu_live_abt_wdpcpu` | TPIDRPRW at the abort | `:3765` |
| 19 | `xnu_live_abt_wdthrmap` | the 1011 chain's final deref | `:3767` |
| 20 | `xnu_live_abt_wdmapok` | 1 iff every deref landed | `:3768` |
| 21 | `xnu_live_abt_wdseen` | 1 — the `_wfseen` shape | `:3771` |
| 22 | `xnu_live_abt_wdctl` | control VA's domain + DACR bits | `:3772` |
| 23 | `xnu_live_abt_wdctlok` | 1 iff the control read was taken | `:3773` |

Plus the **five** AT/SCTLR keys, written in `AT_KEYS` order right after `_wdctlok` (§2.8):

| # | key | source | line |
|---|---|---|---|
| 24 | `xnu_live_abt_wdatok` | 1 iff all three AT/PAR pairs ran **and T-CTL was satisfied** — the leg's gate (checker V-AT0) | `:3798` |
| 25 | `xnu_live_abt_wdatpar` | PAR after AT **user-write** of the storm VA | `:3799` |
| 26 | `xnu_live_abt_wdatparpr` | PAR after AT **privileged-read** of the same VA | `:3800` |
| 27 | `xnu_live_abt_wdctlat` | **T-CTL**: PAR after AT **privileged-read** of the W-SYNC control VA (`_wdctlat`) | `:3801` |
| 28 | `xnu_live_abt_wdsctlr` | `armreg_sctrl_read()` at the abort — SCTLR raw | `:3802` |

Plus the three added by the owed fix (a separate, throttled group beside `_wwnoproof`):
`xnu_live_abt_wwpre`, `xnu_live_abt_wwpost`, `xnu_live_abt_wwmoved` (`:3009-3011`).

So the recorder publishes **28 distinct keys**: the 23-key ladder row (keyspace `WD_LADDER_KEYS`), the
**5-key AT/SCTLR group** (`AT_KEYS`), and the 3 `_ww*` cells. The 23-key row's own order is unchanged
and still matches `WD_LADDER_KEYS` verbatim — the AT group is a SEPARATE flat record after it, which
is how `at_records` reads it. (`WD_LADDER_KEYS` stays at 23: the AT keys are *not* members of it —
they are their own `AT_KEYS` tuple in the checker, so the "in order" requirement is met by emitting
`AT_KEYS`' order, which is what `at_records` binds.)

**How `_wdatok`'s T-CTL gate is written** (`:3713`) — the control is satisfied iff it does not fault
**and** its PA is the control VA's own frame:

```c
if ((atctl & 1u) == 0u && (atctl & 0xfffff000u) == (ctlva & 0xfffff000u))
    atok = 1u;
```

The expected PA is compared against `ctlva` masked, **not** the literal `0x80080000` the checker
hard-codes: the recorder must not transcribe the answer it is testing for. The checker still checks
the literal independently, so the two readings are independent by construction.

---

## 4. Summary

- **OFF byte-identity HOLDS** — `entry_stubs.o` and `entry_trace.o` are `cmp`-identical to HEAD at
  `PERM_WITNESS=0`, over compiles that both succeeded.
- **ON compiles clean** and is non-inert; the 23-key row is emitted in `WD_LADDER_KEYS` order.
- **All five plumbing sites** are in and **the two-way / converse checks pass**.
- **The owed `_wwpre`/`_wwpost` fix** is in on both sides of the wrapper.
- **The AT/PAR/SCTLR/T-CTL hardware cells** (§2.8) are published in `AT_KEYS` order — the decisive,
  press-independent instrument from `docs/1015-perm-static.md` Item 6. The T-CTL control is its own
  key (`_wdctlat`) per the lead's ruling, and `_wdatok` carries it.
- **TASK E2/E3 done**: the 5-key AT group is emitted in the checker's own order and read end-to-end
  by the real `s_at` (T-CTL PASS, V-AT1/V-AT3 reached); OFF byte-identity re-verified after the edit.
- **Three clauses in `scripts/build_1015.sh`** (not my lane) fail on their own literal text — reported
  in §2.6, not worked around.