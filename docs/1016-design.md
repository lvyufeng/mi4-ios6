# 1016 — the rung AFTER the 1015b read: BOTH branches pre-registered

**This is a rung design, not a build.** No build, no press, no device action, and no `git add` was
performed while authoring it, and it touches exactly this one file.

> **STATUS ON WRITE: the 1015b verdict cell is UNREAD.** `records/1015b-press-read.txt` records that
> the second `scripts/press_1015b.sh` DID execute the full press (arm `armed-d13-16fe8885`, the fix is
> in the pool — `kernel_pmap` materializations x3, `@0x440`/`@0x560`/`@0x608`), the run **RAN and the
> device went RESIDENT** (`run_and_capture.sh` exit 2 = resident non-return, the expected shape), and
> the log has **NOT** been recovered — the device is dark and needs the operator's power press. **No G
> cell may be claimed from this run yet.** This document therefore pre-registers **both** continuations,
> A and B, so whichever the recovered log shows, the next arm is already specified.
>
> Every number below is either **MEASURED** (taken from an artifact on disk, with the command in the
> text) or marked `# UNMEASURED — measure at first build/session`. This project's most-repeated defect
> class is a stale or unsatisfiable number ([[mi4-measurement-defects]]); an unmarked number in this
> file is a bug in this file.

---

## 0. The fork, and the ONE thing that decides it

**1015 proved the diagnosis at the hardware; 1015b asks the behaviour question the diagnosis cannot
answer.** 1015's press returned **V-AT1** (`records/1015-press-read.txt` §2): the MMU's own AT of the
storm's VA **faults for a user write** (`_wdatpar = 0x0000001f`, bit0 = 1) while the **privileged read
translates it successfully** (`_wdatparpr = 0x8419d268`, bit0 = 0) — and that privileged translation
**lands on a DIFFERENT physical page than the walked leaf** (`0x8419d000` vs the leaf's `0x8417e000`).
1015b is the deferred behaviour half (`docs/1015-design.md` §2b, `docs/1015b-design.md`): it makes
`pmap_flush_tlbs` take a **full `arm_tlb_flushID()` (TLBIALL)** for every non-kernel pmap, so that a
**GLOBAL** TLB entry (which TLBIASID cannot evict) can be swept, and its **pre-registered** read-out is:

> **G-CONFIRMED = the storm STOPS** (H-G confirmed in behaviour) vs **G-REFUTED = the storm persists**
> (H-G refuted in behaviour).

`docs/1015-design.md` §3d ("THE STITCH") already names the two one-way doors this fork opens:

| V-AT (1015, measured) | V-G (1015b, the pending read) | combined | next rung |
|---|---|---|---|
| V-AT1/V-AT3 | **V-G1 storm STOPS** | **CONFIRMED, CONSISTENT** | **§3 (Branch A)** — take the fix, advance userspace |
| V-AT1/V-AT3 | **V-G2 storm persists** | **CONFIRMED-TRANSLATION, UNFIXED-BY-ONE-FLUSH** | **§4 (Branch B)** — the observer, not the pmap |

### 0.1 The deciding key — by value, never a literal

**The branch is chosen by `tools/check_1015b_capture.py` and nothing else.** That reader
(`tools/check_1015b_capture.py`, the `[1015b] VERDICT:` cell, `b_g_verdict()` ≈ `:403-560`) is the
**single owner of the adjudication**; this design does **not** redefine its table, it names which of
its cells is the gate:

* **G-CONFIRMED** — the stop definition holds: **fewer than `STORM_MIN_ROWS` (= 3) storm tail rows
  AND no storm row at `_pcn >= STORM_MIN_ORD` (= `0x4000`)** (`check_1015b_capture.py:222-223`),
  with the capture **structurally complete** (terminal marker `No errors detected` PRESENT and the
  `xnu_live_abt_*` family + the `_pcn` ladder present — `:505-560`).
* **G-REFUTED** — `>= 3` storm tail rows **OR** any storm row at `_pcn >= 0x4000`; a **positive**
  measurement that must point at a storm **ROW**.
* **G-WITHHOLD** — no tail / a truncated capture / a named missing key. **Never a negative.** A log
  that ends is not a storm that stopped.

**The storm is identified BY VALUE, never by a literal VA.** `classify_storm()`
(`tools/check_1015_capture.py:458`) **computes** the storm VA as the `far` with the most tail rows
(then earliest first appearance, then highest ordinal) and returns it as `storm["va"]`; the 1015b
reader reads **only** `storm["va"]` and holds **no** storm-VA literal for a decision. This is
load-bearing and not a nicety: **the storm VA MOVES every run** — 1011 `0x61000` → 1012 `0x2e000` →
1014 `0xe6000` → **1015 `0x27000`** (`records/1015-press-read.txt` §0). The hardcoded
`STORM_VA = 0x000E6000` at `tools/check_1015_capture.py:186` **turned a complete 1015 verdict into a
vacuous `STOPPED` + `V-AT0` + `P-CTL`** — the exact measurement defect this project keeps paying for.
**A 1016 reader MUST NOT reintroduce it; `STORM_VA_LAST_RESORT` is a label for the no-tail withhold
text only.**

### 0.2 The ATTRIBUTION GUARD — a stop is not a confirmation until it is attributed

`_wfkind = 1` (the full TLBIALL) is **not by itself** evidence of the widened arm: 1014's and 1015's
captures both publish `_wfkind=1` for FULL flushes of **non-current** pmaps at low ordinals
(`records/1014-press-read.txt` §4). `check_1015b_capture.py` therefore requires a `_wfkind=1` row
**keyed to the storm** (`_wfva == storm_va` or `_pcfar == storm_va`, `:456-476`). A `G-CONFIRMED`
**without** that attribution is a stop that is **measured but not attributed**, and MUST NOT be read
as a confirmation. **Both branches below inherit this guard.**

### 0.3 Read the run sanity WITH the cell (§7 of 1015b-design, and it is not optional)

The widened flush is on the storm's hottest path. A run that **stalled earlier** looks, in a tail,
exactly like a storm that **stopped**. So a `G-CONFIRMED` reading is read **together with `_pcn_max`
and the run sanity cell** (RESIDENT / PANICKED / UNKNOWN): **`_pcn_max` materially below the 1014
band (`_pcn` ran to `0x40000`, `records/1014-press-read.txt`) is a STALL, not a stop.** This is the
operator's judgement and it is stated here as part of the pre-registration, not discovered afterwards.

---

## 1. WHICH BRANCH IS MORE LIKELY — stated with its evidence, and the honest counter

**This design's lean: Branch A (G-CONFIRMED, storm stops) is the more likely read — but Branch B is a
live, pre-registered possibility with a named mechanism, and the design specifies both so the lean is
not a bet.**

**Evidence FOR A — a resident translation with the WRONG permission, which a full TLBIALL evicts.**

1. **`_wdatpar = 0x0000001f` is not merely "a fault" — its fault-status field reads a PERMISSION
   status, not a translation status.** RAW, MEASURED (1015 capture, `records/1015-press-read.txt`
   §2): `_wdatpar = 0x0000001f`. Decoded `0b0001_1111`: bit0 (F) = 1, and **bits[6:1] = `0b001111`
   = 15 = the level-2 PERMISSION status**. A **translation** fault (status 5/7) would put `0b000101`/
   `0b000111` in that field (PAR `0x0b`/`0x0f`). So the MMU's own AT says: *an entry for this VA
   exists, and it denies a USER WRITE.* That is a translation that is **present with the wrong
   permission** — exactly what a stale GLOBAL entry (installed when the page was read-only, before
   the COW moved it) looks like. `# UNMEASURED — confirm the PAR[6:1]/FSR status mapping at first
   build; the field reads 15 by the shift, and the design's conclusion rests on the field being
   permission-status and not translation-status, which the build's decode must assert.`
2. **The privileged read lands on a DIFFERENT physical page than the leaf** (`0x8419d000` vs
   `0x8417e000`, `records/1015-press-read.txt` §2). A resident translation pointing at a page the
   table no longer names is the textbook shape of an entry the tables have **moved past** — which a
   full invalidate reaches.
3. **The flush argument from the mechanism side.** `arm_context_switch` already runs a full TLBIALL
   on a pmap **CHANGE** (`pmap.c:2554-2566`, the `user_pmap == new_pmap` bail means it is skipped for
   the storm's own pmap), and `pmap_asid_configure` runs one **once at boot** (`pmap_asid.c:161`) —
   the steady-state flush during the storm is **TLBIASID only** (`pmap.c:1362`). Every user leaf this
   port installs is **GLOBAL** (`L2_NG_BIT 0x800`, `pmap.h:231`, is **never ORed** tree-wide; the
   storm's own leaf has bit11 = 0, `_leafng = 0`). So **the fix (b) is the first invalidate that can
   reach the entry the diagnosis names**, and it runs on the storm's own pmap's next update, before
   the retry.

**Evidence FOR B — and it is a structural objection, not a weakness.**

1. **perm-forensics ranks cause 0 FIRST, above H-G** (`docs/1015-perm-static.md`, RANKED CAUSES):
   *"NO TRANSLATION WAS EVER CREATED for `0xe6000` — the PMAP NEVER TOOK THE WRITE."* Its argument:
   the capture's **9 `far=0xe6000` faults are ALL `fsr=0x0000080f`** — the VA is **never once** seen
   as a translation fault (`0x805`/`0x807`). A stale-entry mechanism **requires a prior successful
   translation** of that VA (a protection change flips a cached translation); nothing in the capture
   shows one. If no entry was ever created, a full TLBIALL has **nothing to evict** and the storm
   persists — **G-REFUTED**.
2. **perm-static Item 4 could not verify the load-bearing premise by value:** *"I could not verify by
   value whether TLBIASID evicts global entries on this implementation."* The whole H-G chain rests
   on that sentence, and it was an assumption, not a measurement.
3. **The `_wdatpar` decode ALSO fits B if its status field is a translation status** (see A.1's
   UNMEASURED flag): if PAR[6:1] maps to FSR status 5/7 rather than 15, then AT never found the leaf
   either — *no translation* — and B is directly indicated. **This single decode is what flips the
   lean, and it is cheap to settle on the 1015 capture itself (a read, not a press).**

**Therefore: A is the lean, on the PAR permission-status reading + the differing PA + the flush
mechanism; B is the fallback, on the "no translation ever created" reading + the unverified
TLBIASID/global premise. The two branches below are written so that whichever fires, the arm is
ready. §5's measurement-defect self-check applies to BOTH.**

---

## 2. BRANCH A — G-CONFIRMED: the next bottleneck on the road to iOS 7 userspace

**In A, the storm stopped: the store on the COW/CS page no longer permission-faults ~2^20 times.**
The rung's question is now the one the storm has been *masking since 999*.

### 2.1 What the record says the frontier is, once the storm is gone

The storm is not a side-show — **it IS the collapse.** The chain from the record:

* **999** (`mi4-999-staged-blocked-on-vmabi`): real `launchd` reaches **user mode** (dyld
  `check_np`/`map_slide` ret = 0, the shared cache maps), then the **rate collapse** appears —
  the timer tick delivers far fewer interrupts than 1 kHz (98 + 122 = 220 deliveries over the run,
  ≈ 2.4/s), and the "tick froze at 64" reading was a ladder artefact (direct probes read 98/122).
* **1003** (`mi4-1003-shared-cache-cs-frontier`): the frontier is **a STALL, not a crash**.
  `cs_invalid_page` is **TOLERATED** (`retval=0`, the fault proceeds) — not fatal. After dyld's
  `shared_region_check_np` (ret = 0) and **exactly three `_mach_absolute_time`** calls (mach trap
  `-3`), pid 1 makes **ZERO further user syscalls** for ~23 min while the kernel runs (97 WDT feeds).
  pid 1 is **alive in the kernel but not making syscall progress**.
* **1005-1007-1009** pinned the stall's mechanism: a **whole-interrupt freeze** of a single **~24.4 s**
  interval (`xnu_live_tick_sum_maxd` = 468,237,313 / 19.2 MHz = 24.39 s), then a catch-up burst; the
  freeze is inside an `sleh_abort`, and (1009) the kernel's abort-return **does not clear the fault /
  re-executes** the same user store for 24.386 s. **That re-execution loop IS the storm** — a store
  that faults in the abort handler and is retried, starving syscall and interrupt delivery alike.

**So the single measurement Branch A's rung must read is: with the storm removed, does pid 1 make
forward USER-SYSCALL progress past the point it previously froze at?** Concretely, past 999/1003's
frozen set: `shared_region_check_np` ret 0 → the three `mach_absolute_time` (num = `-3`) calls at
`pc = 0x2be97328` → the first shared-cache code-page execute. If the storm was the collapse, killing
it should restore the tick cadence and let launchd cross.

### 2.2 THE SINGLE QUESTION (Branch A)

> **Does launchd's user-syscall frontier ADVANCE past the frozen set the moment the storm stops?**

### 2.3 The instrument — already in the image, so A is (mostly) a READ, not a new arm

The **user-SVC ladder** is already built (`src/entry/entry_stubs.c:7345-7355`): every user SVC
publishes `xnu_live_usvc_n` (ordinal), `_num` (the trap number), `_pid`, `_thr`, `_pc` (the user pc
of the `svc`), `_lr`, `_cpsr`, `_r0`. The forward-progress frontier is the **`(n, num, pc)` triple** —
and the record already names the frozen triple (`num = -3` = `_mach_absolute_time`, `pc = 0x2be97328`
de-slid to dyld's `bx lr`). The tick cadence is published by the tick family
(`xnu_live_tick_*`, 999 §5), and `xnu_live_exec_done_seq` (1015 capture: `0x1`) proves launchd's exec
already returned.

**So Branch A's rung 1016-A reads TWO by-value cells from the SAME recovered 1015b capture:**
**(A1) the top of the usvc ladder** (`max xnu_live_usvc_n` and its `_num`/`_pc`/`_pid`), and
**(A2) the tick cadence floor** (`xnu_live_tick_*` / the tick-sum growth). **No new arm is owed if
those keys are present and uncapped** — which is checked at read time, not assumed.

### 2.4 PRE-REGISTERED CELLS (Branch A)

Read on the 1015b capture, **only if §0.1's G-CONFIRMED cell fired with §0.2's attribution guard
satisfied**:

| # | condition | verdict | what it means |
|---|---|---|---|
| **A-PROGRESS** | the top usvc row's `(_num, _pc)` is **NOT** in 999/1003's frozen set — i.e. `_pc != 0x2be97328` **or** `_num` past the timebase trio (a NEW syscall number appears), **and** `xnu_live_usvc_n` exceeds the frozen ordinal | **STORM WAS THE COLLAPSE — LAUNCHD CROSSED THE STALL** | the fix removed the rate collapse: the next rung is the **next user-syscall wall** (read the new frontier's `_num`/`_pc` and name the wall) |
| **A-SAME-WALL** | the top usvc row is the **same** `(_num = -3, _pc = 0x2be97328)` triple with **no** later ordinal | **THE STALL SURVIVED THE FIX** | the storm stopped but the userspace stall did not ⇒ the stall is a **different** defect from the storm; go to 1006's freeze-detecting witness (the freeze's mask/GIC state) |
| **A-NEW-WALL** | usvc advances but **stops** at a *different* named `(_num, _pc)` | **A NEW, NAMED WALL** | the rung succeeded; the frontier **moved**; the next arm keys on the new triple |
| **A-NO-LADDER** | the usvc family is **absent** (`xnu_live_usvc_n` never published) | **WITHHOLD** (`CHECK_KEYS_AT_BUILD`) | the channel was capped or the recorder did not reach the tail; re-arm with the usvc ladder made **direct/unthinned** (the 1000 lesson) before concluding |

### 2.5 The NULL CONTROL (Branch A) — an instrument that cannot read wrong

The usvc ladder's absence and the tick's absence must **never** be read as "no progress" or "no
tick": both are **WITHHELD**. And the ladder's own **ordinal vs the record cap** is the null control
that the reading is not an artefact of truncation: 998/999 established the channel caps at a fixed
record count and **thins after 256** (`records/999-press-read.txt` §5) — **so a `max usvc_n`
materially below the cap while the run is long means the ladder is intact; a `max usvc_n` AT the cap
means the frontier is CUT, and A-NO-LADDER/withhold applies.** This mirrors §0.3's `_pcn_max`
discipline: a value pinning the cap is a measurement defect, not a wall.

### 2.6 The measurement that decides the NEXT step (stated plainly, per the brief)

**The user-SVC ladder's top `(n, num, pc)` triple — against 999/1003's frozen
`(num = -3, pc = 0x2be97328)` set.** If it advanced, the storm was the collapse and the next rung is
a **first-N direct usvc-frontier** read of the new `(_num, _pc)`. If it did not, the stall is a
**separate** defect and the next rung is 1006's **freeze-detecting witness** (the idle-path state
machine), **not** another pmap read.

### 2.7 What Branch A is NOT

* Not a claim that iOS 7 userspace is *launched* — A-PROGRESS only says launchd crossed **one** wall.
* Not a re-litigation of 1003's `cs_invalid_page` tolerance (settled: tolerated).
* It does **not** assume the usvc ladder is uncapped — §2.4's A-NO-LADDER withholds, it does not pass.

---

## 3. BRANCH B — G-REFUTED: the OBSERVER rung (the abort's own description of the access)

**In B, the storm persists under a full TLBIALL.** H-G is dead **as the cause of the fault** even
though V-AT1 confirmed the diagnosis at the hardware — because (as `docs/1015-design.md` §5.6 states)
**AT cannot distinguish "a stale entry" from "a translation the CPU re-derives wrongly"**, and only
the behaviour read-out can. `docs/1015-design.md` §3b's ladder then runs and (if the permission
inputs are all correct) reaches **P-X1**:

> **P-X1 = INPUTS_ALL_CORRECT_STILL_FAULTS** — the leaf `ap=0b11/apx=0/nG=0`, domain 0, DACR = 1
> client, TTBCR.N = 2 routing, the leaf is a small page — **ALL correct** (1015 MEASURED every one:
> `_wdap=3`, `_wdapx=0`, `_leafng=0`, `_wddom=0`, `_wddacr=1`, `_wdttbcr=2`, `_wdsmall=1`,
> `records/1015-press-read.txt` §1) **AND V-AT2** (the hardware translates the write to the leaf's
> PA) — **the translation is right and the store still faults; the next rung is the OBSERVER, not the
> pmap.**

**"The observer" = the abort handler and the fault the kernel believes it took.** Every instrument so
far reads the **page tables** (the walker, the AT). None reads **the abort's own account of itself**:
what `arm_ctx->fsr`/`arm_ctx->far` (the pair XNU ACTUALLY used to decide) say, whether they match the
cp15 DFSR/DFAR the entry wrapper read, whether the frame the handler is handed is the frame the vector
built, and whether the saved `pc` is a **store** whose addressing yields that `far`. **A mismatch here
means the fault being adjudicated is not the store being re-executed** — which would explain a storm
that no page-table change can move.

### 3.1 THE SINGLE QUESTION (Branch B)

> **Does the abort's OWN description of the access (`arm_ctx->far`/`arm_ctx->fsr`, keyed to the frame,
> and the faulting instruction at the saved `pc`) MATCH the cp15 DFSR/DFAR the wrapper read — i.e. is
> the fault being re-executed the fault being described?**

### 3.2 The instrument — a NEW entry-side recorder, keyed on the SAME fault

One new switch, **`STAGE90_XNU_OBSERVER_WITNESS`** (default 0), **entry-toolchain only** — no
kernel-pool edit, no `--wrap`, no source change to `external/`. It rides the SAME gate the 1015
recorder rides and publishes on the SAME tail rows, beside `entry_note_abt_wd(far_)`
(`src/entry/entry_trace.c:5812`). **All reads are inside the abort context, BEFORE
`__real_sleh_abort`** (`entry_trace.c:5831`), so every value describes the fault as TAKEN.

**The three comparisons, and the reads that produce them — every one is a value already in a register
or a slot, not a new walk:**

| comparison | what it proves | read at (file:line) |
|---|---|---|
| **cp15 pair == frame pair** | the `fsr`/`far` the wrapper read off cp15 (`c5,c0,0`/`c6,c0,0`) equal the `SS_STATUS`/`SS_VADDR` slots the **frame** carries — i.e. the vector stored the same fault the wrapper read | cp15: `entry_trace.c:5761-5762`; frame slots: `entry_saved_state.h:84-85` (`STAGE90_SS_STATUS` = 68, `STAGE90_SS_VADDR` = 72) |
| **`context` == `regs`** | `sleh_abort`'s `context` (cast to `abort_information_context_t *`, `trap.c:526-527`) points at the SAME frame the wrapper was handed — the `arm_ctx->fsr`/`->far` XNU reads (`trap.c:537-538`) are the frame's own slots (`abort_information_context_t` `fsr` @68 / `far` @72, `ios7leo_data_abort037.h:10-11`) | `__wrap_sleh_abort`'s `regs` (`entry_trace.c:5742`) vs the `context` its callee used |
| **saved `pc` is a STORE whose addressing yields `far`** | the instruction the abort re-executes is a store (`str`/`strb`/`strh`/`stm`), and (best-effort) a register that the frame shows equals `far` — the fault is the store, not a mismatched key | saved `pc`: `entry_saved_state.h:82` (`STAGE90_SS_PC` = 60), already read at `entry_trace.c:5861` |

**The PAR status decode is the SAME-instant corroboration and it lives on the 1015 key, not a new
one.** `_wdatpar`'s **bits[6:1]** are the AT fault status; **read and published as a DECODED field**
(`xnu_live_abt_wdparstat = (par >> 1) & 0x3f`), so a reader can see **permission-status (15)** vs
**translation-status (5/7)** without hand-shifting. In B this is expected to read **5/7** (no
translation) — and that reading **alone** is Branch B's strongest corroboration (`docs/1015-perm-static.md`
cause 0). `# UNMEASURED — the exact PAR[6:1]↔FSR status mapping is asserted at first build; the field
reads 15 = 0x0f on the 1015 capture's `0x1f`, which the build must confirm.`

**New keys (all grep to 0 tree-wide before this rung — `# UNMEASURED — verify at first build`):**

| key | width | value |
|---|---|---|
| `xnu_live_abt_wdfsrc` | 32 | the cp15 data-side `fsr` the wrapper read (`c5,c0,0`, `entry_trace.c:5761`) |
| `xnu_live_abt_wdfarc` | 32 | the cp15 data-side `far` the wrapper read (`c6,c0,0`, `entry_trace.c:5762`) |
| `xnu_live_abt_wdfsrfr` | 32 | the **frame** slot `SS_STATUS` (`entry_saved_state.h:84`) — what `arm_ctx->fsr` IS |
| `xnu_live_abt_wdfarfr` | 32 | the **frame** slot `SS_VADDR` (`entry_saved_state.h:85`) — what `arm_ctx->far` IS |
| `xnu_live_abt_wdctxeq` | 32 | **1 iff `context == regs`** (the handler's `arm_ctx` IS the frame) |
| `xnu_live_abt_wdcp15eq` | 32 | **1 iff `wdfsrc == wdfsrfr && wdfarc == wdfarfr`** (cp15 pair == frame pair) |
| `xnu_live_abt_wdparstat` | 32 | `(_wdatpar >> 1) & 0x3f` — the AT fault status, DECODED |
| `xnu_live_abt_wdins` | 32 | the 32-bit word at the **kernel-pc-relative** instruction (a best-effort instruction read; the aborting pc may be user VA — publish RAW, with the pc it read at) |

### 3.3 PRE-REGISTERED CELLS (Branch B)

Read on the same tail rows, **keyed to the storm VA by value** (the same `storm["va"]`, §0.1):

| # | condition | verdict | what it means |
|---|---|---|---|
| **O-CTL** | `xnu_live_abt_wdctxeq` = 0 **or** the cells are unpublished | **OBSERVER_INSTRUMENT_UNSOUND** — withhold O-1..O-4 | the handler's `arm_ctx` is not the frame, or the recorder did not run; no observer verdict |
| **O-1** | `wdcp15eq` = 1 **AND** `wdctxeq` = 1 **AND** the saved‑pc instruction is a store | **THE DESCRIPTION MATCHES THE STORE** | the abort describes the fault it re-executes ⇒ the observer is **sound**, and the residue is genuinely "inputs correct, store faults" — go to **H-X** (SCTLR/PRRR, `_wdsctlr`) for real, not another table read |
| **O-2** | `wdcp15eq` = 0 (cp15 pair ≠ frame pair) | **THE OBSERVER IS DESCRIBING A DIFFERENT FAULT** | the `fsr`/`far` XNU used is not the one the wrapper read ⇒ **the keying between the vector and the handler is broken** (`g_abtpte_curva`, `entry_note_abt_cur` at `entry_trace.c:5784`) — that is the defect, and it explains a storm no pmap change can move |
| **O-3** | `wdctxeq` = 0 (`context != regs`) | **`sleh_abort`'s `arm_ctx` IS NOT THE FRAME** | `trap.c:526-527`'s cast reads a different object's slots 68/72 as `fsr`/`far` ⇒ the fault classification itself is from the wrong memory — a structural observer defect |
| **O-4** | `wdparstat` = 5 or 7 | **AT REPORTED A TRANSLATION FAULT, NOT A PERMISSION FAULT** | corroborates perm-static cause 0 (no translation ever created): the leaf the walk reads is **not installed for the CPU**; go to the **tainted-VA test** (perm-static's discriminating experiment: pre-set the leaf before first touch), NOT another flush |

### 3.4 The NULL CONTROL (Branch B) — an instrument that proves it can read

**Every observer cell must be published on a KNOWN-GOOD abort in the SAME capture, not only on the
storm's row.** The W-SYNC control VA (`entry_stubs.c:3153`, `_wcva`, PA `0x80080000`) is already the
tree's known-good abort/read anchor (1014: `_wcleaf=0x8008021a`, `_wck=1`); the 1015 ladder's `_wdctlok`
/ `_wdctlat` are its hardware analogue. **So the observer rung requires a POSITIVE control: at least
one non-storm abort row where `wdcp15eq = 1 && wdctxeq = 1`.** If **no** row shows the cp15 pair equal
to the frame pair (including the control), then **cp15 ≠ frame is the NORM of this port, not a defect**
— and O-2 must be **WITHHELD**, not fired. **A gate that can only fail is not a gate**
([[mi4-measurement-defects]]): O-CTL is the clause that makes O-1 reachable, and the build must
demonstrate the control row PASSES before any O-2 is read.

### 3.5 What Branch B is NOT

* Not another **table** read. §4 of `docs/1015-design.md` and perm-static both say the tables are
  exhaustively cleared by measurement; B reads the **abort's account of itself**.
* Not a re-run of the flush experiment. The widened flush already ran (1015b) and the storm persisted
  — B does **not** re-press 1015b.
* **H-G is not re-opened.** B accepts V-AT1 as measured and moves the residual to the observer.

---

## 4. THE MEASUREMENT-DEFECT SELF-CHECK ([[mi4-measurement-defects]]) — applied to BOTH branches

**The recurring failure this document must not repeat: a criterion that CANNOT PASS** — the
`ASID == 0` + `full >= 2` clause that recurred in three 1015b scripts
(`records/1015b-press-read.txt`; `mi4-1015b-globalfix-rung`), and the `STORM_VA = 0xe6000` literal
that "passed" vacuously. Each is a number that is an artefact of how it was taken. Checked clause by
clause:

| the trap | where it bit | how 1016 avoids it |
|---|---|---|
| **a hardcoded storm VA** | `check_1015_capture.py:186` | both branches key on `classify_storm()["va"]` **by value**; §0.1 states it as a rule; no 1016 reader may hold a storm-VA literal for a decision |
| **an unsatisfiable clause** (`_wdttbr0` masked `2^(14-n) != 0x81a86000` — but `_wdttbr0` publishes `0x1`) | 1015 §1, `check_1015_capture.py` P-T3/P-M1 | Branch B's O-CTL is the reachable pass; O-1 is demonstrable on the control row; **no clause requires a value the recorder provably does not emit** (the `_wdttbr0` key is NOT used by either branch) |
| **a "stopped" that is a truncated log** | 1015b `G-WITHHOLD` | §0.1 keeps the terminal-marker + `_pcn` ladder completeness guard; A-NO-LADDER and O-CTL withhold rather than pass |
| **a stop that is a stall** | 1015b §7 | §0.3 reads `_pcn_max` **with** the cell: below the 1014 band = stall, not stop |
| **a stop that is not attributed** | 1015b attribution guard | §0.2 requires a `_wfkind=1` row keyed to the storm before G-CONFIRMED counts |
| **a reading with no control** | 1013/1015 `WITNESS_METHOD_BROKEN` | §2.5 (usvc cap), §3.4 (the cp15==frame control), and O-CTL |

**And the self-check's own limit, stated honestly:** Branch A's A-PROGRESS is **reachable** (a usvc
row with a new `(_num, _pc)` is a value the ladder already emits), and Branch B's O-1 is **reachable**
(a control abort row proves the cells can read equal). **Neither branch pre-registers a clause whose
success is impossible; if at first build either control cannot be produced, the rung is
`BUILD_REFUSED` before the press — the fix for an unsatisfiable clause is the clause, not the run.**

---

## 5. WHAT IS UNMEASURED UNTIL THE FIRST BUILD (no invented numbers)

* `# UNMEASURED — measure at first build` **the PAR[6:1]↔FSR fault-status mapping** asserted by the
  Branch A §1.1 / Branch B O-4 decode. The 1015 capture's `_wdatpar = 0x1f` reads field 15 by the
  shift; whether 15 is permission-status (A) or something else on this core is asserted at build.
* `# UNMEASURED — measure at first build` the 8 new observer keys of §3.2 — all grep to 0 tree-wide
  before this rung; the recorder, the `ENTRY_CFG_KEYS` site and the checker must agree on every name
  (the seven-way name contract, `docs/1015-design.md` §8d).
* `# UNMEASURED — measure at first build` whether the recovered 1015b capture carries the usvc
  family **uncapped** (Branch A's whole read rests on it) and whether the tick family is present in
  the tail — checked at read time (A-NO-LADDER / withhold), never assumed.
* `# UNMEASURED — measure at first build` the two `bl`/call-site addresses of the observer recorder
  in the linked elf (they move every rung; only the **object census** is asserted, the 1015b §4 rule).
* **The branch itself is UNREAD.** Everything above is conditional on a G cell that cannot be read
  until `scripts/recover_last_kmsg.sh --read-only` returns the run's log after the operator's power
  press. **No branch is taken until `tools/check_1015b_capture.py <capture>` prints it.**

---

## 6. THE DECISION, POSTED IN ONE LINE EACH

* **G-CONFIRMED** → **1016 = Branch A**, the **post-storm userspace-progress frontier**: read the
  **top usvc `(n, num, pc)`** against 999/1003's frozen `(num = -3, pc = 0x2be97328)`.
  *Progress ⇒ the storm was the rate collapse; a NEW `(_num, _pc)` names the next wall. Same ⇒ the
  stall is a separate defect; go to 1006's freeze witness.*
* **G-REFUTED** → **1016 = Branch B**, the **observer rung**: prove the abort's own description
  (`arm_ctx` frame pair, `context == regs`, the saved-pc store, the decoded PAR status) **matches or
  does not match** the faulting store. *Mismatch ⇒ the observer is the defect; match ⇒ H-X for real.*

**Both branches are pre-registered above; neither needs a re-think when the cell lands.**

---

## 7. FILE:LINE ANCHORS THIS DESIGN IS GROUNDED IN

**1015b fix (MEASURED on disk this session):** `external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c:1343-1362`
— the `if (flush_self)` arm, the inserted `else if (pmap != kernel_pmap) arm_tlb_flushID();` at
`:1357-1360`, marker `1015b: GLOBALFIX` at `:1348`, the ASID arm at `:1362`.

**The adjudicator (read by value):** `tools/check_1015b_capture.py` — `classify_storm` import
(`:312`), `STORM_MIN_ROWS = 3` (`:222`), `STORM_MIN_ORD = 0x4000` (`:223`),
`STORM_VA_LAST_RESORT` = label only (`:233`), the attribution guard (`:456-476`), the withhold
ladder (`:505-560`). `tools/check_1015_capture.py:184-186` (`STORM_MIN_ROWS`/`STORM_MIN_ORD`/the
**hardcoded `STORM_VA`** — the defect), `:458` `classify_storm`.

**The observer path (Branch B):** `src/entry/entry_trace.c:5742` `__wrap_sleh_abort`; `:5761-5762` the
cp15 data-side `fsr`/`far` read; `:5784` `entry_note_abt_cur(far_)`; `:5812` `entry_note_abt_wd(far_)`;
`:5829` `entry_note_sleh`; `:5831` `__real_sleh_abort`; `:5861` the saved `pc`/`cpsr`.
`src/entry/entry_stubs.c:2910` `g_abtpte_curva`, `:2945-2947` `entry_note_abt_cur`, `:3495-3513`
`entry_note_abt_wd` keying. `src/entry/entry_saved_state.h:82-85` (`SS_PC` 60 / `SS_CPSR` 64 /
`SS_STATUS` 68 / `SS_VADDR` 72). `external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:510` `sleh_abort`,
`:526-527` the `arm_ctx` cast, `:537-538` `arm_ctx->fsr`/`->far`.
`external/xnu-hd2-darwin13/xnu/osfmk/arm/ios7leo_data_abort037.h:9-11` — the static asserts fixing
`abort_information_context_t` `fsr` @68 / `far` @72 / size 76.

**The AT/PAR instrument:** `external/xnu-hd2-darwin13/xnu/osfmk/arm/proc_reg.h:544` `va2pa_pr_ns`,
`:546` `va2pa_ur_ns`, `:547` `va2pa_uw_ns`; `:522` `armreg_par_read`.

**Branch A's ladder (already in the image):** `src/entry/entry_stubs.c:7345-7355` the
`xnu_live_usvc_*` keys (`_n`/`_num`/`_pid`/`_thr`/`_pc`/`_lr`/`_cpsr`/`_r0`); `:4556-4560` the
`xnu_live_exec_done_*` family.

**The problem statement, pre-registered elsewhere (not redefined here):**
`docs/1015-design.md` §2b (the deferred fix), §3a (V-AT0..4), §3b (P-X1), §3c/§3d (the stitch);
`docs/1015b-design.md` §5 (the cell table, the attribution guard, the withhold); `docs/1015-perm-static.md`
(RANKED CAUSES 0-3, the "no translation was ever created" objection);
`records/1015-press-read.txt` (V-AT1), `records/1015b-press-read.txt` (the run RAN, the cell UNREAD),
`records/1014-press-read.txt` (the storm shape, `_pcn` to 0x40000), `records/999-press-read.txt` (the
rate collapse, the channel cap/thinning), `records/1002-press-read.txt` + `records/1003-press-read.txt`
(the stall, `cs_invalid_page` tolerated).

**No build, no press, no device action, and no `git add` was performed while authoring this file.**