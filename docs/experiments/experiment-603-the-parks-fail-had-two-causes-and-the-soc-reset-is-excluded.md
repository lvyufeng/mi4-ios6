# 603: the park's FAIL had two causes, and the SoC's own reset is excluded by measurement

594 pre-registered the coming run's witness (`xnu_live_poll_seq=3` with `poll_timeout_ms >= 1000`, a
record structurally impossible on the frozen arm) and its **falsifier**: `poll_seq` still 2.

A falsifier is only worth pre-registering if reading it tells you something, and this one has **two
causes**. The park's poll not coming back can be:

* **the arm's failure mode** — the spin is alive and never progresses, so `__real_poll` does not
  return (593 section 4's reading, and the thing the arm exists to test); or
* **the SoC being reset out from under the park** — the payload arms the MSM8974 hardware watchdog
  before the jump and nothing in the image pets it, so the SoC resets itself on a timer.

Only the first is about the arm. If the second were live, a `FAIL` on this rung would be a statement
about the watchdog wearing the arm's name — one value with two definitions, which is this project's
most-repeated defect. This step excludes it, on **533's own capture**, using the counter that the
payload and XNU both publish.

Host-side only: one narration block added to `stages/stage90/run_and_capture.sh`, one change to
`tools/rehearse_live_path.sh` (a row can now carry more than one expectation, and a new state makes
the added block's second branch reachable), reads of two archived captures. No build, no device, no
`fastboot`, no `adb`, nothing written to storage, no boot. **TWRP stays withheld** —
「如果os已经能进去了的话」 is unmet.

## 1. One counter, and how that is a measurement rather than an assumption

`timebase_cntfrq=0x0124f800` is 19,200,000 — the payload's own record of the ARM generic counter's
frequency, and 533's capture carries it. The scale checks out twice inside the same log:

| record | value | ticks | seconds |
| --- | --- | --- | --- |
| `xnu_live_tmr_dl_delta` (a 2000 ms deadline) | `0x024a3a28` | 38,418,984 | **2.00099** |
| `xnu_live_poll_ticks`, poll 1 (a 5 ms ask) | `0x000203c7` | 132,039 | 0.00688 |
| `xnu_live_poll_ticks`, poll 2 (a 40 ms ask) | `0x000d5976` | 874,870 | 0.04557 |

The 2.00099 s is the calibration: a deadline the kernel computed as 2000 ms away reads as 2.00099 s,
which puts the counter's rate within 0.05 % of the declared 19.2 MHz.

**That the payload's records and XNU's are on the same counter is the load-bearing fact, so it is
checked rather than assumed.** The payload writes first (it runs first), and if the two were on
different clocks the log's counter-valued records would not be ordered along it. They are — strictly,
from the payload's first sample to the park's last record:

| line | record | counter | seconds |
| --- | --- | --- | --- |
| 156 | `timebase_boot_ticks_lo` (payload sample 1) | `0x035499b4` | 2.9102 |
| 279 | `timebase_boot_ticks_lo` (payload sample 2) | `0x0355ded6` | 2.9145 |
| 4689 | `xnu_live_tmr_dl_now` (first XNU deadline read) | `0x049827e7` | 4.0146 |
| 8093 | `xnu_live_tmr_dl_now` (last before the park) | `0x04a205dd` | 4.0483 |
| 8319 | `xnu_live_repair_before` (the park's entry snapshot) | `0x04afba8b` | 4.0951 |
| 8356 | `xnu_live_wfi_after` (the last record of the boot) | `0x04b79097` | 4.1218 |

Monotone in line order, across the payload/XNU boundary, with no wrap in an 8,400-line capture. So
the log *is* in write order over these records and the two writers *are* on one clock. **A check that
itself flags the wrong thing is a check nobody reads**, so it is worth saying that this monotonicity
pass flagged three "inversions" on its first run and all three were `xnu_live_poll_ticks` and
`xnu_live_wait_ticks` — keys I had included knowing they are *durations* and not counter readings.
The predicate was wrong, not the log.

## 2. The reset's own interval, from two records that cross-check each other

The payload arms the watchdog at `stage90_main.c:1205`, before the jump, and nothing in the image
pets it. The interval is stated in the log twice over, and the two agree exactly:

| record | value | / `hw_watchdog_hz` (`0x7ffd` = 32,765) |
| --- | --- | --- |
| `hw_watchdog_bark_ticks_written` `0x000c7fb5` | 819,125 | **25.0000 s** |
| `hw_watchdog_bite_ticks_written` `0x000dffac` | 917,420 | **28.0000 s** |

against `hw_watchdog_timeout_s=0x19` = **25**. So 25.0000 s computed from the tick count equals 25 s
written as seconds on its own — a cross-check between two independent records of the same fact, and
the bark-to-bite gap is the 3 s. **The SoC resets 28.0 s after the arm.**

And the arm is *before* the payload's timebase sample, by log order (the arm's records are lines
7–27; the samples are 156 and 279), with the ordering proven by the same monotonicity as section 1.
So the reset is at `timebase_boot_ticks_lo + 28 s` at the latest.

## 3. The budget

Everything that matters is now on one clock:

| | counter | seconds |
| --- | --- | --- |
| the payload's timebase sample #2 | `0x0355ded6` | 2.9145 |
| the park's entry snapshot (`repair_before`) | `0x04afba8b` | 4.0951 |
| **the boot's own share** | 83,234,165 ticks | **1.1805 s** |
| where the park's 2000 ms ask would return | — | 6.0951 |
| the reset, at the latest | — | 30.9145 |
| **margin at the park's return** | | **>= 24.8 s** |

The park is 2000 ms and the budget it is drawn on is 28 s; the boot spends about a second of that
before the park even starts. A park that fails to return is therefore **not** a park that ran out of
SoC. Read this FAIL as the arm, which is what the rung was written to read.

**And 533 gives the falsifier its own measured shape**, which is the same number from the other
direction: the park's entry snapshot is at 4.0951 s, and the *last* record the boot ever wrote is
that window's `wfi_after` at 4.1218 s. So on the frozen arm the park lasted **26.7 ms of its 2000**,
and the poll whose thin absence the rung reports never returned because the machine died inside it
26 ms in. That is what `poll_seq` stopping at 2 looks like when it is the arm, and it is 24.8 s away
from looking like a reset.

## 4. What this changes, and what it does not

* **The falsifier stays the falsifier and becomes informative.** "`poll_seq` is still 2" no longer
  has a second cause to be argued about at 3 a.m. from a log that only says the machine came back.
* **Rung 3's `poll_over` is reachable inside the same budget.** `poll_over` publishes from the fifth
  poll; with 2 s parks the fifth arrives about 10 s in, against a 30.9 s ceiling. That does not
  predict it — the `no-poll-over` row of 602's section B still exists and its NOTE is still a real
  outcome — it says the ceiling is not what removes it.
* **It does not make the interval derivable from the log in hand, and that is left owed on purpose.**
  The key that would mark the park's start on the *sleeper* arm is the one key that arm cannot
  publish: `xnu_live_repair_before` lives inside 594's `#if !STAGE90_XNU_IDLE_NO_SLEEP` guard, so a
  derivation keyed on it would be `UNREAD` in exactly the state it exists for (586's lesson). The
  payload's own records would support it — `hw_watchdog_timeout_s`, `hw_watchdog_hz`,
  `bark_ticks_written`, `bite_ticks_written` are all in the log — and that arithmetic is named here
  as owed and not taken.

## 5. The contradiction the first draft of this block created

The block was first written **unconditional**, saying the watchdog is armed and the reset is 28 s.
Twelve lines above it, the same function prints

```
  hardware watchdog: NOT confirmed armed (no counter_running=1). If this run also
                     failed, the watchdog is the first thing to investigate.
```

on any log that lost the payload's records — and on such a log the two sentences disagree about the
same instrument, in the same output, one screen apart. **A step whose whole subject is "exclude the
second cause" had introduced a second definition of the first cause**, which is the defect class it
was written against, one layer up.

The fix is a guard on the very count that other line reads (`watchdog > 0`, `grep -c
'hw_watchdog_counter_running=0x00000001'`), so the two cannot disagree by construction: with the arm
record the reader may say the reset is excluded; without it, it must say the interval is **cited and
not read from this log**, and that the margin is 533's and not the log's. Both branches are now
states in the rehearsal (§6).

## 6. The rehearsal change, and the check shown to fire

Three changes to `tools/rehearse_live_path.sh`:

1. **A row may carry more than one expectation.** The added block is a *branch inside* the rung-1
   FAIL, guarded on the arm record, and one expectation per row could only ever test the first line
   of whichever branch ran — the second branch would have been written down and reachable by nothing,
   which is 602's own lesson about this file one step later. `reader_state` now takes N expectations
   and every one must appear.
2. **The `ok` line says how many it carried** (`(+1 more)`). A row with two expectations that printed
   only its first would look exactly like a row with one, and the second is the one that tests the
   branch.
3. **A new state, `poll-seq-2-no-arm`**, which is the falsifier with the payload's arm record removed —
   so the block's *else* branch is a state and not a paragraph. `mk_sleeper_log`'s base now carries
   `hw_watchdog_counter_running=0x00000001`, as every real capture does (533 has it), so the six
   pre-existing states exercise the *then* branch.

| state | expectations |
| --- | --- |
| `poll-seq-2` | `no poll record past the second` **and** `the SoC's own reset is not what stopped it - measured, not argued` |
| `poll-seq-2-no-arm` | `no poll record past the second` **and** `whether the reset is what stopped it is not read here` |

**Shown to fire**: the phrase in the first row was doctored in place — "measured, not **argued**" to
"measured, not **assumed**", one word — and the rehearsal reported one state failing, named it, and
quoted the line it did not say, with the exit code untouched (so it was the *text* check that caught
it and not the code check). The runner was restored from a copy taken before the doctoring and
verified by sha256, because `git checkout` would have reverted this step's own edit along with it.

Full pass: **7 ok / 0 failed** (section A, the live path's states) and **7 ok / 0 failed** (section B,
the reading), exit 0, and the gate `preflight_boot_check.sh --allow-xnu-entry` → **EXIT=0 / 518
lines / 0 stderr**, its exit census still 3 sites with codes `2 3`. The gate reads the runner's exit
sites and this step adds no `exit`, no `die` and no `say` outside rung 1's FAIL — verified by the
census not moving, not by reading the diff.

## 7. What this does not do

* **It does not boot anything, and it changes no arm, payload, prediction or gate verdict.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the parked arm is still the sleeper
  (`STAGE90_XNU_IDLE_NO_SLEEP=1`, entry `696a0f39…`), and the press is still the user's.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus and no
  host action substitutes for a power press.
* **It does not make the park's return more likely** — it makes a park that does not return
  *readable*. The arm is unchanged and its witness is unchanged.
* **It does not sweep this file's other `FAIL` branches for the same shape** — a FAIL with a cause
  that belongs to the harness, the SoC or the capture rather than to the arm. Rung 1 is the one that
  was written about a falsifier, so it is the one where a second cause would have been read as the
  arm. That sweep is owed.

## 8. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. Two
host-side files edited, neither in `xnu_arm_entry-sources.txt` (the gate's freshness scan
deliberately does not match `*.sh`): `stages/stage90/run_and_capture.sh` (a comment block and two
`say` groups inside rung 1's FAIL branch, no `exit`, no `die`) and `tools/rehearse_live_path.sh`.
Reads of 533's and 520's archived captures. One single-word doctoring of the runner, reverted from a
copy and verified by sha256. The payload, the parked frozen pair at `/tmp/r594/frozen-payload/` and
the arm the next press sends are unmodified. `fastboot boot` only — never `flash` — so no outcome of
any of this can write to storage.
