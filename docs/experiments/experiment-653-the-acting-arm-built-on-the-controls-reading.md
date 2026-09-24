# 653: the acting arm, built on the control's reading — candidate (A), one switch pair from the arm that ran

**This record is written before the build it names.** Its purpose is that the acting arm's arms-length
question and its readings are on record *before* the image exists, the way 574 §6 pre-registered the
measure arm's two rows. Everything below is either a measurement already in hand, a quotation from a
file, or a prediction labelled as one.

The owed press (652) returned the reading 638 §3 was written for: the seam's pair came back **`b1 == a1`**
with `SCTLR.C` clear, which is that table's second row — the flush did **not** write DRAM ⇒ the stale line
never left the cache ⇒ a `C`-clear restore writes DRAM and **cannot reach a cache line** ⇒ **candidate (B)
is provably useless**, and the repair must be one that acts on the cache itself. That running image was
the **measure** arm: `STAGE90_XNU_SEAM_MEASURE=1`, `STAGE90_XNU_SEAM_POC=0` — the interception with the
operation removed. **This record names the arm that puts the operation back.**

**And 574 §6 pre-registered this row.** Its second row reads:

> the device returns **and the pop still dies** | the same, plus the payoff: `xnu_live_seam_*` is in the log
> at last, with `op=0`, so the pair says whether Apple's own L1 flush writes the slot's line back — the
> control reading that makes 535's `_a`/`_a1` interpretable

That is a description of the run that just happened, read off the log: returned (exit 0), died at the
`pop`, seam keys published, `xnu_live_seam_op=0x00000000`, pair equal. **Its first row — the same run with
the operation in the body — has never had a log at all**: 535 ran this code once, under an older
instrument set and on the regime before 594's switch, and left no log (572 §1), which is why 574 §6 called
the pair uninterpretable without a control. The control now exists. **This is therefore the first run of
candidate (A) whose readings can be read whatever it does**, and that, not the switch itself, is what the
build buys.

## 1. What candidate (A) is, as code that already exists

Nothing in `xnu_arm_boot/` is edited for this arm. The operation is already in the tree, behind the switch
`STAGE90_XNU_SEAM_POC`, and its body is the two steps the measure arm has and this one adds:

```c
/* stages/stage90/xnu_arm_boot/entry_trace.c */
2276  __real_FlushPoU_Dcache();                       /* both arms: Apple's own L1 flush, wrapped   */
2282  #if STAGE90_XNU_SEAM_POC
2283      FlushPoC_DcacheRegion(slot, 8u);             /* the acting arm: MVA clean+invalidate, PoC   */
2284      __asm__ volatile ("dsb sy" ::: "memory");
2285  #endif
2287  a0 = *(volatile uint32_t *)(uintptr_t)slot;      /* both arms: the post-flush DRAM read         */
2288  a1 = *(volatile uint32_t *)(uintptr_t)(slot + 4u);
2290  #if STAGE90_XNU_SEAM_POC
2291      *(volatile uint32_t *)(uintptr_t)slot = b0;  /* the acting arm: restore the push's own words */
2292      *(volatile uint32_t *)(uintptr_t)(slot + 4u) = b1;
2293      __asm__ volatile ("dsb sy" ::: "memory");
2294  #endif
```

The three properties that make this the repair the pair selected, each already recorded and each checkable
in the file that holds it:

* **It acts on the cache, not on DRAM.** `FlushPoC_DcacheRegion` is `Clean and Invalidate d-cache region to
  Point of Coherency` (`caches_asm.s:290-310`, body `:298-310`: `and r2, r0, #((1<<MMU_CLINE)-1)` /
  `bic r0, r0, #((1<<MMU_CLINE)-1)` / `lsr r1, r1, #MMU_CLINE` over `mcr p15, 0, r0, c7, c14, 1`, and
  `MMU_CLINE` is **6** in every `proc_reg.h` this build sees, so the mask is `#63` and the shift `#6`). So it
  walks **whole 64-byte lines** and its one cache instruction is `DCCIMVAC`, clean-and-invalidate **by MVA**.
  **An MVA operation is defined to the Point of Coherency, so it reaches whichever level holds the line**
  without needing XNU's operand-encoded level or a geometry from `proc_reg.h` — which is exactly the pair of
  questions 534 left open and 638's equal pair has now measured the other way: Apple's own
  `FlushPoU_Dcache`, the routine this seam intercepts, **did not reach the line** (an equal pair with `C`
  clear says no write-back happened; 534's level arithmetic says why — `CSSELR` is left at the L2 by
  `do_cacheid`, so the L1-geometry loop sweeps 256 of the L2's lines and **none of the L1's**).
* **Its clean half has something to write**, because the line is **dirty**: the enter wrapper's `strd r4,
  [sp, #-12]!` at `0x8047c8d4` writes a cacheable word into that line inside this window, and nothing in the
  window cleans or invalidates the L1 (597 §4, re-derived from the image; the same `strd` idiom and the same
  address are measured in 519, 520, 590 and the gate's own bracket paragraph, and re-measured today in
  `out/stage90/xnu_arm_entry.elf`). The seam's own comment said the opposite, which 597 corrected.
  *Corrigendum to 638 §3, measured 2026-09-24:* that section spells the same site `0x8047c8b4`; the frozen
  ELF says `0x8047c8d4`, one digit off, and 638 §3's conclusion does not turn on the address.
* **Step 4 is not decoration and not the (B) candidate.** After the clean, DRAM holds the line's *old*
  contents — the pre-push spill — over the word the exit's `push` had written. The restore puts the push's
  two words back **in DRAM**. What 638 §3's second row rules out is the restore **as the repair**: a
  `C`-clear store cannot reach a cache line, so on its own it fixes nothing about the line that answers the
  `pop`. It is required here only as the *consequence* of the operation, and the pair will say so.

## 2. The switch difference, and it is exactly two values

Read out of the record of the arm that just ran
(`/mnt/data/mi4-ios6-export/arm-574-idle-no-sleep-0/xnu_arm_entry-config.txt`), against the switches this
build will set:

| key | the arm that ran (574 park) | **this arm** |
| --- | --- | --- |
| `STAGE90_XNU_SEAM_POC` | `0` | **`1`** |
| `STAGE90_XNU_SEAM_MEASURE` | `1` | **`0`** |
| `STAGE90_XNU_IDLE_NO_SLEEP` | `0` | `0` (unchanged — the window stays enterable) |
| `STAGE90_XNU_SLOT_NULL` | `1` | `1` (unchanged) |
| `STAGE90_XNU_EXIT_POC_FLUSH` | `0` | `0` |
| `STAGE90_XNU_IDLE_CACHE_ENABLE` | `0` | `0` |
| `STAGE90_XNU_ISTACK_SEPARATE` | `0` | `0` |
| `STAGE90_XNU_IDLE_STACK` | `1` | `1` |
| `STAGE90_ENTRY_TRACE` | `1` | `1` |
| `STAGE90_ENTRY_REAL_ARM_INIT` | `1` | `1` |

**The two changed keys are one arm of one seam, not two changes.** `build_entry.sh:441-447` refuses the
image where both are 1 — *"`STAGE90_XNU_SEAM_POC=1` and `STAGE90_XNU_SEAM_MEASURE=1` are two arms of one
seam and the image would carry both bodies' compile-time choices: enable exactly one, so the record says
which arm the run was (572 section 6)"* — and `entry_trace.c:437-438` carries the same refusal as a
`#error`, so both the build script and the translation unit stop it. "The operation is present" and "the
operation is absent" are therefore the two values of one switch pair, and the control cell is the arm that
just ran. Everything else is byte-for-byte the same configuration, which is what makes the pair's change
attributable to the operation rather than to a second difference: **the only thing that changes between
the two runs is steps 3 and 4 of one function.**

**`SLOT_NULL` stays 1 deliberately, and it costs one reading.** With it at 1 the four `slot_*` words are
not published, so `xnu_live_slot_rtcpre_pop` is absent — which is why 642's three-way join could only be
read on one arm of three in 652, and why the reader's `op=1` cell 2 (`a1 == rtcpre_pop`, below) cannot
fire on this arm either. Turning it to 0 would add that reading *and* a second behavioural difference
inside the window (extra publication calls in the same pass), so it is left alone: an arm that changes the
operation and the instrument at once cannot attribute a changed pair to either. **The value of `a1` is
published regardless** (`xnu_live_seam_a1`), which is the number the join needs; it is named by the reader
only in a cell that cannot fire here, and that reader gap is recorded rather than patched under a press.

## 3. Pre-registered readings, and the falsifiers

The reader already carries this arm's cells — `run_and_capture.sh:2069-2088`, the `seam_op == 0x1` branch —
so the predictions below are the reader's own sentences, quoted:

| the pair in this run | the reader's cell | what it establishes |
| --- | --- | --- |
| `a1 ≠ b1` | **`CHANGED`** — *"the pair came back changed … and a1 is not this pass's rtcpre_pop … : a dirty line, holding something this block does not name"* | **the operation reached the line** and wrote it back to DRAM. With the control's equal pair beside it, the difference is the operation and nothing else. The **value** of `a1` is then the line's pre-pop contents, and the number to compare by hand is the death's `pc`/`r11` |
| `a1 == b1` | **`CLEAN LINE`** — *"the line was not dirty: the clean had nothing to write out, and a pop that still died on a stale value got it from somewhere this operation does not reach — the falsifier for 546 section 3's mechanism rather than its confirmation"* | **the falsifier**: neither the level-arithmetic account, nor the dirty-line account, nor the MVA escape survives for this death. If this cell fires, the mechanism is not the flush at all and m659's identity question (the slot read ≠ the slot consumed) becomes the whole story |
| `a1 == rtcpre_pop` | **`STALE LINE, WRITTEN OUT`** — 585's cell | the operation wrote the idle loop's own deadline where the frame's word belongs. **Unreachable on this arm** while `SLOT_NULL=1` (`rtcpre_pop` absent), recorded so that its absence is not read as its refutation |

**And the independent reading, which does not depend on the pair at all.** The frontier is the `pop`; the
deaths of 520, 533 and 652 all have `slot_pre_calls`/`slot_rtcpre_calls` published with `slot_post_calls`
**absent**, because the pass never came back through the wrapper. So:

* **the boot gets past the `pop`** ⇒ the exit wrapper's post site publishes (`xnu_live_slot_post_calls ≥ 1`),
  and the park's own witness becomes readable as a *boot* reading rather than a regression one — the third
  `poll` (`poll_seq = 3`, `poll_timeout_ms` ≥ the 1000 ms the park asks for) returns, on an arm whose
  window **was** entered. **That is the first outcome in this phase that would be progress rather than a
  re-reading**: the death named by every capture since 520 would, for the first time, not be the stopping
  point. `progress: N user-mode fault record(s)` and `sleh_storm` are the counters to watch beside it
  (both were 520's numbers in 652).
* **the boot dies at the `pop` again, with `a1 ≠ b1`** ⇒ the operation reached the line and the restore put
  the push's words back in DRAM, so the `pop`'s lookup should have missed to correct memory. A death there
  would mean the lookup did not miss — a **different** defect from the one this arm repairs, and it would
  move the frontier from "the flush does not reach the line" to "something re-populates the line between
  the seam and the `pop`" — which is exactly m659's mechanism 1 (`a str r4, [sp, …]` between the two
  moments), now with a value to test it by.
* **the device does not return** (the runner's exit 2) ⇒ the expensive cell, and the one 535 occupied
  without a log. It refutes nothing about the pair and costs a power press; the payload's armed watchdog
  caps the run at **28 s** (`STAGE90_HW_WATCHDOG_TIMEOUT_S` 25 + `_BITE_GAP_S` 3), so a non-return is a
  28-second windows, not a hung device. **This is 574 §6's third row and 597's window-loop candidate**
  (the clean writing the enter wrapper's `lr` over the exit wrapper's at `sp-4`, returning into the `wfi`
  path) — and it is now a *readable* cell rather than a silent one only if the capture still happens, which
  is the caveat below.

**What would make this arm uninformative rather than falsifying.** A non-return carries no capture: the
runner's exit 2 means the log was never read, so the pair is lost with it and "the operation ran and cost
the return" cannot be separated from "the operation ran and never reached the line". That cell has an
answer in the instrument that exists for it — the **live channel**, which publishes during the run and
whose fullness is itself published (609/610) — and it is not asserted here that the channel would have
drained in this window. It is named so that a non-return is not read as a verdict on the pair.

## 4. What this record is not

* **It is not a claim that the repair works.** The pair selected the *shape*; whether an MVA operation with
  `C` clear actually reaches a dirty L1 line on this part is the thing the run measures. The control
  measured one negative (`Apple's L1 flush does not write the line back`); this arm measures the other
  half.
* **It does not edit `xnu_arm_boot/`.** No source changes, so the freshness clause is not triggered by an
  edit — only by the rebuild itself, which is the point of the build.
* **It does not decide the *implementation* question 597 §4 raised** (whether what cost 535's return is the
  clean's write-back of the line's **other fourteen words**, the idle thread's live frame, rather than
  anything about the slot's two). That remains 572 §5's first candidate and is what the pair cannot see.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」 by itself**, and **TWRP-to-storage stays
  withheld** — 「如果os已经能进去了的话」 is unmet and this record does not meet it.
* **It does not arm anything.** No catcher is alive (652 §1: the relay stopped by its own spent-press rule
  at 17:24:31), so a press needs a **fresh arming** and the operator's presence; the build's own record
  below is host-side and reversible (the arm that just ran is parked byte-for-byte outside the tree).

## 5. Safety

Host-side only, for this record: one build once the pre-registration above is committed, one gate run, one
`verify_press_ready.sh`, and a `cp` of the built set outside the tree. **No device action in this step.**
`fastboot boot` only, **never `flash`**; nothing is written to storage, so no outcome of the run this arm
waits for can write to storage and a brick is impossible by construction. The run is capped at **28 s** by
the payload's armed watchdog, and the arm that just ran is recoverable — its park verifies
(`tools/verify_revert_set.sh … --set=frozen-574` → exit 0, 11 files) from
`/mnt/data/mi4-ios6-export/arm-574-idle-no-sleep-0/`, which is outside the tree a build rewrites.
