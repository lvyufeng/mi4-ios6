# 1017 — the shared-cache-execution rung: how to READ the spin, and the arm if it must be re-run

**This is a rung design, not a build.** No build, no press, no device action, no `git add` while
authoring it. It is written to be the harness for the two naming investigations launched alongside it
(`docs/1017-shared-cache-naming.md`, `docs/1017-timebase-poll-terminus.md`), which are the READ.

> **STATUS ON WRITE: 1016-A is READ.** `records/1016a-read.txt` (`dbe6793`) adjudicated the recovered
> 1015b capture as **A-NEW-WALL**: the storm is gone **by value**, and pid 1 is now caught **spinning
> in USER mode inside the dyld shared cache** at `pc = 0x3b04aa54`, `cpsr = 0x20000030`.
> **This design takes that as given and asks the one question it leaves open.**

---

## 0. The state 1016-A left, and the ONE thing this rung decides

1016-A named a **pc**, not a **place**. Its verdict is precise about the *evidence* and deliberately
silent about the *meaning*:

* **The evidence (MEASURED, `records/1016a-read.txt` §4).** The 1003 stall sampler
  (`xnu_live_stall_*`, `src/entry/entry_stubs.c:4634`) read **5 consecutive samples** of the
  interrupted pid-1 frame, all `pc = 0x3b04aa54`, `cpsr = 0x20000030` (`& 0x1f == 0x10` = **USR**,
  bit5 = **Thumb**), `sp = 0x2bd2b400`, `n` = tick ordinals 16384…19200 of a 19200-tick run. Per the
  sampler's own reading rule (`entry_stubs.c:4607-4623`): *"the sampled pcs are all the user pc AND
  IDENTICAL ⇒ pid 1 is wedged at one user instruction"* — a **user-space loop**.
* **The place (MEASURED, the capture's own geometry).** `0x3b04aa54` ∈
  `IOS7LAB cache mapping 0` (`va=0x2ee10000 size=0x0c266000`, capture :4105) — the **dyld shared
  cache**. The run's slide is `0x2e10000` (mapping 0's slid base `0x2ee10000` = the artifact's
  `0x2c000000` + `0x2e10000`), so the **un-slid VA is `0x3823aa54`** = **cache file offset
  `0x0c23aa54`**.
* **The gap.** `0x3b04aa54` is **not named**: no image, no symbol, no instruction. A "wall at an
  address" is not actionable; a wall at a **named function** is.

### 0.1 THE SINGLE QUESTION (1017)

> **What instruction is pid 1 executing at cache file offset `0x0c23aa54`, in which of the 696
> shared-cache images — and does that loop explain the 24.4 s freeze, or is it a bystander?**

The answer selects §2, §3 or §4 below. **No arm is specified until the naming lands** — that is the
whole point of this rung's ordering.

---

## 1. The two READ instruments (both launched, both read-only)

| instrument | owner | question | deliverable |
|---|---|---|---|
| **extract + disassemble the cache** | `docs/1017-shared-cache-naming.md` | which image/symbol/instruction is at file offset `0x0c23aa54` | image + symbol (or honest failure) + the disassembly window |
| **name the `-3` terminus** | `docs/1017-timebase-poll-terminus.md` | is the `mach_absolute_time` (`num=-3`) poll the thing pid 1 is stuck on, or a bystander | the ladder tabulation + the caller chain + the freeze cross-check |

**Why these two and not a third.** 1016-A left exactly two unresolved superlatives: *where* the spin
is (§1.1) and *whether the terminal syscall is the spin* (§1.2). A third instrument would be a walk of
the cache image table, which the first already does.

### 1.1 The address arithmetic, stated so it can be checked ([[mi4-measurement-defects]])

Every hop is an algebraic identity on two published numbers, not a memory:

```
capture :4105   IOS7LAB cache mapping 0  va=0x2ee10000  size=0x0c266000  offset=0x00000000
capture :4098   IOS7LAB shared region 0x2c000000..0x40000000
stall sample    pc = 0x3b04aa54
                                        slide = va - artifact_base = 0x2ee10000 - 0x2c000000 = 0x2e10000
un-slid VA      = 0x3b04aa54 - 0x2e10000            = 0x3823aa54
cache offset    = un-slid VA - artifact_base        = 0x3823aa54 - 0x2c000000 = 0x0c23aa54
in mapping 0?   = 0x2c000000 <= 0x3823aa54 < 0x2c000000 + 0x0c266000  => TRUE
```
`0x2c000000` and the mapping geometry are **the artifact's own values**, independently recorded in
`docs/reference/d13-dyld-shared-cache-study.md` §1 (header: magic `dyld_v1   armv7`, mappingOffset
`0x68`/count `3`, imagesOffset `0xc8`/count `0x2b8`, map[0] = `0x2c000000` size `0x0c266000` off `0`
prot 5/5). **So the slid form of a mapping the artifact fixes at `0x2c000000` == the capture's
`0x2ee10000` — the two agree, which is the null control that the offset is not off by a mapping.**

### 1.2 The `-3` terminus — why it is a SEPARATE question, not a forgone conclusion

1016-A measured that the ladder's terminal syscall is `num = -3` (`mach_absolute_time`) at the
link address `_mach_absolute_time+0x68` = link `0x2be1b328` (`records/1003-stall-forensics-preliminary.txt`
§6), **and separately** that the stall sampler's pc is in the **shared cache**. **Those are different
addresses in different images** (`0x2be…` = `/usr/lib/dyld`'s own image; `0x3b0…` = the shared cache).
So the capture carries **two candidate walls** and 1016-A did **not** decide between them:

* **W1 (the poll):** pid 1 is stuck **inside** `_mach_absolute_time`, spinning on the commpage/timebase
  — the 999/1003 wall, unmoved.
* **W2 (the cache code):** pid 1 is stuck in **shared-cache code** that *calls* `mach_absolute_time`
  in a loop — a new, smarter wall, reached only because 1015b stopped the storm.

**They are distinguishable by the sampler**: if the tick interrupt catches pid 1 at `0x3b04aa54` (a
cache VA) on 5 consecutive samples, the CPU is **in cache code**, not in `_mach_absolute_time`
(`0x2be…`) — which is **W2**, and the `-3` rows are the *history* that led there. **This is the
cross-check `docs/1017-timebase-poll-terminus.md` confirms or refutes by disassembling the caller at
`lr = 0x2bedec9f`.**

---

## 2. BRANCH N-ID — the spin is NAMED and it is a cache-code loop (expected)

If the naming returns an image + a loop, the rung's **next** arm is **NARROW and instrument-only**:

### 2.1 The arm — a first-N direct cache-execution witness (the 1000 lesson)

A user-pc sampler on a **powers-of-two ladder already exists** (the stall sampler). What it does
**not** carry is the **sequence**: which cache addresses pid 1 walked **into** the spin. The 1000
lesson (`records/999-press-read.txt` §5, givens (b)/(c)) is that a record whose **absence** is read as
evidence must be **cap-exempt and direct**, and a frontier wants the **first N**, not a doubling.

* **New key family** `xnu_live_uspc_*` on the **DIRECT** channel (`entry_write_kv`, cap-exempt), gated
  by a new switch **`STAGE90_XNU_USPC_WITNESS`** (default 0): the **first 64 distinct user pcs** seen
  in the pid-1 frame at the tick, each with its `cpsr` and ordinal. This makes the *path into*
  `0x3b04aa54` a reading, not a guess.
* **Entry-toolchain only**, riding the same tick gate as the stall sampler
  (`src/platform/darwin13/pe_msm8974.c:523-529`); **no kernel-pool edit, no `--wrap`, no `external/`
  source change** — the 1015b `XNU_KERNEL_EXTRA_DEFINES`-only rule is **not** needed here.

### 2.2 Pre-registered cells (Branch N-ID)

| # | condition | verdict | what it means |
|---|---|---|---|
| **C-ENTRY** | the first-N pcs reach `0x3b04aa54` and were **preceded** by other cache pcs | **THE SPIN HAS A PATH** | name the entry: the wall is the *call* that entered the loop, and 1017's next arm is a breakpoint/guard there |
| **C-ENTRY-0** | `0x3b04aa54` is the **first** user pc after exec and never changes | **THE SPIN IS ENTERED IMMEDIATELY** | the wall is at the exec→cache transition, not inside a loop — go to the exec entrypoint (`xnu_live_entrypt_entry`) |
| **C-CTL** | the family is absent / never fires | **INSTRUMENT_UNSOUND — withhold** | the tick gate did not run this arm, or the channel capped (the 1000 lesson: check the cap FIRST) |

### 2.3 The NULL CONTROL (Branch N-ID)

The first-N witness must publish **at least one pc in a NON-cache, user region** (`0x2bxxxxxx`, dyld's
own `__TEXT`) in the **same capture** — otherwise the witness cannot show it *distinguishes* cache
from non-cache, and every pc reading is suspect. **A gate that can only read cache pcs is not a
witness.** ([[mi4-measurement-defects]], the 1013 control's lesson.)

---

## 3. BRANCH N-POLL — the spin IS the `-3` timebase poll (refutes W2)

If the naming shows the loop at `0x0c23aa54` **is** a `mach_absolute_time` poll (a cache-image copy of
the same loop), then **1016-A's "the wall moved into the cache" is REFUTED** — the wall is the 999/1003
wall, still unmoved, and 1015b merely let pid 1 reach it from a different caller.

**The rung is then 1004's commpage/timebase question, resumed** (`docs/1004-deepening-commpage-mat-pl0vcten.md`):
the commpage alias published at `IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN`
(capture :4020) — **XN = execute-never**, so a `mach_absolute_time` that takes the **slow SVC path**
because the commpage validity byte is 0 would loop. The arm: publish the commpage's `mach_absolute_time`
validity structure **by value** and read whether the fast path is armed.

---

## 4. BRANCH N-OTHER — the spin is neither a poll nor a clean loop

Any other instruction (a self-referential store, a lock CAS stepping on itself, a compare that never
satisfies) is still a **named wall**: the rung records it and arms the 1017 witness of §2.1 with the
instruction as the pre-registered "what".

---

## 5. THE MEASUREMENT-DEFECT SELF-CHECK ([[mi4-measurement-defects]]) — applied to 1017

| the trap | how it would bite here | how 1017 avoids it |
|---|---|---|
| **a slid address read as un-slid** | `0x3b04aa54` is a **slid** VA; using it as a file offset names the WRONG instruction | §1.1 subtracts the run's own slide (`0x2e10000`, from the capture's mapping row) before indexing the artifact |
| **two addresses in two images conflated** | `0x2be…` (dyld) vs `0x3b0…` (cache) are different files | §1.2 keeps W1/W2 **separate** and gives the discriminator (the sampler's pc region) |
| **the ladder's end read as a stop** | `usvc_n` stops at `USVC_FULL_N = 64` (`entry_stubs.c:7313`) ⇒ powers of two past it | §0/§2.3 never treat the ladder's end as the frontier; the stall sampler is the instrument |
| **a memory of 1003 used as a check** | the `0x2be1b328` identification is a *prior run's* reading | §1.2 cites it as an identification and re-derives the this-run link address by slide (1016-A §2) |
| **a criterion that cannot pass** | a clause keyed on a symbol that may not exist (the cache has no ELF symtab) | §2.2's C-CTL withholds on absence; §0.1's question is answerable **without** a symbol (the disassembly IS the answer) |
| **an instrument with no control** | the first-N pcs might all be cache pcs by construction | §2.3 requires a non-cache user pc in the same capture |

**And the honest limit:** Branches §2/§3/§4 are **mutually exclusive and jointly exhaustive** over the
naming's outcome, so whichever lands, the arm is already specified. **None requires a value the
recorders provably do not emit** — §2.1's witness is a NEW recorder (the one new arm this rung
authorises), and its absence **withholds** (C-CTL) rather than passing.

---

## 6. WHAT IS UNMEASURED UNTIL THE NAMING LANDS (no invented numbers)

* `# UNMEASURED — measure at first build` **which of the 696 images** contains file offset
  `0x0c23aa54`, and the instruction there. This is the whole of the READ; **no branch is taken until
  `docs/1017-shared-cache-naming.md` reports it** (or reports honestly that the cache could not be
  extracted).
* `# UNMEASURED` whether the loop is a `mach_absolute_time` poll (§3) or something else (§4) —
  decided by `docs/1017-timebase-poll-terminus.md`'s disassembly of the caller at `lr 0x2bedec9f`
  (this-run) = link `0x2be86c9f`.
* `# UNMEASURED — measure at first build` the `xnu_live_uspc_*` key names, the `ENTRY_CFG_KEYS` site,
  and the checker — the seven-way name contract (`docs/1015-design.md` §8d) applies if §2 is taken.
* `# UNMEASURED` whether the 24.4 s freeze (`xnu_live_tick_sum_maxd = 0x1be9f52b`) is the spin or a
  separate kernel span (`xnu_live_abt_maxcall = 0x1571b974` = 18.7 s inside one `sleh_abort`). 1016-A
  §5.3 left this open; §3's cross-check reads it.

---

## 7. FILE:LINE AND RECORD ANCHORS

**1016-A's evidence:** `records/1016a-read.txt` (the read, `dbe6793`); the capture
`out/stage90/captures/recovered-unknown-arm-20261015-163856-last_kmsg.txt` (`IOS7LAB shared region`
:4098, `IOS7LAB cache mapping` :4105-4108, the stall block :33977-34010).

**The stall sampler:** `src/entry/entry_stubs.c:4607-4664` (`entry_note_stall_sample`, the reading
rule at :4607-4623, `USVC_FULL_N` note at :7313); `src/platform/darwin13/pe_msm8974.c:523-529` (the
powers-of-two tick gate).

**The cache artifact:** `/mnt/data/ios7-payload/v2/ios7/rootfs.hfs` (939524096 B, HFSX);
`/System/Library/Caches/com.apple.dyld/dyld_shared_cache_armv7` (315952666 B, 301 MiB);
`docs/reference/d13-dyld-shared-cache-study.md` §1 (header by value: map[0] `0x2c000000` /
`0x0c266000` / off 0 / prot 5/5, imagesOffset `0xc8`/count `0x2b8`), §3 (geometry);
`docs/experiment-ios7-abi-staging.md:225-227` (the file is the `armv7` cache, not `armv7s`).

**The prior wall:** `docs/1004-deepening-commpage-mat-pl0vcten.md` (the commpage timebase question),
`records/1003-stall-forensics-preliminary.txt` §6 (`0x2be97328` = link `0x2be1b328` = the `bx lr`
after `svc #0x80`), `records/999-press-read.txt` §5 (the rate collapse and the cap lesson).

**No build, no press, no device action, and no `git add` was performed while authoring this file.**