# 652: the park ran, the seam's pair is equal — and the words the pop consumed are not the words the seam read

The owed press fired at **17:24:03** on 2026-09-24 and returned. It sent the **574 park**
(`151425c4…`, `IDLE_NO_SLEEP=0`, `SEAM_MEASURE=1`) rather than the sleeper, by the operator's choice:
the sleeper cannot answer 638 §3 and the park is the only arm that enters the window. This is the first
run in this project to carry **both** a `xnu_live_seam_*` pair and a return — the cell 638 §3 was
written for and no build had produced.

**Numbering:** 651 is `run-experiment-526`'s doc in the export directory (the catcher's ambiguity
hold), and my landings commit `bc0d92a` labelled itself 651 in its subject line before this record
existed. So "651" names two different things in this project's records, and this doc is **652**.

Device-side, in one line: `fastboot boot` only, never `flash`, nothing written to storage — the phone
returned to Android by itself and is not bricked, hung, or altered.

## 1. The run, exit code first

| | |
| --- | --- |
| arm sent | the 574 park, `151425c4…` — gate read `IDLE_NO_SLEEP=0`, `SEAM_MEASURE=1`, `SEAM_POC=0` |
| gate | **exit 0** |
| runner | **exit 0** — returned **and** captured (`/tmp/cancro-last_kmsg.txt`, 598044 B, 8961 lines) |
| capture preserved | `out/stage90/captures/650-owed-run-park-574-2026-09-24-last_kmsg.txt`, sha256 **`708ff83e…`**, plus the catcher's log beside it |
| cover | the relay printed *"the press has been spent … the relay stops here and will never arm again"* at 17:24:31 and exited — **so nothing armed is left to be changed under, and the 620 hold on the gate and the runner is over** (that is what §7 lands) |

The run's own summary is in the catcher log at `press-watcher.log:1583-1800`. It was also re-run by
hand and the numbers below come from the hand run, not from the log's copy.

## 2. The witness: the falsifier stands, and the death is one level in from the frontier

```
xnu_live_poll_seq          1, 2          (two records; no third)
xnu_live_poll_timeout_ms   5, 40         (no ask at or above the park's 1000 ms threshold)
xnu_live_poll_over         absent
```

So `poll_seq` **stops at 2**, exactly as 520 and 533 did. 638 §3's premise — that the third `poll`'s
*timeout* was the thing being rounded to zero — is therefore **not confirmed** by this run, and the
falsifier pre-registered in Step 6 of the read sheet stands.

**But this arm entered the window, so the falsifier now has a cause the sleeper could never show.** The
window family is present and the seam ran:

```
xnu_live_sip_seq       1        (past cpu_idle's first test)
xnu_live_pce_seq       1        (the enter wrapper)
xnu_live_wfi_seq       1        (the WFI)
xnu_live_slot_cwe_*    3
xnu_live_seam_calls    1        <- the seam's own instrument fired
```

and the localization is inside the exit, from the three bracket notes sharing one schedule and one
gate: `slot_pre_calls=1` and `slot_rtcpre_calls=1` published, **`slot_post_calls` absent**. So the pass
reached the wrapper, took the rtcPop reading, got as far as the call, and **did not come back through
it**. The death is inside `platform_cache_idle_exit` — one level deeper than "the park's poll did not
return", which is all the archived pair could say.

## 3. The pair, and the measurement 638 §3 pre-registered

| key | value |
| --- | --- |
| `xnu_live_seam_calls` | `0x00000001` |
| `xnu_live_seam_lr` | `0x800462dc` — the exit's own `bl`'s return address (the hook was entered at the seam, not at one of the routine's three other callers) |
| `xnu_live_seam_sp` | `0x8054fec8` — the exit's frame slot |
| `xnu_live_seam_sctlr` | `0x30c57879` — `C` clear, so all four words were read out of DRAM |
| `xnu_live_seam_b0` / `b1` | `0x800b2648` / `0x8047c990` — the two words the exit's `push {fp, lr}` wrote |
| `xnu_live_seam_a0` / `a1` | `0x800b2648` / `0x8047c990` — the same two, after Apple's own L1 flush |
| `xnu_live_seam_op` | `0x00000000` — this is the **measure** form: the interception with no operation behind it |

**`a1 == b1`.** 638 §3 pre-registered exactly two readings for this pair and this is the second row of
its table:

| reading | what 638 §3 said follows |
| --- | --- |
| `a1 ≠ b1` | the flush wrote DRAM while `C` was clear ⇒ the clean half acted ⇒ **597's candidate (B) works** (restore the two words) |
| **`a1 == b1`** | the flush **did not** write DRAM ⇒ the stale line never left the cache; a restore runs with `C` clear, so it writes DRAM and **cannot reach a cache line** ⇒ **candidate (B) is provably useless**, and the repair must be one that acts with the cache **set**, or must not leave the line stale across the disable ⇒ **597's candidate (A)** |

**So the choice 597 could not make is now made by measurement: candidate (A), not (B).** The
`entry_seam_flush` restore stores cannot repair this death, because the value the `pop` reads is not
in DRAM for them to restore. That is the answer this press was owed, and it is the first time it has
been available: 535's run left no log, and every arm since has either not entered the window or not
carried the pair.

**And the arm is a clean control for it.** `seam_op=0` means nothing was done to the line behind the
interception, so the equal pair is the L1 flush alone and not the seam's own stores. This is the
measurement 561/572 built the measure form to make, and the run is the first to deliver it.

## 4. The death, and the join 642 asked for — which now exists and disagrees

`panic(cpu 0 caller 0x80454584): sleh_abort: prefetch abort in kernel mode: fault_addr=0x5006e74`

```
r0: 0x8051a000  r1: 0x00000001  r2: 0xde500000  r3: 0x0008bb74
r4: 0x05006e74  r5: 0x8051a000  r6: 0x8051a0e0  r7: 0x00000000
r8: 0x80553520  r9: 0xc0558df0 r10: 0x800ba588 r11: 0x05006e74
r12: 0xde58bb7e  sp: 0x8054fed0  lr: 0x001ffff3  pc: 0x05006e74
cpsr: 0x80000093  fsr: 0x00000005  far: 0x05006e74
```

Three things, each checkable:

* **`pc == r4 == r11 == far == 0x05006e74`**, and it is not a kernel address (the kernel's base is
  `0x80000000`). That is 597's own signature a **third** time: the fatal `pc` is the **spilled `r4`**
  (597 measured `0x04b79075 → 0x04b79074` and `0x33f1c1b5 → 0x33f1c1b4`, both with bit 0 masked; here
  bit 0 is already clear, so the equality is exact).
* **`sp = 0x8054fed0` is `seam_sp + 8`** — the abort is taken with the frame pointer eight bytes above
  the seam's slot, which is what a `pop {fp, pc}` leaves behind. So the fault is *at the pop*, at the
  slot the seam read.
* **`lr = 0x001ffff3`**, which is the value the reader's clause (1) picked up as `sleh_lr` and reported
  as *"not the exit pop's own return address (`0x800462dc`)"* — correct, and the reason the run's
  clause (1) FAILs.

**And here is the finding.** The seam read `[slot] = 0x800b2648` and `[slot+4] = 0x8047c990`, before
*and* after the flush. The abort's `r11` (the popped `fp`) and `pc` (the popped `pc`) are **both
`0x05006e74`** — a value **neither of the seam's four readings contains**. 642 pre-registered exactly
this join — read `xnu_live_sleh_pc` against `b1`, `a1`, or `rtcpre_pop` — and **this is the first log
that carries both sides of it**. The join is now made, and it **disagrees with all three**:

* `sleh_pc = 0x05006e74` vs `b1 = a1 = 0x8047c990` — no match (`rtcpre_pop` is absent from this log,
  so that arm of the three-way cannot even be read).

> **CORRIGENDUM (656, measured 2026-09-24, host-side).** The parenthesised clause above is **false**: the
> log *does* carry `xnu_live_slot_rtcpre_pop=0x05006e74` (line 8358 of the archived capture), and the
> reader's own extractor sees it (`rtcpre_pop=$(keyval slot_rtcpre_pop)`, `run_and_capture.sh:1809`). The
> absent-key claim came from grepping `xnu_live_rtcpre_pop=` — the *argument* `keyval` takes — instead of
> the key the publisher writes, which carries a `slot_` infix (`xnu_live_slot_rtcpre_*`).
>
> **So the join does not disagree with all three; it matches exactly one of them.** `sleh_pc = far = pc =
> r11 = r4 = 0x05006e74 = rtcpre_pop`, while `b1 = a1 = 0x8047c990`. Two of the three arms fail and the
> third is an exact equality — which is a different finding from "matches none", and it narrows the
> paragraph below ("two mechanisms are consistent with what is measured"): see `experiment-656` §4.
> Nothing else in this record changes, and the pair's own reading (638 §3's second row) is untouched.

**So the pair the arm published is not the pair the `pop` consumed**, and that is a *different* defect
from the one the pair was built to test. Two mechanisms are consistent with what is measured, and this
log does not separate them:

1. **the slot was written between the seam and the pop** — some instruction after the seam's second
   read stores `0x05006e74` over the frame slot, and the `pop` then loads it. The `r4` register holds
   the same value, which is the shape of a `str r4, [sp, …]`;
2. **the `pop` read a different slot than the seam** — the seam's `sp` is the value at its own entry,
   and a frame adjustment between the seam and the pop would move it.

Both are read off the image, not guessed at; naming which one is the next instrument's job and **this
log cannot do it**. What this log *does* establish is the negative that matters: **`a1 == b1` is not a
statement that the slot was intact when the `pop` ran.** A reader who took the equal pair as "the two
words were unchanged" would be reading one moment's contents as another moment's — this project's
most-repeated defect, in the one cell where the two moments are 10 bytes apart in the same function.

## 5. Two defects in the reader, found by reading this log, and one of them is costly

**(a) The same image is named as two different arms in one run.** The reader printed, in this order:

```
  ARM   533's arm: xnu_live_slot_cwe_set=0x30c57879 has C clear …
        … xnu_live_seam_calls is present, so the arm under test is 535's, whose cache settings are 533's.
  xnu_live_seam_op=0x00000000  ARM 572: the interception with NO operation behind it …
```

Three labels — 533's, 535's and 572's — for an image that is **none of them**: it is the 574 park,
`IDLE_NO_SLEEP=0` + `SEAM_MEASURE=1` + `SEAM_POC=0`. The first label is right about the *cell* (533's
cache settings) and the second and third are inferences from two different keys that this arm
satisfies simultaneously: `seam_calls` present (which the reader takes as "535's arm, whose cache
settings are 533's") and `seam_op=0` (which it takes as "572's arm"). **The measure arm was built
precisely to be 535's interception with the operation removed, so it inherits 535's `seam_calls` and
572's `seam_op` at the same time** — and the reader has no state for that, so it names two arms.

**(b) And the wrong label is the one that drives a recommendation.** The clause (1) FAIL goes on:

> **and this image carries 535's arm**, so the new fault has two more candidates before that one: the
> seam's own two stores to the exit's frame slot, and the clean half of its operation writing back a
> line whose other words are the idle thread's.

**On this image neither candidate exists**: the measure form has no operation behind the interception
(`seam_op=0`, and the gate's own body check counts `FlushPoC_DcacheRegion` at **0** calls in this
image), so there is no "clean half" and no second store. The reader's next-arm recommendation is
therefore aimed at machinery this image does not contain — and it is aimed there *because of* the
label. That is the shape that costs a build and a press, so it is recorded as a defect and not as
prose: the predicate needs the `SEAM_POC`/`SEAM_MEASURE` pair, which the *record* carries and this
reader never reads.

Both are left **unrepaired**: they are the runner's own reading half, they are a different change from
the four that landed in `bc0d92a`, and the honest next move is to decide the next arm (§6) first,
because that decides which of the two is costly.

## 6. What this does not do, and what the goal still needs

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The OS is still not observed
  booting. The user-mode floor is present and PASSes — `open` 2 calls with the first answered by a
  driver, `read` returning 4 bytes whose first word is `0xfeedface`, `getpid`, `exit`, `wait`, two
  `ast` records — and **that floor is what 520 and 533 also met**, so it is evidence the boot still
  reaches pid 1's syscalls, not that it got further.
* **The frontier has not moved.** `progress: 3 user-mode fault record(s)` — 520 had 3 — and
  `sleh_storm=9`, which is 520's fatal run's count. The boot dies in the same function it died in
  before, now with a fault shape **neither 547 §4 nor 533 §5 names**.
* **It decided the repair, not the boot.** 597's candidate (A) is now the measured direction (§3), and
  candidate (A) is a change to `entry_trace.c` — a **build input**, so it needs a build that replaces
  the armed image. That is now permitted (the press is spent and the relay has stopped), and it is the
  next step and this session's, not a press.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet, and this run does not meet
  it.

## 7. What landed in the same window, and why it could

The press being spent ended the 620 hold, so `bc0d92a` landed the four repairs that had been staged
behind it: **637** and **640** in the gate (`87753103…` → `ad698429…`, 140 insertions / 40 deletions,
`bash -n` clean, gate re-run **exit 0 / 578 lines**), and **646** and **650** in the runner
(`9b2b5ba0…` → `bc291893…`, the two patches order-free, every hunk at line ≥ 818 inside
`summarise_log()`). The 640 repair is the one this run validates: the gate now prints *"for this arm
`xnu_live_seam_calls` is present"* only under `if [[ $V_IDLE_NO_SLEEP -eq 1 ]]`, and on the arm that was
sent (`=0`) the sentence is **true** — the run published `seam_calls=0x00000001`.

## 8. Safety

One `fastboot boot`, non-persistent; **`flash` was never used** and nothing was written to storage, so
no outcome of this run can write to storage and a brick is impossible by construction. The device
returned to Android on its own and was not hung. Everything else here is host-side: the captured log,
one `--summarise` run, greps, and four file edits that landed. Both catchers were alive through the
fire (`press-watcher.log`: catch 4 of 16 armed 14:02:43, fired 17:24:03) and the relay exited by its own
spent-press rule at 17:24:31, so the cover is **not** running now and the next press will need a fresh
arming.
