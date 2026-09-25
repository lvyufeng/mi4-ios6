# 669: the next press's reading — the runner measures the return time, and the launcher that fires it derives its arm

668 made a press *attributable*: the runner resolves which arm the bytes are, requires the caller to declare
the arm, and pins the hash across the send. This step is the other half of the same question, and it was
found by asking what the owed press's **log** would be read with.

The owed press is for the hardware-watchdog self-test arm (666), and its whole question is **whether the
SoC's own countdown reset the device** — a *time*, not a sentence. 663 §3.1 chose that arm because a hang
destroys the reading, so the net has to be proven before any design rests on it. Measured this step:

| what was in the tree | what it could not do |
| --- | --- |
| `run_and_capture.sh` had **no elapsed-time computation at all** | the interval between `fastboot boot` and the device's return — the arm's actual answer — was left to the operator subtracting two clock readings in their head, on the one run whose log is the only copy of the measurement |
| `summarise_log`'s marker table carried **two** of the arm's five `hw_watchdog_*` keys, and **neither** of its two SELFTEST sentences | the net's own evidence (`readback_ok`, `countdown_plausible`, `checksum`) was not counted, and the arm's own statement of its two candidate times was not printed anywhere |
| the arm's numbers arrive as **hex** (`log_hex32`, `stage90_main.c:1244-1254`) | the two readings the whole press is about are stated in the log as `~0x0000001c` and `~0x0000005a` — 28 and 90 — which is m672's shape: a value that *is* present, in an encoding a reader may not decode |

Nothing was fired, nothing was sent, and no byte under `out/` was written. Neither the gate nor the entry
builder was touched: `preflight_boot_check.sh` is the peer lane's file, and an edit to `xnu_arm_boot/**`
would move the entry sources the gate compares by content, i.e. refuse the arm that is currently owed a
press (668 §6).

## 1. The runner measures the send-to-return interval

`BOOT_SENT_AT` is taken immediately before `sudo fastboot boot`, and `mark_return HOW` — one definition, used
by all three tests that can see a return (adb, the host log's serial counter, the host log's port counter) —
fills `RETURN_AFTER_S` as `now - BOOT_SENT_AT`. Section 4 then prints it:

```
the device came back 29s after this run called `fastboot boot` (seen via: host log, serial).
```

Three decisions in that, each with a reason:

* **The clock starts at the send and not at the return's detection.** It is the interval the arm's own log
  line talks about ("the hardware countdown should reboot us at ~28s"), and it is measurable even when the
  return is seen by a test that fires seconds late.
* **It includes the transfer and the payload's start-up**, so it reads a little larger than the arm's own
  number — 633 measured 1.1805 s of that prelude for the park arm. The print says so rather than presenting
  the number as the arm's time; the two candidates are ~3.2x apart, so the prelude cannot change the verdict.
* **`RETURN_AFTER_S` is the empty string while no return has been seen, never zero.** Zero is a duration
  ("came back instantly") and "not measured" is a different fact; the print is gated on a non-empty value,
  so `--dry-run` and a run that never returned print no number at all.
* **It is printed in section 4, before the capture, and that placement is the point.** The number is a fact
  about the host's clock, so it does not need the log — and the state where the log is *lost* (exit 3, the
  device returned but adb never reached it) is exactly the state where the summary has nothing to read. A
  run that returns and fails to capture still prints the arm's answer.

## 2. The summariser joins the arm's two numbers to the run's one

A new clause in `summarise_log`'s `reading:` section, and it exists because **neither site has both
numbers**: the arm's two are in the log, the measured one is in the run. So the clause reads the arm's own
sentence out of the log, prints both numbers as decimals, and then prints what the run measured beside them.

* **The selector is the arm's own sentence**, `hw_watchdog SELFTEST: spinning`, and not a switch in the build
  record. `--summarise` is used on logs captured in other sessions, where no configuration file is being
  read — a clause gated on the record would be silent for exactly the logs a later reader brings back. A log
  that carries the sentence is a log from a self-test arm, whoever booted it.
* **Both spellings of the number are accepted** (`~0x0000001c` from `ram_console.c:37`, `~0000001c` from
  `xnu_log.c:29`'s raw variant), because which one wrote the line is a property of where the arm was when it
  logged and not of the arm; a reader requiring one spelling would report the arm's own numbers as absent.
* **One number found is a reading and not a crash**: the clause says it read only one, so the two candidate
  readings cannot be told apart from this log alone. And when the run's measurement is absent — every
  `--summarise` — the clause says **where it lives** (section 4's line) rather than printing a guess, and
  names the 3.2x separation so a hand-captured log is still readable with a stopwatch.
* **The marker table now carries all five keys and both sentences.** A key that is not in the table is a key
  nobody counts, and two of the three added ones (`readback_ok`, `countdown_plausible`) are the arm's own
  statement that the net was really armed — the *precondition*, which the readiness tool's row 4 already
  says is not the question and is still worth having.

## 3. Falsifications

| what was falsified | how | what it printed |
| --- | --- | --- |
| the clause, on a log that has the block | `--summarise` of a synthetic log with the `0x` spelling | both numbers as decimals (28 / 90) and the NOT-IN-THIS-LOG arm of the measurement |
| the encoding tolerance | the same line with the raw 8-digit spelling | **28 / 90** — the same reading, so the tolerance is real and not a second parser of one value |
| one number only | a spinner line carrying the first number and not the second | `only 1 of the arm's two numbers could be read out of that line` |
| the selector | a log with the five keys and no spinner line | the clause does not print at all (`grep -c` = 0) |
| the whole battery | `tools/rehearse_live_path.sh` after the change | see §5 |

## 4. The launcher that spends the press, derived and declared

The armed press launcher is a scratch file outside the tree, and 668 §6 recorded it stale in three ways.
668 made it four: the runner now refuses a live run with no `--expect-arm`, so the launcher's runner line is
refused before the gate. `press-on-clear.v3.sh` is the repair, **staged and rehearsed, not installed**:

* **The arm is declared, with no default** (`OWED-SET` as `$1`), for 668's reason one file over: a default is
  a name nobody typed. The launcher resolves the live arm itself (`tools/resolve_arm_set.sh`) and refuses
  unless the two agree.
* **The declaration is checked twice, with the wait in between** — once before the wait (so a mis-armed
  launcher fails in a second rather than six hours later) and once after it, because the wait is hours long
  and a build landing inside it replaces the arm under a launcher that has already decided. That is R4's pin,
  one file over.
* **The flags come from `tools/gate_flags_for_arm.sh`** and are passed to both the gate and the runner, plus
  `--expect-arm=$OWED` on the runner only — an asymmetry the launcher's own comment explains, because the
  gate has no such argument and refuses unknown ones.
* **Readiness is tested, not `say`ed** (661 R1), and it is called with no `--set`/`--park` so the set is
  *found* from the bytes (667).

**Rehearsal: 25 rows, 0 failed**, and the harness is falsified by six mutants of the launcher, each of which
turns the rows it should: dropping `--expect-arm` (2 rows red), removing the pre-wait comparison (2),
`say`ing readiness instead of testing it (2), dropping the post-wait re-check (2), giving the gate the owed
*name* instead of the derived flags (2), and asking for the flags but not passing them (1). One case runs the
**real tree, real tools and real bytes** with only `sudo` stubbed so the neighbour reads as parked — the flow
resolves the live arm, derives the two flags, and stops in the wait, before any gate or runner.

**The harness's own first draft was the defect this project ranks first to suspect, and it was caught by the
expectation rather than by review.** The driver wrote each mutant to `mut.sh` and ran the harness — which
hard-coded the *unmutated* path — and so printed the same `25 ok, 0 failed` for all six, i.e. a green table
for a file nobody had changed. The check that caught it was the driver's own "did the row I expected go
red"; the fix is that the script under test is a parameter and every run prints its **sha256**. That is
m663's shape (a knob that reads as armed while having no effect) recurring in a new place, and it is why the
row count alone is not the evidence in this section — the mutants are.

## 5. The battery, after the change

`tools/rehearse_live_path.sh` re-run against the runner with the timing in it: **20 ok / 0 failed**, **15 ok / 0
failed** reader states, **4 ok / 0 failed** path states, exit 0. The battery asserts phrases and exit codes, so
the three new `say` lines had to be checked for collisions with the states that *forbid* a phrase — none, and
the run's own `sha256sum -c` **after** the battery shows the runner byte-identical across it, so that is a
verdict about one revision and not a table taken across a write (m662's rule).

And the new print is the one thing the tally cannot show, so it was read out of the per-state outputs (the
harness deletes them on exit; this run used a copy whose `cleanup()` keeps them, `diff` against the real file:
**one line**, deleted before the commit):

* **it fired in 8 of the 20 states**, including both of the mechanisms the battery can produce —
  `seen via: adb` (`happy-adb` 1s, `adb-plan-line` 0s, `happy-reads-the-log` 0s) and
  `seen via: host log, serial` (`neighbour-leaves-fastboot` 3s, `host-log-return-no-adb` 3s,
  `exit3-log-was-parked` 3s, `boot-call-fails-after-send` 3s, `fastboot-plan-line` 3s);
* **it fired in none of the 7 states that do not return** (`no-return`, `port-underivable`,
  `host-log-unreadable`, `fastboot-other-after-wait`, `one-device-wrong-serial`, `adb-unauthorized`,
  `adb-offline`), where `RETURN_AFTER_S` is empty and the print is gated on a non-empty value;
* and **`0s` is printed as a measurement**, not as an absent value — which is the empty-versus-zero
  distinction the design turns on, seen from the other side: the stubs return instantly, so a real zero is
  available and only appears where a return was seen.

## 6. Findings recorded and not fixed

* **One of `mark_return`'s three call sites is not exercised by the battery, and it is named here rather than
  covered by a cell that would be flaky.** The port-only return **inside** the wait window
  (`mark_return "host log, port"`) has no state: the battery's `qdl-return` cell deliberately places its
  port-only return *after* the window (`return_after:5`), and the runner's own in-wait band with the battery's
  shortened `RETURN_TIMEOUT=6` is `(3.05, 6.05]` — one sleep wide, as that cell's own comment says, so any
  value that lands inside it has under a second of margin on one side. A cell that is flaky is worse than a
  named gap, so the gap is named. What is checked instead is structural: `mark_return` has exactly three call
  sites, one per return test, each passing a distinct `HOW` string, and the two the battery *can* produce are
  both seen in the artifacts above. The port reading's other half is exercised — `qdl-return` covers the
  post-wait port branch (`ENUM_ADVANCED=port`).
* **The launcher is staged and NOT installed.** Installing it is one `cp`, and it must not happen before a
  fresh arming is possible, because 660's closure rule is about what a *live* catcher fires. The file lives at
  `/tmp/g668/press-on-clear.v3.sh` with its rehearsal beside it.
* **The return-time print is in section 4 and the arm's numbers are in the summary**, so a reader who greps
  only one of the two sees half the join. Deliberate: the run and the log are different artifacts, and the
  summary says where the other half lives.
* **Nothing in this step changes what the owed press will do.** The arm in `out/` is unchanged
  (`armed-selftest-wdog-ef0361a2`), and R4 refuses any run that does not name it.

## 7. Safety, and what this does not do

Host-side only: no `fastboot`, no `adb`, no boot, no byte under `out/` written. The rehearsal's only `sudo`,
`fastboot` and `adb` are stubs in `/tmp/g668/fake/bin`, asserted to resolve there before any case runs, and
the one real-tree case stubs `sudo` alone so the flow stops in the wait. `fastboot boot` only, never `flash`;
nothing written to storage; the neighbour's serial `33e80afe` was not touched.

**It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The frontier is where 652 left it — XNU
reaches pid 1, runs the userland phase, dies at the idle exit's `pop {fp, pc}` — and this step produces no
boot and no reading of the device: it makes the *next* press's answer a number the tooling computes instead
of a subtraction the operator performs. **TWRP-to-storage stays withheld**, because 「如果os已经能进去了的话」
is still unmet.
