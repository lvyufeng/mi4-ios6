# 656: the join's third arm matches — and `slot_` names two instruments

642 pre-registered a three-way join on the death that follows the seam: read `xnu_live_sleh_pc` against
the seam's `b1`, against its `a1`, or against `rtcpre_pop`. 652 was the first log carrying both sides of
it, and it recorded the join as **disagreeing with all three** — "`rtcpre_pop` is absent from this log,
so that arm of the three-way cannot even be read". This step measures that clause and finds it false in
the way that matters most: **the third arm matches, exactly, and the absence was an artifact of a key
*name*.**

Nothing was built, nothing was run against a device, and no file the press fires was touched: this is the
archived 652 capture read with the runner's own extractor, plus the entry sources read for the publisher
of each key.

## 1. `rtcpre_pop` is present, and the reader can see it

From `out/stage90/captures/650-owed-run-park-574-2026-09-24-last_kmsg.txt`, line 8358:

```
 xnu_live_slot_rtcpre_pop=0x05006e74
```

and the reader resolves it with its own function (`run_and_capture.sh:1809`):

```
rtcpre_pop=$(keyval slot_rtcpre_pop)     # keyval: grep -ao "xnu_live_$1=[0-9a-fx]*" | tail -1
```

which returns `0x05006e74`. So the key is published, the extractor sees it, and the reader has the value
in hand. The fixture check in §3 confirms it end to end: the reader's own `CHANGED` cell prints
*"… and a1 is not this pass's rtcpre_pop=0x05006e74"*. **652's clause is false and 653's dependent claim is
false with it.**

**How the false absence was produced, and it is worth naming because it is not carelessness.** `keyval`'s
argument is `slot_rtcpre_pop`; the function prepends `xnu_live_` and searches for
`xnu_live_slot_rtcpre_pop=`. A reader checking the claim by hand reaches for the same string the *code*
uses — `xnu_live_rtcpre_pop` — which occurs **zero** times, and prints the reassuring answer: absent. The
check was run with the wrong name and returned a definite negative, which is the shape this project has
recorded before: **an absence claim is only as good as the extractor's pattern, and a pattern one token
off is a measurement of nothing** ([[mi4-measurement-defects]], and the guard is its own rule: before
concluding a value is absent, establish that the extractor could have seen it). The key's own comment
(`run_and_capture.sh:266`) spells the full name, so the information was on the same line as the mistake.

## 2. `slot_` is a prefix shared by two instruments, and the switch gates only one

The belief that `SLOT_NULL=1` removes `rtcpre_pop` is a plausible reading of the sources if the prefix is
taken as the family. It is not:

| family | keys | publisher | gated by `SLOT_NULL`? |
| --- | --- | --- | --- |
| the slot **null** instrument | `xnu_live_slot_sp`, `_m16`, `_m12`, `_m8`, `_m4`, `_calls` | `entry_slot_null_note`, inside `#if STAGE90_XNU_SLOT_NULL` (`entry_stubs.c:6315-6347`) | **yes** — that is what the switch is for |
| the **rtcpre** instrument | `xnu_live_slot_rtcpre_thr`, `_datap`, `_pop`, `_pcb`, `_sp`, `_lr`, `_calls`, `_rej`, `_rin` | `g_slot_rtcpre` (`entry_stubs.c:6403-6406`), a **separate key table with no `#if` guard** | **no** |

So one substring, two families, and the record inferred the second family's absence from the switch that
gates the first — [[mi4-one-value-two-definitions]] with the *name* as the shared quantity, which is its
mildest-looking and most expensive form: nothing here is wrong in the code, and the wrong belief is
unfalsifiable without going to the publisher. **Measured counterexample on the arm itself:** the 574 park
that ran carries `STAGE90_XNU_SLOT_NULL=1` (`arm-574-idle-no-sleep-0/xnu_arm_entry-config.txt`) *and* its
log carries `xnu_live_slot_rtcpre_pop`. The switch and the key coexist.

`SLOT_NULL` still stays `1` on the acting arm, and 653's decision not to change it still stands — an arm
that changes the operation and the instrument in the same pass attributes nothing. What changes is the
reason: not "it would recover a lost reading", but "it would add an unrelated behavioural difference".

## 3. The reader's third cell fires, measured rather than argued

653 recorded the `op=1` cell 3 (`a1 == rtcpre_pop` → `STALE LINE, WRITTEN OUT`) as **unreachable**. Built
here as **three one-variable fixtures** from the real 652 capture, so the operation is the only thing that
differs from the arm that ran (`sed 's/xnu_live_seam_op=0x00000000/xnu_live_seam_op=0x00000001/'` plus one
edit to `a1`), and each run through the reader's own CLI (`--summarise`, read-only):

| fixture | `seam_op` | `b1` / `a1` | `rtcpre_pop` | the reader printed |
| --- | --- | --- | --- | --- |
| f1 | `1` | `0x8047c990` / `0x8047c990` | `0x05006e74` | **`CLEAN LINE`** |
| f2 | `1` | `0x8047c990` / `0x8054fec0` | `0x05006e74` | **`CHANGED`** (and it printed `rtcpre_pop=0x05006e74` in its own sentence) |
| f3 | `1` | `0x8047c990` / `0x04b79075` | `0x04b79075` | **`STALE LINE, WRITTEN OUT`** |

All three cells exist, are distinguishable, and none needs the other to be read. **So the press's reading
is three-valued and all three values are readable** — which is what this step was for: the cells that will
read the coming press have now been *seen to fire*, which is the same standard the live-path battery holds
itself to (613's rule, 614's).

## 4. What the join's third arm means, and how it narrows 652's two mechanisms

The dump, read from the capture (`line 4002`):

```
panic(cpu 0 caller 0x80454584): sleh_abort: prefetch abort in kernel mode: fault_addr=0x5006e74
r0: 0x8051a000  r4: 0x05006e74  r5: 0x8051a000  r6: 0x8051a0e0  r9: 0xc0558df0
r11: 0x05006e74  sp: 0x8054fed0  pc: 0x05006e74  far: 0x05006e74
```

Three things line up that the earlier readings had only partly fixed:

* `r0 = r5 = 0x8051a000` is `cpu_data` (it equals `xnu_live_slot_rtcpre_datap`), and **`r6 = 0x8051a0e0 = cpu_data + 0xe0`** is `rtcpre_pop`'s **address** — the `add r6, r5, #224` of 590's chain, now visible in the same dump as the value it produced.
* `r4 = r11 = pc = far = 0x05006e74 = rtcpre_pop`. So **the word the `pop` consumed is this pass's `rtcpre_pop`** — the idle loop's own deadline — and the join's third arm is not merely present but *equal*.
* `sp = 0x8054fed0 = seam_sp + 8` (`seam_sp = 0x8054fec8`).

**The `sp` arithmetic rules out one of 652's two mechanisms.** The fault is a **prefetch** abort, and
`pop {fp, pc}` is `ldmia sp!, {fp, pc}`: the load committed and `sp` advanced by 8 before the fetch of the
new `pc` faulted. So `sp` at the `pop` was `0x8054fed0 - 8 = 0x8054fec8 = seam_sp`, and the `pop` loaded
its `pc` from `[seam_sp + 4]` — **the same word the seam read as `b1`**. (This is an inference from ARM's
semantics, not a measurement, and it is falsifiable the way the rest of this chain is: `sp - 8` must equal
`seam_sp`, and it does, exactly.) So "the `pop` read a different slot" is out, and the first mechanism
stands: **the word at that slot changed between the seam's non-cacheable read and the `pop`'s cacheable
one** — which is 597's stale-line account, with the line's contents now named.

**And the value says what the line held.** The seam read `0x8047c990` with `SCTLR.C` clear (DRAM: the
exit's pushed `lr`, and `seam_sctlr = 0x30c57879` has `C` clear); the `pop`, the first cacheable load
after the re-enable, got `0x05006e74`. A non-cacheable store — the exit's `push {fp, lr}`, and the enter
wrapper's `strd` before it — writes DRAM and **leaves the cache line untouched**, so a line left dirty
with the loop's deadline before the disable survives to answer the first lookup after it. That is
consistent with every number above and with 638's equal pair; it is stated as the reading the values
support, and **it is a mechanism, not a measurement** — the measurement that would separate it from "the
slot was overwritten by a later store" is the one this log cannot make.

**What this does to the goal.** It does not move the frontier: `poll_seq` still stops at 2 and
`slot_post_calls` is still absent. It makes the **one** press that is owed read as a decision with three
named outcomes instead of two plus a note, and it removes a false absence from the record that a
top-down reader would have taken as a reason not to look at `rtcpre_pop` at all.

## 5. What this does not do

* **It does not build, run, or edit anything the press fires.** No source, no image, no `out/` byte. The
  three fixtures live in `/tmp/r655/`; the reader was invoked with `--summarise`, which reaches
  `summarise_log` and never the gate or the plan. `run_and_capture.sh`, `preflight_boot_check.sh` and
  every xnu source are untouched — **and deliberately so, because a press watcher is armed**
  (`/tmp/r654/press-on-clear.sh`, waiting for the neighbour `33e80afe` to leave the bus): a watcher fires
  the runner *by path*, so its arming closes the runner's edit window even though no catcher is alive.
* **It does not touch the runner, and none is needed.** The reader is right in both places this step
  examined: `keyval slot_rtcpre_pop` is the correct call for the key `xnu_live_slot_rtcpre_pop`, and the
  three cells are correct. **The defect is entirely in the record**, which is why the repair is a
  corrigendum in two documents rather than a patch under a press.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The OS is still not observed booting,
  and **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet.
* **It does not claim the mechanism is settled.** §4's cache account is the reading the values support;
  the log cannot separate it from a later store to the same slot, and that separation needs a run.

## 6. Safety

No device action, no build, no `fastboot`, no `adb`, **nothing written to storage**. Every reading is a
read of an archived capture, of the runner's own source, or of the entry sources; the only writes anywhere
are the three fixtures and the reader's output under `/tmp/r655/`. `fastboot boot` only, never `flash`,
and no image was sent anywhere by this step. The press watcher's state is unchanged and reported: armed at
20:06:48 for 6 h, waiting on the neighbour.
