# 690: the ending becomes a clock, and the run's length stops depending on a count

689 ended the last step with the shape of this one: the pass-count arm had measured that the OS stays
alive past the frontier and keeps idling, but **it said nothing about how long**: "two passes" was the
whole reading, and a pass is whatever the boot happens to do between two returns through the exit
wrapper. **`STAGE90_XNU_POST_END_TICKS`** replaces the count with an **elapsed-tick deadline** at the same
site and through the same call, so the arm now ends the run after a *duration* rather than after a
number of events — the first arm here whose ending is independent of how many passes happen, and the
first whose negative cell is *"the machine stopped before the clock ran out"* rather than *"the counter
did not reach N"*.

**Nothing was sent to the device in this step.** The arm is built, its bytes are measured, it is parked
and recorded by hand, the revert set verifies and readiness is 5 of 5 green — and the press is the next
step's act, because 660's rule forbids editing the closure while a firer is armed and this step still
had a tree to edit.

## 1. The switch, and the question of units

`STAGE90_XNU_POST_END_TICKS=115200000` — 115,200,000 ticks, i.e. **6000 ms** at the 19200 ticks/ms the
payload's own device tree states, i.e. **3× the fixture's 2000 ms park** (`PARK_MS` in
`entry_ramdisk.s`), so the boundary case lands *inside* a park rather than between two. The two triggers
are mutually exclusive, refused in `entry_trace.c` (`#error` on both being set) and in `build_entry.sh`,
and the switch is range-checked at compile time: floor 1,000,000 ticks (~52 ms) and ceiling 201,326,592
(0x0C000000, ~10.5 s), so an image whose deadline is a typo is a build failure and not a run that never
ends.

**Why ticks and not milliseconds, and where the rate comes from.** 19.2 MHz is, in this project, a
**comment and a device-tree claim** — `entry_counter()`'s own source calls it *"the ARM generic timer's
virtual count (at 19.2 MHz it wraps every 3.7 minutes)"*, and nothing in this tree has ever *measured*
it (grepped for the figure and for CNTFRQ across the tree). Rather than encode an unmeasured constant
into an arm that will be read as a duration, the switch is in **the units the hardware counts** and the
arm **publishes the machine's own statement of the rate**: `CNTFRQ` (`mrc p15, 0, r2, cr14, cr0, 0`,
read once beside the baseline) goes into the log as **`xnu_live_post_cntfrq`**. `run_and_capture.sh`
computes milliseconds from the log's own rate when it is present and **prints which source it used**
(the hardware's CNTFRQ, or the payload's device tree when the machine stated none). So the 6000 ms above
is the *expected* reading: **this arm is also the first thing in the project that puts the tick rate on
the record**, and if the machine states a rate other than 19,200,000 Hz then every millisecond figure in
these docs scales by that ratio — 689 §3's 21.2 ms and 100.6 ms, the poll overshoots, this deadline.

## 2. The arm, measured in the ELF

Read out of `out/stage90/xnu_arm_entry.elf`, not inferred from the switch:

* **`__wrap_platform_cache_idle_exit` (0x8047cad8) keeps its EIGHT-byte frame** — `str r4, [sp, #-8]!` /
  `str lr, [sp, #4]` — gives it back (`add sp, sp, #8`) and ends in a **tail branch**, not a call:
  `8047cb44: ldr r1, [r3, #24]` (`g_slot_post` 0x805100f8 + 24 = the post site's own `calls` field — no
  new counter and no new site) then `8047cb48: b 8047b530 <entry_post_clock>`.
  **That frame is the slot**: the exit's own `push {fp, lr}` writes *this* `sp` and its `pop {fp, pc}`
  reads it back, so a wrapper frame of any other size moves the reading to an address that is real and
  quiet and wrong (546).
* **`entry_post_clock` (0x8047b530)** takes `now` and `calls`. Its first-call path (`cmp r1, #1` /
  `bls`) reads CNTFRQ at 0x8047b5e4 and stores the baseline — `g_post_clock_freq` 0x805a0570,
  `g_post_clock_t0` 0x805a0574, `g_post_clock_elapsed` 0x805a0578, three `static uint32_t` = 12 bytes,
  which is why `.bss` grew by 8 and not 12 (the section's alignment).
* **The deadline is a `mov`/`movt` pair** — `mov r3, #0xd000` / `movt r3, #0x6dd` = **0x06ddd000 =
  115,200,000** — compared by `sub r0, r0, r2` / `cmp r0, r3` / `bcc` at 0x8047b55c–0x8047b568.
* **`entry_seam_end_run` is at 0x8047b47c in both ELFs — it did not move** — and the ending is
  `bl entry_seam_end_run` at 0x8047b5b8, whose **immediately preceding instruction is
  `bl entry_live_write`** at 0x8047b5b4: the publish is the last thing before the ending, *locally*.
* The four live keys are in the image verbatim: `xnu_live_post_t0`, `xnu_live_post_cntfrq`,
  `xnu_live_post_elapsed`, `xnu_live_post_end_calls`. **The fourth is published on the ending path only**,
  which is what makes the negative cell readable at all. The arm's identity is published by the seam as
  `xnu_live_seam_post_end_ticks` (0x06ddd000) beside `xnu_live_seam_end_run` (0).

## 3. The four defects the build caught, and why they are the step's instrument

Every one of these was a *plausible* arm that the build refused, and three of them were in the build's
own clauses rather than in the C:

1. **The inline clock made the wrapper's frame 16 bytes** (`strd r4, [sp, #-16]!`, `str r6, [sp, #8]`,
   `str lr, [sp, #12]`) — the slot moved, invisibly, and the arm would have published a stall word from
   a quiet address. Caught by the build's own frame clause; repaired by making the clock a `noinline`
   function called once as a tail branch.
2. **An early `return` in the baseline branch** let gcc lay out a tail-branched `entry_live_write`
   *after* the ending's block, so "the ending is the body's last call" failed. Repaired by one publish
   site reached by both paths.
3. **The "last call by address" test is not a fact about the function.** With one publish site, gcc
   still puts the first-pass branch and a duplicate publish block after the ending's `bl`, so the
   address-ordered last call is a publish in *another* branch. The clause was **weakened to a local
   property** — the instruction immediately before the ending's `bl` is a `bl entry_live_write` — with
   the reason recorded in the failure text rather than hidden by a looser test.
4. **Three shell variable-name slips** in the new image clauses (`pcbc`/`pcflag`, `pchi`/`phi`, and a
   `cmp` operand read from `$4` instead of `$5`), each of which made a **correct** image FAIL. The
   `cmp` one is the same defect class as [[mi4-one-value-two-definitions]] read from the other side: a
   parser that reads the wrong field reports zero and a zero reads like a clean image.

The build was walked from `exit 1` to `exit 0 / 0 FAILs`; the shape it then measured is §2.

## 4. The negative cell, and this arm's one cost

Ending on a clock moves the negative from *"the counter did not reach N"* to *"the machine stopped
before the clock ran out"*. If a pass dies where the first one did not, **the ending never fires**, and
the run is a no-return (exit 2) — the same cost as 688's, paid with a power press and not with the
device. And on an arm whose ending is a **store that aborts** (the restart-reason write faults under
XNU's own TTBR0 by then — 684), a no-return cannot be told from a hang by the log **at all**: the log
only comes back with a return, and a hang destroys the reading (663 §1). **No net is armed in these
bytes** — 666's hardware-watchdog self test is a different arm, and its bite has still never been
observed to fire.

## 5. The payload half, measured directly, and it closes exactly

`stage90.bin` differs from the 688 park in **586,101 bytes**: 586,096 of them inside the embedded entry
blob (span **494236..6014232** in `stage90.bin`, with 1,260 bytes of payload after it; 496,284 in the
qcdt, the same offset as 688's — the entry bin occurs verbatim and exactly once), and **five** in
`stage90_xnu_entry_run`, every one of which is the entry image's bss size:

| VA | parked | this arm | what it is |
| --- | --- | --- | --- |
| 0x4ac40 | `movw r3, #0x5c0` + `movt r3, #0x805a` | `movw r3, #0x5c8` | `BSS_END` 0x805a05c0 → 0x805a05c8 |
| 0x4ad04 | `movw r2, #0xcb40` | `movw r2, #0xcb48` | the bss byte count passed to `memset` |
| 0x4ad18 | `movw r1, #0xcb40` | `movw r1, #0xcb48` | the same count logged as `xnu_entry_bss_bytes` |

The first is also why the diff *looks* like a different instruction: at 0x5c0 the compiler could use a
rotate-imm8 `mov`, and 0x5c8 is not an 8-bit-rotated immediate, so it becomes a `movw`. **Five bytes,
all derivable from the 12 bytes of clock statics; no payload code differs.** Inside the blob the 586,096
bytes arrive as 64,485 runs, mostly two-byte runs at four-byte spacing — the `movw` immediates
renumbered because `.text` grew 480 bytes — with the largest single run 4,394 bytes.

`stage90.img` and `stage90-qcdt.img` differ in 586,121, and the extra 20 are at file offsets
**576..595**.

### 5.1 That field, and a sentence in the 688 record that is wrong

The 688 record says those 20 bytes are "inside the boot image's command line (cmdline offset 514..533) —
a 20-byte digest the build writes into the command line that follows the entry image". **Both halves are
wrong.** The v0 header's command line is 64..575, and 576 begins the **32-byte `id` field**, of which the
first 20 are the digest and the last 12 are zero. And it is not something written *after* the entry
image: it is `mkbootimg`'s own id over the kernel, and it is **reproduced here exactly** —

```
sha1(stage90.bin || len(stage90.bin) || ramdisk(empty) || 0 || second(empty) || 0)
  = b4895a518bd220d63aba5ccfd5582afb99031cfa   = the field at 576..595
```

It differs between arms because the payload differs, which is what an image digest is for. The correction
is in `revert-set.txt`'s 690 block, where the wrong sentence lives.

## 6. Parked, recorded, gated

Parked as **`armed-post-endticks-b12616bc`** (the entry bin's own sha prefix) into
`out/stage90/frozen/armed-post-endticks-b12616bc/` with `cp -p`, each file hashed in place and compared
against the live tree (all eleven agree, 15:09 UTC). The set line for every member was written by hand
into `stages/stage90/revert-set.txt` — **24 comment lines and 11 `set=` lines, including three
pre-registrations that can be checked in a log** (§7).

| file | bytes | note |
| --- | --- | --- |
| `xnu_arm_entry.bin` | 5,519,996 | `b12616bc…` — the one binary this step changed |
| `xnu_arm_entry.elf` | 6,698,832 | **+136 on 688's**: `.text` 0x50e900 → 0x50eae0 (+480), `.bss` +8, `.data` unmoved, entry 0x80000074, `BSS_START` 0x80543a80 |
| `xnu_arm_entry-config.txt` | 847 | **+37 on 688's** = the new `STAGE90_XNU_POST_END_TICKS=115200000` line |
| `stage90-qcdt.img` | 8,540,160 | `f5ed8a0b…` — what `fastboot boot` sends |
| `stage90.bin` / `.img` / `.elf` | | same sizes as 688's; the five bss immediates and the header `id` above |
| `SHA256SUMS.txt`, `xnu_arm_entry-sources.txt`, `stage90-build-config.txt`, `stage90_fixture.macho` | | the payload's switch record is **byte-identical** (`6c2b6038…`) to every arm since 653 — this file cannot name this arm |

Nine of the eleven differ from the 688 park and the two that agree are, as always,
`stage90-build-config.txt` and `stage90_fixture.macho` — the two files that do not contain the entry
image. **This is the first arm in this sequence whose artifacts change size** (the 688 record states all
eleven kept their byte counts against 686's).

Measured: `tools/verify_revert_set.sh out/stage90 --set=armed-post-endticks-b12616bc` → **exit 0**, 11
files plus 6 manifest-member checks; `tools/verify_press_ready.sh` → **5 of 5 green**, including row 5
(`adb` lists `4a2fe00b` as `device`, so a press would be caught) and the row that names the arm, which
now reads *"690's CLOCK arm"* — see §9 for the repair that made it say so.

## 7. Pre-registration, written before any press

The arm's own reading is one cell, and the log separates it from three others:

| log | cell |
| --- | --- |
| `xnu_live_post_end_calls` **present** | **the ending FIRED**: the boot stayed alive for the whole 6000 ms deadline and was then ended on purpose, on that many idle passes. The frontier moves from "two passes" to "≥ 6 s of live kernel with a working idle loop", which is the first duration this phase can state |
| `xnu_live_post_end_calls` **absent**, `xnu_live_post_elapsed` present | **the machine stopped before the clock ran out** — this arm's negative cell, at (at least) the last power-of-two elapsed it published. Comes back with a log only if the stop was a *return* |
| `xnu_live_post_elapsed` absent too, `t0`/`cntfrq` present | only the first return happened: the boot died in the second pass, before the first traced point |
| no `xnu_live_post_*` at all | the wrapper's first return never happened — the `pop` died on pass 1, and `xnu_live_slot_post_calls` would be absent with it |
| exit 2 | **nothing is read.** A non-return is not a verdict about the arm (663 §1) and owes a power press |

Two more readings are pre-registered because the ending moved one call deeper:

* **`xnu_live_sleh_lr` should be `0x8047b5bc`** — the instruction after the `bl entry_seam_end_run` inside
  `entry_post_clock` — where the 688 arm read `0x8047ca0c` (after its counted `bl` in the wrapper). The
  ending is reached by a **tail branch** out of the wrapper, so the wrapper's own return address
  (0x8000da40, from `cpu_idle`'s `bl __wrap_platform_cache_idle_exit` at 0x8000da3c) is in `lr` only up to
  0x8047cb3c and must **not** be what the abort shows. `xnu_live_sleh_pc` stays 0x8047b488.
* **`xnu_live_seam_end_run` is published as 0 on this image** — the string is in it (measured: 653's park
  does not carry it, this one does) — so an *absent* key here is a lost publication and not an image that
  predates 678.

## 8. What this does not do

**It does not enter the OS in the sense the goal needs, and it is not designed to.** The boot is still the
fixture's own path: pid 1, a character device, `open`/`read`/`getpid`/`exit`/`wait`, a `poll` park, and
the kernel's idle loop. No driver beyond the fixture's, no storage, no filesystem, no second process. What
this arm *adds* is a duration — and if it fires, the next arm can finally ask *how long is the boot alive
for* rather than *how many times did it come back*.

**The storage condition is still unmet and TWRP-to-storage stays withheld**: 「如果os已经能进去了的话」
asks for a boot *observed* entering the OS and staying there, and an arm that ends the run on purpose is
by construction not that observation.

## 9. One defect found by this step, in the readiness gate, repaired

`tools/verify_press_ready.sh`'s row 4 named this arm **green but wrong**: it printed *"the entry record
does not name STAGE90_XNU_SEAM_END_RUN at all, so this image predates 678 and its seam RETURNS … the log
will carry no `xnu_live_seam_end_run` key."* This record **does** name that switch — as `0` — and the key
string **is** in the image, so both halves of that sentence are false about this arm. The cause is a
fall-through: the row had branches for `SEAM_END_RUN=1` (678), `POST_END_RUN=1` (686) and
`POST_END_RUN≥2` (687), and *everything else* fell into the sentence about a record that predates the
switch. Two different facts therefore printed one sentence — and **690 is the first arm that reaches the
fall-through with the key present-and-zero** (688 escaped it through the `≥2` branch, 653 by genuinely
predating it).

The repair is three things, and the third is the one that matters: the ticks key is read and given its
own branch, so the arm is *named*; the fall-through now distinguishes **absent** from **named-as-0**; and
both branches are told apart by **the record**, not by the log — because the fact they disagree about is
whether the string is in the image, and that is checkable in the park. Re-run after the repair: **5 of 5
green**, row 4 reading *"690's CLOCK arm (STAGE90_XNU_POST_END_TICKS=115200000 ticks — 6000 ms at the
payload's own 19200 ticks/ms …)"* with both pre-registered readings of §7 attached.

## 10. State

The arm in `out/` is `armed-post-endticks-b12616bc` (11 files, recorded by hand, parked, `verify_revert_set`
exit 0, gate exit 0, readiness 5/5). **No firer is armed** — 689's launcher exited at 14:35:15 — and no
catcher has been alive since 2026-09-24 17:24:31. The phone is on the bus (`adb` lists `4a2fe00b` as
`device`) and the neighbour `33e80afe` is off it. Nothing was flashed, nothing was sent, and nothing was
written to storage. **The press is the next step's act**: arming a firer opens 660's closure, which
forbids editing the closure's files while it is armed.
