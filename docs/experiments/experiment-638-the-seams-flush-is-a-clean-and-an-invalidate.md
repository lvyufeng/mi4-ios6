# 638: the seam's flush is a clean AND an invalidate, and the arm's pair is read through the cache the `pop` reads through

The owed press sends the sleeper arm, and the sleeper arm carries the seam in its **measure** form
(`STAGE90_XNU_SEAM_MEASURE=1`, `STAGE90_XNU_SEAM_POC=0`): `entry_seam_flush` saves the two words of
`platform_cache_idle_exit`'s frame, calls Apple's own `FlushPoU_Dcache` through the seam, reads the two
words back, and publishes both pairs. 597 established that the death is a stale cache line answering
the `pop`'s lookup. This step reads the instruction stream and the source of that measure arm and
records **what its pair can and cannot decide** — so that the one press this phase gets is read as a
decision rather than as a yes/no.

> **640 corrects this step's applicability, and only that.** The arm that is armed is the one whose idle
> never sleeps, and that arm cannot reach `platform_cache_idle_exit` at all: the seam's acting site is a
> `bl` inside it, its only caller is `cpu_idle` behind the `SIGPdisabled` gate, and 599 section 5 shows
> that gate never opens on this arm. So the pair below is **unreadable on the owed run** — the owed log
> will carry no `xnu_live_seam_*` key — and what follows is a pre-registration for an arm that enters the
> window (`IDLE_NO_SLEEP=0` with `SEAM_MEASURE=1`), which no build has carried. The physics below stands;
> the run that can read it is not this one. See 640 sections 2-4.

**Three things were corrected or established, all from bytes that are in the tree.** Nothing was built
and nothing was run.

## 1. `FlushPoU_Dcache` is a clean **and an invalidate**, and the seam's comment reasons about half of it

The seam's own note inside `entry_seam_flush` says its clean half "has nothing to write back". That
sentence, and 597 §4's correction of it, both treat `FlushPoU_Dcache` as a **clean**. It is not:

```
external/xnu-4570.1.46/osfmk/arm/caches_asm.s:275
  LEXT(FlushPoU_Dcache)
      mcr  p15, 0, r0, c7, c14, 2      // cleanflush dcache line by way/set
```

Apple's own comment calls it **cleanflush**, and `c7, c14, 2` is the clean-and-invalidate form. The
frozen ELF carries the same instruction (`0x80045878`, `ee070f5e`), and the three neighbours are now
distinguishable by their encodings and by the comments beside them in that file:

| routine | encoding | Apple's comment | semantics |
| --- | --- | --- | --- |
| `CleanPoU_Dcache` | `c7, c10, 2` | "clean dcache line by way/set" | clean, lines stay **valid** |
| `FlushPoU_Dcache` (the seam's call, and the enter's) | `c7, c14, 2` | "**cleanflush** dcache line by way/set" | clean **and invalidate** |
| `FlushPoC_DcacheRegion` (the POC arm's step 3) | `c7, c14, 1` | "**Clean & invalidate** dcache line" | clean **and invalidate** |

So the seam's operation has two halves with opposite effects on the line: the clean writes the line's
present contents to DRAM, and the invalidate then drops it. **Whichever half is the agent, the arm's
pair is the reading that separates them** — §3.

## 2. `b1`/`a1` is the word the `pop` branches to, and both its readings go through DRAM

Read off `entry_seam_flush` and the instruction stream of the frozen arm:

| what | where | value |
| --- | --- | --- |
| `slot` — the seam's first argument | `__wrap_FlushPoU_Dcache` does `mov r0, sp` and tail-branches | the exit's `sp` **after** its `push {fp, lr}` |
| `b0` / `a0` | `ldr r9, [r0]` / `ldr r7, [r5]` | `[slot]` — the exit's saved **`fp`** |
| **`b1` / `a1`** | `ldr r8, [r0, #4]` / `ldr r6, [r5, #4]` | `[slot+4]` — the exit's saved **`lr`**, i.e. **the word `pop {fp, pc}` loads as `pc`** |
| the read-back's cache state | published as `xnu_live_seam_sctlr` (`mrc p15, 0, sl, cr1, cr0, {0}`) | must have **bit 2 = 0** |

`platform_cache_idle_enter` clears `SCTLR.C` at `0x80046240-44` and `platform_cache_idle_exit` sets it
again at `0x80046320-24`; the seam's `bl` is at `0x800462d8`, between them. So **all four of the arm's
loads are non-cacheable, and the `pop` at `0x8004633c` is the first cacheable load after the
re-enable — of the same address.** That is the arm's whole design (`entry_trace.c:2271-2276`: "with
`SCTLR.C` clear they are non-cacheable loads"), and the run's log will show it in
`xnu_live_seam_sctlr`.

**Which makes the pair and the `pc` two readings of one word taken through two different cache
states** — `[[mi4-one-value-two-definitions]]` in its purest form, and the reason an **equal pair is
not a clearance**: the arm can only see DRAM, and the fault 597 established is a line that answered
the lookup instead of DRAM. The arm's own comment already says this in as many words —

> an *equal* pair there is the arm working as designed and not "the line was clean"

— and it is worth being precise about *why* it is not: an equal pair has two causes the pair cannot
separate. Either the line was clean (so the clean half of `FlushPoU_Dcache` wrote nothing), or the
whole routine is inert with `SCTLR.C` clear (so neither half acted). Both leave the pair equal.

## 3. The pre-registered reading: which half is the agent, and which repair each selects

> **640: this section is a reading of an arm that enters the window, and the armed arm is not one.** Both
> rows below are statements about `_b1`/`_a1`, which only the seam's acting branch publishes — and that
> branch is reachable only from inside `platform_cache_idle_exit`, which the armed arm cannot reach
> (640 section 2). So neither row applies to the owed run, and a log with no pair must not be scored as
> the second row.

597 named two candidate repairs and could not choose between them, because no run has ever produced
one of these pairs alongside a return. **The pair is the choice**, and this is what each reading means:

| the pair in the owed run | what it establishes | what it selects |
| --- | --- | --- |
| **`a1 ≠ b1`** | the flush **wrote DRAM while `SCTLR.C` was clear**, so the routine is not inert with the cache off, and the line was dirty — the clean half acted | the seam's own `bl __wrap_FlushPoU_Dcache` inside `platform_cache_idle_exit` writes the **pre-push** line over the pushed `lr`. A restore of `slot`/`slot+4` after the operation repairs the exact word the `pop` reads ⇒ **597's candidate (B) works**, and 535's operation stays a suspect |
| **`a1 == b1`** | the flush **did not write DRAM** | the stale line never left the cache, so the `pop`'s lookup was answered by it; and a restore runs with `SCTLR.C` clear, so it writes DRAM and **cannot reach the cache line** ⇒ **candidate (B) is provably useless** and the repair must be one that acts with the cache **set**, or must not leave the line stale across the disable — the enter wrapper's `strd` at `0x8047c8b4` is the last cacheable writer of that word ⇒ **597's candidate (A)** |

**And the same pair is decisive whether or not the sleeper arm survives the `pop`** — the reads are
taken either way, so a run that gets past the `pop` still selects between the two repairs.

> **Measured, 2026-09-24 17:24 (652, the owed press, exit 0): the second row is the one that governs.**
> The 574 park returned `b1 == a1 == 0x8047c990` with `SCTLR.C` clear and `seam_op=0`, so the flush **did
> not** write DRAM: **candidate (B) is provably useless and the repair is candidate (A)** — decided by
> measurement rather than by choice, and the first run to carry both this pair and a log. The arm it
> selects is pre-registered in `experiment-653-…`, which is the same code with the operation put back
> (`STAGE90_XNU_SEAM_POC=1`, `STAGE90_XNU_SEAM_MEASURE=0` — one arm of one seam).
>
> **Corrigendum to the `a1 == b1` row (653, measured):** the address it ends with is **`0x8047c8d4`**, not
> `0x8047c8b4` — one digit. Measured in `out/stage90/xnu_arm_entry.elf`
> (`__wrap_platform_cache_idle_enter`'s `strd r4, [sp, #-12]!`), and spelled that way in 519, 520, 590,
> 597 and the gate's own bracket paragraph. The row's conclusion does not turn on the address.

## 4. A self-check the owed run gives for the first time

597 showed that the fatal `pc` in both archived runs is the **value of the enter wrapper's spilled
`r4`**, with bit 0 masked (`0x04b79075 → pc 0x04b79074`; `0x33f1c1b5 → pc 0x33f1c1b4`). No run has ever
had both the seam's pair and a log — 535 left no log (572 §1), and the sleeper arm is **UNRUN**. So the
first run that can close the loop **inside one log** is a seam arm that enters the window, and not this
press (640 section 4: on the armed arm the premise "if the run dies at the `pop`" does not hold):

> if the run dies at the `pop`, then `min(b1, a1)` masked to bit 0 should equal the value the log
> reports as the fatal `pc` — and the one of the two that matches says which side of the flush the
> `pop` read.

If neither matches, then the pair is not a reading of the fatal word at all, and that is a finding
about the arm rather than about the operation.

## 5. What this does not do

* **It does not predict which branch the run will take, and it does not claim to.** §3 is a
  pre-registration: two readings, defined before the run, each selecting a different repair. Choosing
  between them from the host would be exactly the "plausible story" 597 turned into a measurement.
* **It does not build, run, or edit anything.** No source file was changed; the arm, its park and the
  runner are untouched (`out/stage90/stage90-qcdt.img` is still `60063c47…`, still **UNRUN**), and
  `tools/verify_press_ready.sh` reads 3 ok / 1 FAIL with the FAIL being the neighbour. (That count was
  638's; since 646 the tool has a fifth row and reads 4 ok / 1 FAIL on the same host - see 646. The
  arm and the FAIL are unchanged.)
* **It does not settle whether a set/way maintenance operation is defined with `SCTLR.C` clear.** That
  is the physical question §3's first row decides, and it is a property of this part, not of the ARM
  ARM's permitted implementations — which is why it is left to the run rather than argued here.
* **It does not change 597's conclusion.** The mechanism (a line older than the push answering the
  lookup) stands; §1 corrects the *operation's* description, which the seam's comment and 597 §4 both
  inherited from a name (`Flush` vs `Clean`) rather than from the encoding.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** It makes the one press that can
  advance it decisive. The device is off the bus; the press is the user's: **unplug `33e80afe`, then
  Vol-Down + Power.**

## 6. Safety

No device action, no boot, no build, no `fastboot`, no `adb`, **nothing written to storage**. Every
measurement is a read of the frozen ELF (disassembly and `nm`) and of Apple's own cache source in
`external/`. No file was modified: `git status` is clean and the arm still hashes to `60063c47…`. Both
catchers were confirmed alive (pid 1344846 watcher, pid 4067419 relay). `fastboot boot` only - never
`flash` - so no outcome of this step can write to storage.

## 7. Corrigendum (2026-09-24): the arm this step pre-registers for is **built**

The banner above says the pair needs "an arm that enters the window (`IDLE_NO_SLEEP=0` with
`SEAM_MEASURE=1`), **which no build has carried**". **That clause is wrong, and only that clause.** A
build carried exactly that combination on 2026-09-23:

| | |
|---|---|
| entry bin | `151425c40c48cb1a417de1e4c570cb812f5b07a2e45aa41b04421c63fe34d746` (5519996 B) |
| entry ELF | `3bc726056dfb90eeee4fbcb5afcfec32b8b356a187576a8f63ecaaad8af188c6` |
| record | `STAGE90_XNU_SEAM_POC=0`, `STAGE90_XNU_SEAM_MEASURE=1`, `STAGE90_XNU_IDLE_NO_SLEEP=0` |
| payload | `stage90.bin` `0f108392…`, `stage90.img` `8274b1c4…`, `stage90-qcdt.img` `914f45ac…` |

Parked, with its own verified manifest, at `/mnt/data/mi4-ios6-export/arm-574-idle-no-sleep-0/`. Its
record differs from the armed record in **two lines**, one of them the entry hash: the other is
`IDLE_NO_SLEEP` `0` vs `1`.

**What "no build has carried" was reaching for is true of a run.** `experiment-594` §"the frozen 574
arm is untouched (`914f45ac…` / `151425c4…` / `3bc72605…`) and **unrun**". So the pre-registration
stands exactly as written in §3 — it is *unread*, not unreachable — and the arm that reads it needs a
**press**, not a build. That distinction is load-bearing rather than pedantic: a `build_entry.sh` run
with the catcher armed silently swaps the armed image (636), so a reader who takes this banner at face
value and builds the arm first spends the owed press on nothing.

Two measurements were taken for this corrigendum, both host-only and both recorded in full in the
park's `README.md` and in `experiment-641`: `tools/check_idle_window_unreachable.py` distinguishes the
two arms cleanly (armed → `the window is UNREACHABLE in this image`; this park → `reachable EXACTLY
ONCE`, at `0x8047be64` in `__wrap_poll`), and the two `stage90.bin`s are byte-identical outside the
entry region, which each one hashes to the entry its own record names.

Nothing else in this step changes. §1's correction to `FlushPoU_Dcache`, §2's two-readings-through-DRAM
argument and §3's table are about the physics and stand as measured.
