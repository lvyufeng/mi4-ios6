# 691: the press — the clock arm ends the run at 8.008 s, and the deadline is enforced to 61 µs

The press 690 armed was fired. **The device returned and the log came back** (exit 0, **28 s**), and the
pre-registered cell is filled: **`xnu_live_post_end_calls=0x00000008`** — the ending *fired*, on the eighth
deep-idle window, at an elapsed of **`xnu_live_post_elapsed=0x092a0bea` = 153,747,946 ticks = 8007.84 ms**.

**And the arm's other pre-registration held to the digit**: the abort's own record of its call site reads
**`xnu_live_sleh_lr=0x8047b5bc`**, the instruction after the `bl entry_seam_end_run` inside
`entry_post_clock` — where 688's arm read `0x8047ca0c` after its counted `bl` in the wrapper. The ending was
reached by a **tail branch** out of the exit wrapper, and the log says so.

**The press also answered a question this project has carried since its first timing figure and never
measured**: `xnu_live_post_cntfrq=0x0124f800` = **19,200,000 Hz** — the machine's own statement of the
generic timer's rate. Every millisecond in these documents was computed at 19,200 ticks/ms from *a comment
and a device tree*; it is now a reading.

**One press spent**: one readiness chain (**5/5**), one gate (**exit 0**, 558 stdout lines), exactly one
runner, one non-persistent `fastboot boot`. Nothing flashed, nothing written to storage (25 ×
`persistent_write_attempted=0x00000000`, none nonzero), no device state changed but the boot. The firer was
`press-on-clear.v5.sh` (sha `7f23cae5…`), armed by this step for `armed-post-endticks-b12616bc` with the
flags it derived from the arm's own switches (`--allow-xnu-entry`).

## 1. The press

| time (UTC) | event |
| --- | --- |
| 15:17:59 | the launcher armed for `armed-post-endticks-b12616bc`, flags `--allow-xnu-entry`, sha `7f23cae5…`, budget 600 s |
| 15:18:00 | the phone is on the bus (fastboot `[]`, adb `[4a2fe00b device]`); neighbour `33e80afe` absent |
| 15:18:33 | **readiness exit=0** — 5 of 5, including `the press would be caught` |
| 15:18:33 | `preflight_boot_check.sh --allow-xnu-entry` |
| 15:18:58 | **`GATE EXIT=0`, 558 stdout lines** |
| 15:18:58 | the one run: `run_and_capture.sh --allow-xnu-entry --expect-arm=armed-post-endticks-b12616bc` |
| 15:20:07 | **`RUNNER EXIT=0`** — returned and captured; *"the device came back 28s after this run called `fastboot boot`"* |

The capture is **613,085 bytes**, sha256
`4cfa68c0b653f59a…`, archived as
`out/stage90/captures/690-clockend-pressed-2026-09-25-last_kmsg.txt`. The phone was back on Android when the
run ended, and the neighbour never appeared.

## 2. The arm's answer, and the answer is a duration

```
xnu_live_seam_post_end_ticks=0x06ddd000      the arm's deadline: 115,200,000 ticks
xnu_live_seam_post_end_run=0x00000000        and the ending is NOT the count arm
xnu_live_post_cntfrq=0x0124f800              19,200,000 Hz - the hardware's own CNTFRQ
xnu_live_post_t0=0x07242b20                  the baseline: the counter at the wrapper's first return
xnu_live_post_elapsed=0x092a0bea             153,747,946 ticks = 8007.84 ms  <-- the answer
xnu_live_post_end_calls=0x00000008           the ending FIRED, on the 8th deep-idle window
xnu_live_sleh_pc=0x8047b488  xnu_live_sleh_lr=0x8047b5bc  xnu_live_sleh_far_frame=0x0fa0065c
```

The deadline is 6000.000 ms by construction. **The run lasted 8007.84 ms**, and the reason it is not 6000.0
is the arm's own stated resolution: the ending is decided *at a return through the exit wrapper*, so it can
only fire on a pass boundary — the run overshoots by whatever is left of the sleep it is in. Here that is
+2007.84 ms, and §3 shows it is exactly the last sleep.

**And the crossing is measured to 61 µs.** The eight `wfi` records the same run published carry the counter
before and after every real halt, so the timeline can be reconstructed against the baseline:

| # | wfi starts at (from `t0`) | halted for | elapsed at the return | past the 6.000 s deadline? |
| --- | --- | --- | --- | --- |
| 1 | −23.463 ms | 23.319 ms | −0.144 ms | no |
| 2 | 0.490 ms | 100.684 ms | 101.174 ms | no |
| 3 | 101.678 ms | 100.696 ms | 202.373 ms | no |
| 4 | 202.893 ms | 717.776 ms | 920.669 ms | no |
| 5 | 921.262 ms | 1062.052 ms | 1983.314 ms | no |
| 6 | 1985.268 ms | 2007.071 ms | 3992.340 ms | no |
| 7 | 3992.790 ms | 2007.149 ms | **5999.939 ms** | **no — by 1178 ticks (61 µs)** |
| 8 | **6000.474 ms** | 2007.221 ms | **8007.695 ms** | **YES** |

**The seventh pass returned 1,178 ticks short of the deadline** — 0.061 ms — so the ending did not fire, and
the eighth pass began 9,084 ticks (0.473 ms) past it and was ended on its return. That is the whole arm
working: the trigger is a comparison at a call site, and the log carries both sides of it.

The turnstile is exact in the other direction too: each `wfi`'s `after` sits 0.14 ms before the *same pass's*
`xnu_live_post_elapsed` publication (101.174 vs 101.314 ms; 920.669 vs 920.802; 8007.695 vs 8007.839), which
is the real `platform_cache_idle_exit`, the post note and the tail branch — so the trace and the arm's own
keys are two readings of one timeline that agree to a tenth of a millisecond.

## 3. The machine slept, repeatedly, and the process's own deadline was honoured

689 ended **inside** the fixture's 2000 ms park; this run finished it, twice.

```
xnu_live_poll_seq=0x00000003  timeout_ms=0x000007d0 (2000)  ticks=0x024c1c40  2007.44 ms
xnu_live_poll_seq=0x00000004  timeout_ms=0x000007d0 (2000)  ticks=0x024c2325  2007.60 ms
```

Both 2000 ms parks **returned**, and the fixture's own console report printed its park block
(`mini4: the OS has nothing to run -- pid 1 parked in poll for 2000 ms (caller 0x80286898)`). The sleeps the
kernel took under them are the last three `wfi` records: **2007.071, 2007.149 and 2007.221 ms** — the
process asked the kernel to sleep for two seconds and the kernel slept, three times, to within 0.2 ms of the
figure the fixture asked for.

The whole timeline of sleeps, in order: 23.3, 100.7, 100.7, 717.8, 1062.1, 2007.1, 2007.1, 2007.2 ms — the
first two short ones matching 689's own 21.2/100.6 ms pattern, then the parks.

**And the idle path is entered far more often than it sleeps.** The park's report reads *"the kernel's own
idle path was entered 58449 time(s) by that time"* and *"SetIdlePop entered 5 time(s) (TRUE 5, FALSE 0)"* —
~58,000 entries in the first park, **5** of which reached the deep-idle window. So the kernel's idle loop is
mostly not sleeping, and the deep window is the rare path; that ratio is a reading this arm produced for
free and is worth having before any driver work, because it says the CPU is not where the time is going.

## 4. The two nets, and which one brought the device back

Both were armed and **neither fired**: the payload's hardware watchdog (`hw_watchdog_timeout_s=0x19` = 25 s,
`counter_running=1`) and the dead-man (`deadman_interval_us=0x186a0` = 100 ms, 600 samples).

The return at **28 s** is the ending's own reset, and it is worth showing rather than asserting, because the
watchdog's bite falls at almost the same wall-clock moment. The ending's store faults at **8007.8 ms** past
the baseline, i.e. ~9.2 s after `fastboot boot` (the prelude is ~1.2 s, 633), and the entry epilogue's
PS_HOLD then resets the phone; 689's press measured Android as adb-visible **~19.6 s** after its own reset.
9.2 + 19.6 = **28.8 s** against a measured **28 s** — the PS_HOLD path, to within a second. Had the reset
come from the watchdog bite (armed at payload start, bite at ~28–29 s), Android would not have been visible
until ~48 s. **So the bite still has not been observed to fire**, and 664's claim holds from a fifth
direction: an abort cannot leave the phone dark, because both `fleh_dabort` exits reach that store.

## 5. A defect in the arm's own instrument, sharpened and now source-verified

689 §6 reported, and did not explain, three per-window counters that printed `1` in both passes while four
siblings advanced. **This run reproduces it and makes it much more specific.**

`entry_note_wfi` published **eight** records — one per halt — and **every one of them reads
`xnu_live_wfi_seq=0x00000001`**, with eight distinct `(before, after)` pairs. The publisher's own rule is
`n == 1 || (n & (n-1)) == 0` with `n = g_wfi_calls + 1`, so eight publishes at `n = 1` mean **the function
read `g_wfi_calls == 0` on every one of its eight calls, and its own `g_wfi_calls = n` store was invisible to
its next call**. The console side agrees: the park's own report reads *"the wfi reached **0** of the boot's
**0** time(s)"*, with `g_wfi_inst_seen` and all four first/last words also 0.

Five things were measured about it, and each closes a guess:

* **The store's address is right.** Disassembled, `entry_note_wfi` (`0x80006d30`) bases every store on
  `ip = 0x80546a78` with negative offsets: `str r1,[ip,#-3488]` → **0x80545cd8** = `g_wfi_calls`
  (the address `nm` gives), `#-3484` `g_wfi_fast_calls`, `#-3480` `g_wfi_ticks`, `#-3476` `g_wfi_ticks_max`,
  `#-3472` `g_wfi_inst_seen`. Every one resolves to the symbol the console line reads.
* **It is not a region effect.** `g_sip_calls`/`g_sip_true` (0x80545c84/0x80545c88) read **5** in the same
  park, `g_idle_calls` (0x80545c48) reads **58449**, `g_repair_calls` (0x80545cc8) reads 1, and
  `xnu_live_idlestack_calls` (0x80545dac) **advances 7 → 8** — all in the same `.bss` page, 0x54 to 0xD4
  bytes away from the failing group. So 689's scatter argument is now much stronger: **no zeroing of a range
  explains this**, and neither does a page- or section-level alias.
* **It is not "inside the window is lost" either.** `entry_note_wfi` runs inside the platform-cache window
  (its wrapper is between `__wrap_platform_cache_idle_enter` and `__wrap_platform_cache_idle_exit`), but so
  does the seam's `g_seam_calls` (0x805a05a4), which advanced to **8**.
* **Two different notes, two different wrappers.** `entry_idle_stack_note` is called from
  `__wrap_platform_cache_idle_enter` (`0x8047ca8c`, before the real enter) while `entry_note_wfi` is called
  from `__wrap_cpu_idle_wfi` (`0x8047ca30`, after the real `cpu_idle_wfi`) — so the surviving counter is
  written *outside* the window's own cache handling and the failing one *inside* it. That is a real
  difference and it is the best lead, but it is not yet an explanation, because `g_seam_calls` is inside too.
* **A label on two quantities, in one park.** The doors line prints *"through the wfi %d"* from `g_sip_true`
  (the count of `SetIdlePop` calls answered TRUE), while the line four lines later prints the actual
  `g_wfi_calls`. So the park's own report says **"through the wfi 5" and "the wfi reached 0 of the boot's
  0"** at the same instant, and the two numbers are different counters with one name. Whatever the cause of
  the zero, that label is wrong on its face and is the reason 689 read the `1`s as *counts*.

**Stated as an open reading, not a finding**, exactly as 689 left it: the candidates are the idle path's own
cache handling (a dirty line discarded by a clean-and-invalidate, which is this phase's own subject), or a
translation that differs inside the window, and the log in hand **separates none of them**. The arm that
would is one line of C — publish the readback of `g_wfi_calls` *inside* `entry_note_wfi`, immediately after
the store — and until it runs, **no `g_wfi_*` key may be read as a count** (the `wfi_ticks` in the live
records are per-call locals and are sound; the globals are not).

## 6. The goal's own criterion, re-read and unchanged

```
open    2 call(s), error in call order: 0x00000000 0x00000002
read    1 call(s), ret_lo=0x00000004 of nbytes=0x00000004; buffer 0x00102000 held 0xfeedface after it
getpid  1 call(s), value 0x00000001        exit 1, wait 2, ast 2
```

The floor holds: pid 1 runs, a character device answers the first `open` (`error 0x0`) while the control open
gets the `ENOENT` devfs has no node for (`0x2`), and a driver's `read` moved the fixture's `0xfeedface` into
a user page. **It is a floor and not this run's progress** — 520 and 533 met it too, because the whole
userland phase happens before the frontier. The abort count is unchanged (`sleh_storm=9`, `seen=9`, one
`panic(cpu`), so the boot did not get further in the OS's own terms; what it got further in is **time**:
8.008 s of live kernel against 689's two passes.

## 7. What this does not do

**It does not enter the OS in the sense the goal needs.** The boot is still the fixture's own path: pid 1, one
character device, `open`/`read`/`getpid`/`exit`/`wait`, a `poll` park, and the kernel's idle loop. No storage,
no filesystem, no second process, no driver beyond the fixture's. What this press bought is the **duration**
and the **rate** — the two numbers a driver-bring-up step needs before it can say how much time it has — plus
a measured defect in the instrument that reads it.

**The storage condition is still unmet and TWRP-to-storage stays withheld**: 「如果os已经能进去了的话」 asks
for a boot *observed* entering the OS and staying there, and this run was told to end after six seconds.

## 8. State

One press spent; the arm in `out/` is unchanged (`armed-post-endticks-b12616bc`, 11 files, recorded, parked,
gate green) and remains revertible. The launcher exited at 15:20:07 (`done. gate=0 runner=0`), so **no firer
is armed** and 660's closure has released. The phone is back on Android, the neighbour `33e80afe` is still
off the bus. Nothing was flashed and nothing was written to storage.

The next step is the one §5 names, and it is small: **an arm that publishes the readback inside
`entry_note_wfi`** — one store and one publish — which turns "the instrument's globals read zero" from an
open reading into either a lost store or a wrong address. It is worth doing before the next arm that reads
those keys, and it is cheap because the arm it needs is the one already parked plus three lines.
