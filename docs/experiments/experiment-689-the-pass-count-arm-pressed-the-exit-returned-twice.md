# 689: the press — the idle exit returned TWICE, and the kernel really slept

The press 688 armed was fired. **The device returned and the log came back** (exit 0, 21 s), and the key is
present at the arm's own number: **`xnu_live_slot_post_calls=0x00000002`**. The idle exit's `push {fp, lr}` /
`pop {fp, pc}` returned **twice**, and the ending fired on the second return — so one whole idle pass completed
past the frontier, which is exactly what 688 §7 pre-registered as this arm's positive cell.

**The log carries more than the arm's own instrument, and that is the step.** Both deep-idle passes went through
a **real `wfi` that really halted the CPU** (`wfi_inst=0xe320f003`, `wfi_fast=1`) for **21.2 ms** and
**100.6 ms**, and the run ended *inside the fixture's park* rather than at a frontier: the park's own record and
its console line are both absent, which is what a call that never returns looks like. The arm ended a machine
that was **asleep in its idle loop**, not one that was dying.

**One press spent: one readiness chain (5/5), one gate (exit 0, 564 stdout lines), exactly one runner, one
non-persistent `fastboot boot`. Nothing flashed, nothing written to storage (25 × `persistent_write_attempted=0`,
none nonzero), no device state changed but the boot.** The firer was `press-on-clear.v5.sh` (sha `7f23cae5…`),
armed by this step for `armed-post-endrun2-57d441cc` with the flags it derived from the arm's own switches.

## 1. The press

| time (UTC) | event |
| --- | --- |
| 14:33:13 | the launcher armed: `armed-post-endrun2-57d441cc`, flags `--allow-xnu-entry`, sha `7f23cae5…`, budget 600 s |
| 14:33:14 | the phone is on the bus (fastboot `[]`, adb `[4a2fe00b device]`), neighbour `33e80afe` absent |
| 14:33:49 | **readiness exit=0** — 5 of 5, including `the press would be caught` |
| 14:33:49 | `preflight_boot_check.sh --allow-xnu-entry` |
| 14:34:16 | **`GATE EXIT=0`, 564 stdout lines** |
| 14:34:16 | the one run: `run_and_capture.sh --allow-xnu-entry --expect-arm=armed-post-endrun2-57d441cc` |
| 14:35:15 | **`RUNNER EXIT=0`** — returned and captured; *"the device came back 21s after this run called `fastboot boot`"* |

The capture is **609,109 bytes**, sha256
`7014ff5d21b1e4bb93017029f9702d6b2463d2cc62f49183728e344dcd3c0e24`, archived as
`out/stage90/captures/689-countend2-pressed-2026-09-25-last_kmsg.txt`. The phone was back on Android when the
run ended (`sys.boot_completed=1`), and the neighbour never appeared.

## 2. The key, and the arm's answer

```
xnu_live_slot_post_calls=0x00000001      pass 1
xnu_live_slot_post_calls=0x00000002      pass 2  <-- THE ANSWER
xnu_live_slot_pre_calls=0x00000001 / 0x00000002
xnu_live_slot_rtcpre_calls=0x00000001 / 0x00000002
xnu_live_seam_calls=0x00000001 / 0x00000002       xnu_live_seam_op=0x00000001
xnu_live_poll_seq=0x00000001 / 0x00000002
xnu_live_pcx_seq=0x00000001 / 0x00000002
xnu_live_seam_b0=0x800b2648  xnu_live_seam_b1=0x8047c9c0
xnu_live_seam_a0=0x800b2648  xnu_live_seam_a1=0x8047c9c0
xnu_live_seam_sctlr=0x30c57879   xnu_live_seam_post_end_run=0x00000002  xnu_live_seam_end_run=0x00000000
```

Two complete sequences, not one: the pre site, the RTC/pop site, the seam with its operation, the post site and
the pcx note all published twice, in order, and the running counters went 1 → 2 rather than restarting. So the
second pass is **a second pass of the same loop**, not a re-publication.

* **`slot_post_calls=2`** is published by `entry_slot_null_note(&g_slot_post)` from the wrapper *after*
  `__real_platform_cache_idle_exit()` returns — so the exit completed twice, and the arm's ending (armed for the
  second return) then ran.
* `seam_post_end_run=2` is the arm's identity key: this is **688's pass-count arm** and not 686's, and its own
  number is what the log's counter had to reach.
* The pair came back unchanged, read with `SCTLR.C` clear — the same DRAM reading as 653's, and 686 §3's point
  stands: it is not this press's answer. What is, is the counter.
* The live channel is **not full** and no `xnu_live_capped` appears, so an absent key in this log is an event
  that did not happen.

## 3. The machine was asleep, and both sleeps are on the record

```
xnu_live_wfi_seq=0x00000001  fast=1  inst=0xe320f003  before=0x0737c3db  after=0x073dfc51  ticks=0x00063876
xnu_live_wfi_seq=0x00000001  fast=1  inst=0xe320f003  before=0x073e2ac3  after=0x075baa1d  ticks=0x001d7f5a
```

`entry_counter()` is the ARM generic timer's virtual count (the image's own comment: *at 19.2 MHz it wraps every
3.7 minutes*), so the two halts are **21.2 ms** and **100.6 ms** of stopped CPU. `wfi_inst` is read live through
Apple's own symbol — the value that would be `nop` if the `wfi` boot argument had patched the instruction out —
and it is `0xe320f003`, the `wfi` encoding; `fast=1` is the caller's argument, which selects one `wfi` rather
than the 32-iteration delay loop. **So the halt was a halt and not a busy-wait, and it happened twice.**

The two `poll` probes before the park agree with the fixture's own constants (`POLL_SHORT_MS` 5, `POLL_LONG_MS`
40) and show the same relation between the requested deadline and the measured sleep:

| call | `timeout_ms` | `ticks` | measured |
| --- | --- | --- | --- |
| `poll(NULL, 0, 5)` | `0x5` | `0x2cf9f` = 184,223 | 9.6 ms |
| `poll(NULL, 0, 40)` | `0x28` | `0xdfec9` = 917,193 | 47.8 ms |
| `poll(NULL, 0, 2000)` — 512's park | — | — | **no record at all** |

The third call is the park, and it has **no record and no console line** — `entry_note_poll` writes *after* the
real call and the console line (`the idle's doors …`) is printed on the park's own return, so the absence of
both is the arm's ending having fired **inside the park**. That places both deep-idle passes inside a 2,000 ms
park: the process asked the kernel for a 2 s deadline, the kernel idled, and the run ended ~130 ms into it.

`xnu_live_idle_seq` is the other end of the same window: it is the `machine_idle` counter, published at powers
of two (1, 2, 4, … 0x8000), so this run entered the idle path **at least 32,768 times** — a floor, not a count,
and one to read beside 512's own figure of 33,554,432 entries on a different arm. It is a *different site* from
the exit wrapper (513: `cpu_idle` leaves by three doors and only the `wfi` door reaches the platform-cache
window), so the two numbers are not a ratio: the exit wrapper's `2` is "deep-idle windows that returned", and
the idle counter's `≥32768` is "times the kernel had nothing to run".

## 4. The pre-registration was met, and it is worth quoting

688 §7, written before the press:

> | `0x2` (the arm's own number) | the boot completed **one whole idle pass** and reached the ending: the exit
> returned, `ClearIdlePop` and `cpu_idle_exit` ran, `cpu_idle` was re-entered, its early tests passed, the enter
> wrapper opened the window, the `wfi` halted, and the exit returned again. **The OS stays alive past the exit
> and keeps idling** … |

That cell is what the log carries, and the `wfi` records turn the sentence "the `wfi` halted" from the arm's
description of the sequence into a measurement of it (§3). **The other cell — `0x1`, the boot dying between the
passes — did not happen, and neither did the arm's one cost: no exit 2, no hang, no power press.**

The runner's own summary printed the arm's rule off `xnu_live_seam_post_end_run` as designed:

> `PASS  slot_post_calls=0x00000002 - the exit returned through the wrapper, which 520's run never did`
> … `AND ON THIS ARM THAT IS THE PRESS'S ANSWER (687). … so the boot completed 0x00000002 whole idle passes past
> the frontier … this says the OS **stays alive past it and keeps idling**`

## 5. The death this log also reports is the ending's own store, at the counted call site

```
panic(cpu 0 caller 0x80454668): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c
r12: 0xde58c6ab  sp: 0x8054fed0  lr: 0x8047ca0c  pc: 0x8047b488
xnu_live_sleh_pc=0x8047b488  xnu_live_sleh_lr=0x8047ca0c  xnu_live_sleh_far_frame=0x0fa0065c
xnu_live_sleh_seen=0x00000009   xnu_live_slot_ab_calls=0x00000009  xnu_live_slot_ab_rej=0x00000003
```

`pc=0x8047b488` is `entry_seam_end_run`'s fourth instruction, the restart-reason store to `0x0fa0065c` — the same
store that faulted on 687's press, for the same measured reason (XNU's pmap owns TTBR0 by then, 684).

**And the recorded `lr` is this arm's identity read a second way**: `0x8047ca0c` is the instruction after the
counted `bl entry_seam_end_run` at `0x8047ca08` (§688 §4's disassembly), where **687's press recorded
`0x8047c9ec`** for its own ending at `0x8047c9e8`. The two differ by exactly 0x20 — the 32 bytes the counted test
added — so the stub's own record of its call site says *which arm's ending ran*, exactly as 687 §3 used it.

## 6. One reading this press produced and does not explain

Three per-window counters published **`1` in both passes** while four others went 1 → 2:

| printed `1` twice | printed 1 then 2 |
| --- | --- |
| `xnu_live_slot_cwe_calls`, `xnu_live_pce_after_seq`, `xnu_live_wfi_seq` | `xnu_live_seam_calls`, `xnu_live_slot_pre_calls`, `xnu_live_slot_post_calls`, `xnu_live_pcx_seq` |

Each of the three publishes on `n == 1 || power of two` off a counter that is incremented unconditionally at the
top of its note, and **no line anywhere in the tree resets any of them** (`g_wfi_calls` has exactly one writer:
its own `g_wfi_calls = n`). So either the three counters were **reset between the passes by something not in the
source**, or a store to them was **lost** — the same family as the frontier's own stale-word, and the same kind
of claim this project refuses to make without a measurement. **It is reported as an open reading, not as a
finding**, with three things that can be said now. It is specific to those three keys: the seven other counters
are monotone across the same two passes. It is **not one zeroed range** — read out of this arm's own ELF, the
three sit scattered (`g_slot_cwe` 0x80510094, `g_wfi_calls` 0x80545cd8, `g_pce_after_calls` 0x80545d34) while a
monotone counter sits 0x10 below `g_wfi_calls` in the same `.bss` (`g_idle_calls` 0x80545c48), so a
`memset` of a region is refuted by the pattern rather than assumed away. And **it does not touch this arm's
answer**, which is `xnu_live_slot_post_calls` and which went 1 → 2 in the same log. The candidate this step would
name first is the idle path's own **cache handling** — `platform_cache_idle_enter`/`exit` is the one mechanism in
this image that runs between the two passes and can discard a store — and the next step that reads this log
should not read those three as per-run totals until it is settled.

## 7. What was already true and is re-read here

The userland floor is unchanged and complete: `open` 2 calls (the first answered by a driver, error `0x0`), `read`
1 returning the fixture's `0xfeedface`, `getpid` 1, `exit` 1, `wait` 2, `ast` 2. **Both nets were armed and
neither fired**: the payload's hardware watchdog (`hw_watchdog_timeout_s=0x19` = 25 s, `counter_running=1`,
`bite_truncated=0`, `hw_watchdog: armed and counting`) and the dead-man (`deadman_interval_us=0x186a0` = 100 ms,
600 samples). The run came back in 21 s, **before** the watchdog's 25 s, so the return is the entry image's own
epilogue path and not a net.

## 8. What this does not do, and the next question

**It does not enter the OS in the sense the goal needs.** What is measured is that the kernel's idle loop runs,
sleeps and wakes, twice, with a scheduler and a timer behind it; what is *not* measured is any driver beyond the
fixture's character device, any storage, any filesystem, or any process other than pid 1. The boot is still the
fixture's own path with the same 9 `sleh` aborts.

**It also does not test how long the machine stays up**, because the arm ends the run at a *count*. The next arm
should end it on a **clock**: an ending gated on a CNTVCT deadline at this same site — the wrapper already reads
the counter it would compare against (`entry_slot_rtc_note`'s `mrc 15, 0, r1, cr13, cr0, {4}`) — which turns
"two passes" into "what the boot does in N milliseconds", and is the reading the goal's own criterion will
eventually need. It is a bigger edit than a compare immediate and it would be the first arm whose ending is
independent of how many passes happen, which is also the first arm whose *negative* cell is "the machine stopped
before the clock ran out" rather than "the counter did not reach N".

**The storage condition is still unmet and TWRP-to-storage stays withheld**: 「如果os已经能进去了的话」 asks for a
boot *observed* entering the OS and staying there, and this run was told to stop after two idle passes.

## 9. State

One press spent; the arm in `out/` is unchanged (`armed-post-endrun2-57d441cc`, 11 files, recorded, parked, gate
green) and remains revertible. The launcher exited at 14:35:15 (`done. gate=0 runner=0`), so **no firer is
armed** and 660's closure has released. The phone is back on Android (`sys.boot_completed=1`), the neighbour
`33e80afe` is still off the bus. Nothing was flashed and nothing was written to storage.
