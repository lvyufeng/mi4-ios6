# 755: the OS's own `poll()` is late by exactly the storage probe's blocking commands — and the wall clock re-derives 746's headline

**Read from the archived captures and the payload source. No device action of any kind** — no `fastboot`,
no `adb` to the device, no press, no gate, no runner, no build, and `out/` untouched.

**The one-line finding.** Nine captures carry both a `poll(2000 ms)` the OS made and the storage probe's
own command timings, and the two are the **same number**: the poll returns **exactly as late as the probe
overran the OS's own 2000 ms window**, and the probe's blocking time is published, command by command,
under other keys. Across the ladder the lateness goes **7.6–9.3 ms → 427 ms → 1624 ms → 425 ms**, and each
step is a command whose `_*_ticks` reads **1.2000 s** to within half a millisecond. **So 746's headline —
*the response demand was the stall* — is confirmed a second time from a cell that has nothing to do with
the command path: the operating system's own wall clock, which regained exactly 1.2 s when rung 20 removed
the demand.** And 754 §4 is corrected: its example named a key at the wrong **rung spelling**.

## 0. Why this was read out of a capture

It is one cell the archive already holds nine times, and the comparison is arithmetic between two
independently-published quantities. **A press would re-measure numbers already in hand.** This is the same
economy 749, 750 and 753 were written under.

## 1. The cell, and what it measures — from the source

`entry_stubs.c:4561` (the 503 comment starts at `:4523`) publishes ten keys per `poll()`, of which four are the call's inputs and four its
outcome. The two that matter here are the bracketing readings, and the source is explicit about what their
difference is:

> **`after - before` is the step's headline number, and it is measured here rather than inferred.**
> `poll` with no descriptors blocks: the deadline is armed in `kqueue_scan` … the thread parks in
> `thread_block_parameter`, and the only thing that can return it is `thread_timer_expire`'s
> `clear_wait_internal(thread, THREAD_TIMED_OUT)` … running out of the timer interrupt 483 armed. So the
> interval between the wrapper's two readings is *the wall time the kernel kept this thread parked*

**"the wall time the kernel kept this thread parked"** — and that is the whole finding. Wall time is not
the timeout: it is the timeout **or whatever kept the CPU busy instead**, whichever is longer. The record
says so itself and then treats the number as a timer reading; §4 below is the archive showing the
difference.

## 2. The measurement: nine captures, and the probe's own blocking time beside it

`p3` is the third `poll` in the boot — the first one that asks for 2000 ms. The "full-bound polls" are the
storage probe's command polls whose own published `_*_ticks` exceed 1.0 s, i.e. the ones that burned their
entire bound:

| capture | `p3` measured | `p3 − 2000 ms` | the probe's full-bound command polls | sum | sum − 2000 ms |
| --- | --- | --- | --- | --- | --- |
| rung6 | 2.0093 | **+9.3 ms** | – (none) | 0 | 0 |
| rung8 | 2.0082 | **+8.2 ms** | – (none) | 0 | 0 |
| rung9 | 2.0088 | **+8.8 ms** | – (none) | 0 | 0 |
| rung12 | 2.0076 | **+7.6 ms** | `cmd0` 1.2001 | 1.2001 | 0 |
| rung13 | 2.0093 | **+9.3 ms** | `cmd0` 1.2002 | 1.2002 | 0 |
| rung14 | 2.0084 | **+8.4 ms** | `cmd0` 1.2001 | 1.2001 | 0 |
| rung15 | 2.0090 | **+9.0 ms** | `cmd1` 1.2001 | 1.2001 | 0 |
| rung16 | 2.0077 | **+7.7 ms** | – (none) | 0 | 0 |
| **rung17** | **2.4270** | **+427.0 ms** | `cmd1` 1.2000, `cid` 1.2001 | 2.4001 | **+400.1 ms** |
| **rung18** | **2.4220** | **+422.0 ms** | `cmd1` 1.2002, `cid` 1.2002 | 2.4003 | **+400.3 ms** |
| **rung19** | **3.6241** | **+1624.1 ms** | `cmd1` 1.2001, `cid` 1.2001, `rca` 1.2000 | 3.6003 | **+1600.3 ms** |
| **rung20** | **2.4249** | **+424.9 ms** | `cmd1` 1.2000, `cid` 1.2002 | 2.4002 | **+400.2 ms** |

**Two regimes, and the boundary is a command count.** Where the probe's full-bound total stays under the
OS's 2000 ms window, the poll is clean to **7.6–9.3 ms** — the poll's own entry and exit, a constant.
Where it exceeds the window, the poll's lateness is the excess **plus a residual of 21.6–26.8 ms** (the
tail of the probe after its last command). The residual is not a fitted number: it is what is left when
the measured lateness has the probe's own published durations subtracted from it, and it is stable to
5 ms across four rungs.

**Which rungs have which polls is itself a published reading.** At rungs 12–14 the probe's CMD0 is stuck
(`cmd0_complete = 0`, `cmd0_ticks = 1.2001 s`) and `st_cmd_path` takes its early return at
`entry_storage.c:3685` — so **CMD1 never runs and there is no `cmd1_*` key at all** (0 occurrences in
those three captures, against 30 in each of rungs 15 and 17–20). At rung 16 the earlier gate at `:3556`
refuses and **no command runs**. So the table's "none" rows are facts about the image, not missing cells —
and `mi4-silence-is-a-reading-only-if-success-is-silent` is why they are stated as counts (0 vs 30) rather
than as blanks.

## 3. The two arithmetic checks the table makes possible

**(a) The rung mapping is confirmed four times over, from the guards alone.** The captures are named in the
record's **ordinal** spelling and the `#if` guards count the **switch value**; the record says they are one
apart. Four independent keys confirm `value = ordinal − 1` in this table without anyone having to trust
that statement:

| key | its own guard (from `tools/read_storage_key_order.py`) | first capture that carries it | implied value |
| --- | --- | --- | --- |
| `_cmd0_ticks` | `STAGE90_XNU_STORAGE_PROBE >= 11` | **rung12** | 11 |
| `_cid_ticks` | `>= 16` | **rung17** | 16 |
| `_rca_ticks` | `== 18` | **rung19** | 18 |
| `_nrsp_ticks` | `== 19` | **rung20** | 19 |

**(b) The 1.2 s is one bound, measured five times.** `cmd0`, `cmd1`, `cid`, `rca` and (at rung 20) the
absent `nrsp` all read **1.2000–1.2002 s** on every rung they appear. Five different commands, five
different words on the bus, one bound — `STAGE90_XNU_CMD_TIMEOUT` in ticks — and the *only* one that is
not 1.2 s is `_nrsp_ticks = 0.0003 s`, the command rung 20 built to ask for nothing back.

## 4. What it does to 746's headline: an independent second confirmation

746 concluded *the response demand was the stall*, from the ladder's own five-command table — a comparison
of `_*_complete`, `_*_status_any` and `_*_ticks` **between** commands. This document reaches the same
conclusion from a quantity outside that ladder entirely: **the operating system's own `poll()` lateness.**

| | rung 19 (value 18) | rung 20 (value 19) | difference |
| --- | --- | --- | --- |
| `_rca_ticks` / `_nrsp_ticks` | 1.2000 s | **0.0003 s** | −1.1997 s |
| the probe's full-bound total | 3.6003 s | 2.4002 s | **−1.2001 s** |
| **the OS's poll lateness** | **1624.1 ms** | **424.9 ms** | **−1199.2 ms** |

**The OS got back 1.1992 s of its own wall clock** — 1199.2 ms measured against 1200.1 ms of probe time
removed, a 0.9 ms agreement between two cells published by different code for different purposes. One
constant moved (`RSP_SHORT` → `NONE` on one command word) and a user-mode process's sleep got 1.2 s
shorter.

**What this is not.** It is not a new claim about *why* the response demand stalls the block — 746's
reading of that is the ladder's own, and §7 is explicit. What it is: **a second, independent witness that
the stall is real and that its cost is exactly one command bound**, measured on a clock the ladder does not
own.

## 5. A constant worth having: the poll-to-poll handoff is 1.5 ms on twelve boots

`_poll_seq = 4`'s `before` minus `_poll_seq = 3`'s `after` — the gap between one poll returning and the
next entering — across every capture carrying both:

    1.505, 1.507, 1.508, 1.516, 1.516, 1.517, 1.518, 1.521, 1.521, 1.521, 1.529    (ms)

twelve boots, nine different rungs, **a 24 µs spread on a 1.5 ms quantity (1.6 %)**. That is the cost of a
syscall's second entry plus `kqueue` setup plus the timer arm — a number the record did not have, and a
useful baseline if a future rung changes the timer path, because a regression there is the kind of thing
that would show up here first.

## 6. The record's own falsifier, and it fired — with the right explanation

503's source comment states the design and its own failure mode:

> **Two calls with timeouts in an 8:1 ratio make it a shape and not a sample**: a wake that came from
> anywhere but the countdown — the scheduler's quantum, a stray interrupt, a poll loop — returns after
> about the same interval whatever was asked for, and the ratio would read about 1.

**The archive holds both halves of that test, and they disagree in exactly the way the comment predicts:**

| the pair | asked for | measured | asked-for gap | measured gap |
| --- | --- | --- | --- | --- |
| polls 1 and 2 | 5 ms / 40 ms | 7.5–12.1 / 46.1–49.8 ms | 35 ms | **37.69–38.57 ms** |
| polls 3 and 4 (rungs 17–20) | 2000 ms / 2000 ms | 2422–3624 / 2011–2016 ms | 0 ms | 411–1609 ms |

**The 5/40 pair passes the test and passes it tightly**: the *difference* between the two durations is
37.69–38.57 ms for a 35 ms difference in what was asked — **constant to 0.88 ms across thirteen boots**, so
the interval does track the countdown. The *ratio* only looks compressed (4.1–6.1 against 8.0) because each
call carries a few milliseconds of its own overhead, which the difference cancels.

**The 2000/2000 pair fails it, and fails it on rungs 17–20 only** — same ask, and the two durations differ
by 411–1609 ms. §2 says why: **the wake came from something other than the countdown, and the something is
this image's own storage probe.** The comment's falsifier fired, the record can explain it, and the
explanation is the ladder's, not the kernel's.

**The correction this implies.** 503's cell is a **timer-latency** reading only while the image is quiet.
On rungs 17–20 it is a **wall-clock** reading and its excess is the probe's. A sentence that quotes
`_poll_ticks` on those rungs as *how long the kernel kept the thread parked* is quoting a number that has
two producers — the timeout **and** this image — and `_poll_timeout_ms` is the key that tells a reader
which. That is `m760`'s class, and it is now a known property of a landed instrument rather than a surprise.

## 7. A correction to 754 §4 — the same defect, one level up

**754 §4 is wrong in one sentence, and this is the correction.** It said:

> These are rungs 19, 20 and 16 — the three ordinal arms whose bodies were replaced rather than added. …
> A reader holding a rung-20 capture and a sentence quoting `_nrsp_complete` is quoting a cell that
> **cannot exist in that image**.

**`_nrsp_complete` exists in the rung-20 capture**, and its guard is `STAGE90_XNU_STORAGE_PROBE == 19`.
The two halves of that sentence use the two different spellings of "rung 20": the tool's report was the
**switch value** (20, the armed arm, where `nrsp` is indeed dead), and the capture named `rung20-nrsp` is
the **ordinal** rung 20 = value **19**, where the `nrsp` body is exactly the one that ran. The claim is
true of the armed arm and false of the capture it names.

**The cause is the tool's own header, which carried only one spelling** — the one it read from `out/`.
Fixed rather than annotated: **every header now prints both**, the ordinal derived in the tool:

    # rung 20 (switch VALUE; the record's ordinal rung 21) [the image in out/ was built at this value; ...]

and `--rung 21` still refuses with the note that the ladder's "rung 21" **is** value 20. The `--selftest`
is unmoved by the change and `make check` still exits 0.

**This is the fifth instance in five experiments of the same family** — a name with two readings, 746 §4b's
CMD1/CMD2 merge, 749's branch 1, 752's branch 2, 754's four tool defects, and now this — and it is the
first that landed *inside a tool whose own refusal message warns about it*. The warning was in the
refusal path; the header, which is what a reader actually looks at, did not carry it.

## 8. What the OS is doing when the arm's deadline fires

Not a defect, and it is the reading the second strategic decision needs. `entry_post_clock`
(`entry_trace.c:2358`) is explicit:

    g_post_clock_elapsed = now - g_post_clock_t0;
    end_now = (g_post_clock_elapsed >= (uint32_t)STAGE90_XNU_POST_END_TICKS) ? 1u : 0u;
    ...
    if (end_now != 0u)
        entry_seam_end_run();          /* THE ENDING */

**So the 6-second deadline is a hard stop, and it is the only thing that ends a healthy run.** And the run
it ends is healthy: at the buzzer the boot has, in order, made four recorded polls plus `_poll_over = 1`
(the fifth), **successfully `open`ed `/dev/rmd0` (`error = 0`) and been refused `/dev/nosuch`
(`error = 2`, `ENOENT`)** — 503's designed control pair — reaped a child (`_wait_status = 0x300`) and
settled into a repeating `poll(nfds = 0, timeout = 2000 ms)` sleep loop that `_poll_retval = 0` and
`_poll_error = 0` say returns **normally every time**. The gap from the last recorded poll to the deadline
is **4.0249 s = exactly 2.000 × a 2000 ms poll**, and `_poll_over` is **1 rather than 2**, which is what the
record predicts if the run ended *while* the sixth poll was parked.

**Stated as a reading and not as a verdict:** the archive shows a running OS in a timed sleep loop, not a
hung one, at the moment the arm stops it. **Whether it would stay up is still not measured** — that is the
`STAGE90_XNU_POST_END_TICKS = 0` arm, and it is the operator's decision. This section narrows what that
arm would be testing: not *does the OS survive*, but *for how long does an idle OS with a 2-second sleep
loop keep returning*.

## 9. What this document does not say

- **It does not say the kernel's timer is late.** §6 says the opposite: the 5/40 pair scales with its
  timeouts, so the deadline path works; the 2000 ms pair is late because **this image** was busy.
- **It does not change 746's headline, only corroborates it.** §4's independent witness agrees with 746 to
  0.9 ms and adds no mechanism.
- **It does not re-derive 754's other three defects**, and §7 corrects §4 of it only. 754's tool audit,
  its four measured defects and its `make check` clause all stand.
- **It does not say the rungs before 17 had no storage cost.** They had 1.2 s of it — it simply fitted
  inside the OS's 2000 ms window, so the poll could not see it. **A cost that fits inside someone else's
  timeout is invisible to that timeout**, which is the general form of the whole finding.
- **It does not arm anything.** No press is owed, no firer is armed, `out/` is untouched, and **no press
  may be spent without the operator's authorization.** Rung 21 remains **ARMED AND NOT PRESSED**.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. It re-derives a landed
  conclusion from a second clock, corrects a landed document's spelling, and puts a number on what the
  storage probe costs the OS — and that is the whole of what it claims.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist; 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
