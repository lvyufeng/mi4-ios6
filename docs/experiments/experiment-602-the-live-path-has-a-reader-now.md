# 602: the live path has a reader now, and the two halves of "will the press produce one" are tests

The press is the scarcest thing this phase has, and the next one is owed. 601 asked whether the runner
would get through its own first step — and found that it would not, cleanly, on the state the machine
is *actually* in. That defect was reachable only by **running** the live path; every `--summarise`
reading this phase has taken ran on a file that existed, and the live path was the one with no reader.
601 §6 named the gap and did not close it: the rehearsal lived in `/tmp`.

This step closes it. `tools/rehearse_live_path.sh` drives the live path through every state it can
reach with **no device**, and asserts each state's exit code and the line its own wording promises. It
has two halves, because "the press is not wasted" needs both: the **runner** must reach the capture,
and the **reader** must be able to read what the capture contains.

Host-side only: one new tool under `tools/`, one file read (`run_and_capture.sh`), no build, no device,
no `fastboot`, no `adb`, nothing written to storage. **TWRP stays withheld** — 「如果os已经能进去了的话」
is unmet.

## 1. Safety by construction, and the assert that makes it a property

The harness cannot touch a device, and that is structural rather than careful:

* `sudo`, `adb` and `fastboot` are **replaced by stubs** that are first on `PATH`. `fastboot boot` in
  the stub is a `touch` on a marker file; `sudo dmesg` prints synthesised lines;
  `exec-out cat /proc/last_kmsg` `cat`s a copy.
* A bare `adb` or `fastboot` stub exists as well, so a code path that *bypassed* `sudo` still cannot
  reach a device — it hits `rehearse-stub: a bare adb call`.
* **Before any state runs**, the harness asserts that all three names resolve into its own stub
  directory, and refuses otherwise: *"a rehearsal that can reach a real device is not a rehearsal."*
  That assert is not decoration. 600 §7 lost a whole comparison to a copy of a script that was not
  executable, and a stub directory that failed to `chmod` is the same defect one layer down: without
  the assert, `command -v sudo` would be the real one and the next line would be a real `fastboot boot`.
* Anything the stub does not model is **refused by name** (`unhandled adb: …`), and a refusal is a
  **failure of the state**, whatever exit code the runner then produced — so an unmodelled call shows
  up as a refusal rather than as a silent pass.

## 2. Section A — the live path's states

Seven states, each driven end to end through the gate, the device detect, the boot, the return wait,
the capture and the reading. The expected line is quoted from the runner, so a state that keeps its
exit code but changes its message is caught too.

| state | what it models | exit | the line it must produce |
| --- | --- | --- | --- |
| `happy-adb` | in fastboot → boot → the phone re-enumerates → adb comes up → the capture reads | **0** | `reading the log this run captured` |
| `host-log-return-no-adb` | the phone returned into a state adb cannot reach; the host log sees it, the capture cannot | **3** | `REFUSING to call this a hang` |
| `no-return` | no adb entry and no new enumeration | **2** | `The device did not come back` |
| `host-log-unreadable` | `dmesg` itself fails — *not* a non-return and *not* exit 2 | **1** | `the host could not read its own USB log` |
| `no-fastboot-after-reboot` | adb mode, and the phone never appears in fastboot | **1** | `device did not appear in fastboot` |
| `happy-reads-the-log` | as `happy-adb`, and the run reads its own capture in the same invocation | **0** | `reading the log this run captured` |
| `exit3-log-was-parked` | exit 3 **with an earlier log at the name**: step 2b must park it and say where it went | **3** | `step 2b parked the previous run` |

All seven pass. The five exit codes the contract defines are all exercised (0, 1, 2, 3), and the two
exit-3 rows differ in the one thing that decides what the operator does next: whether an earlier log
exists and where it is.

The stub reproduces the *timing* the return criterion depends on, and that is the part worth having: the
serial is on `usb 3-10`, the count is **1** on the baseline the runner takes immediately after
`fastboot boot` returns, and the return appears on the **second** `dmesg` read after it — because a
baseline that already contained the return would leave the comparison nothing to detect. `no-return`
and `host-log-return-no-adb` differ by exactly that one marker, so the two paths are separated by the
mechanism rather than by a flag the harness sets and trusts.

## 3. The first draft's three failures — all of them the harness's, none of them the runner's

The first run reported **3 ok, 3 failed**. Every failure was in an expectation, and each is a lesson
worth keeping:

1. **The expectation was met on the other stream.** `host-log-unreadable` and
   `no-fastboot-after-reboot` end in `die`, and `die` writes to **stderr** while the check read stdout.
   The harness reported *"the runner did not say X"* about a runner that said X. Fixed by testing both
   streams together — a `die` message is as much the contract as a `say`. This is the harness's own
   version of the project's standing defect: an extractor that could not have seen the thing it
   declared absent.
2. **A marker meant the opposite of the state I named.** `host-log-return-no-adb` was given
   `no_enum_after_boot`, so the host log showed *no* return and the runner correctly printed
   `The device did not come back` and exited **2**. The runner was right and the row was wrong; the
   state as described in its own comment needs the enumeration *present* and the capture failed. A
   state named in prose and produced by a marker that contradicts it is the same shape as 598's
   selector and 600's sentence.
3. **An expectation that spans a line wrap matches nothing.** In section B, the rung-1b row asked for
   `below the park's own threshold`, and those two lines are printed as `… is 40 ms, below the` /
   `park's own threshold of 1000 ms` — the runner breaks its `say` strings by hand at ~88 columns. The
   harness reported *"the reader did not say X"* about a reader that said X in two pieces. Each
   expectation is now one printed line, and the tool says so where the rows are defined.

None of the three was a defect in `run_and_capture.sh`, and that is the finding: two of them
(1 and 3) are exactly the class this project keeps paying for, reproduced inside the check written to
detect it.

## 4. Section B — the reader, on every state the coming run can produce

The second half is the other way the press can be wasted: the log arrives and the reader cannot read
it. Six states, all built from the **sleeper arm's** signature — `door_seq` and `idle_seq` climbing the
powers of two with no ceiling, the poll records of a boot that reached the userland fixture, and
**none** of the window family — with one thing changed per row:

| variant | what it is | the line it must produce |
| --- | --- | --- |
| `predicted` | the arm's pre-registered reading: a third poll back at 2000 ms, `poll_over` published | `this arm did what it was built to do at the point that matters` |
| `poll-seq-2` | **falsifier A**: the park's poll never returned | `no poll record past the second` |
| `small-timeout` | **falsifier B**: a third poll returns, but its ask was 40 ms | `the largest recorded timeout is 40 ms` |
| `door-max-0x8000` | the machine died earlier — `door_seq` stops at the baseline's ceiling | `stops at or below 0x8000` |
| `no-door-seq` | `cpu_idle` was never entered: a fault earlier than either arm's | `carries no xnu_live_door_seq= record` |
| `no-poll-over` | the park returned and pid 1 never asked again | `this arm did what it was built to do at the point that matters` |

All six print their own line, and none of them prints nothing. Two readings in this table are worth
naming rather than leaving in the tool:

* **`door-max-0x8000` is the one state that can refute a PASS.** It fails rung 0 *and* passes rungs 1
  and 1b, so the operator sees a `FAIL` immediately followed by a positive verdict. That is not a
  contradiction the reader leaves unexplained: rung 0's own FAIL text ends with *"And if rung 1 below
  passes anyway the two disagree, which is itself the reading"*, and the reason they can is that rung 0
  reads a publisher's **schedule** while rung 1 reads the machine (596's correction, from the other
  direction).
* **`no-poll-over` is the one rung whose absence is silent, and that is deliberate.** `poll_over`
  publishes from the **fifth** poll on; its absence beside a passing rung 1 means pid 1 parked once and
  had nothing further to run — which is a healthy idle, not a fault. Printing "rung 3 absent" would be
  noise about a bonus witness. The tool asserts the *verdict* for that state instead, which is the
  contract, and this paragraph is where the reasoning lives.

## 5. The check is shown to fire — and the first attempt found a *different* check

A check whose success is all `ok` cannot be told from one that never ran, so the runner was doctored
**in place** and the tool re-run. The first doctoring changed an **exit code** (`exit 2` → `exit 7` in
the non-return path, one line), and the result was not what the rehearsal was aimed at:

```
  FAIL  happy-adb                    exit 1, promised 0
  ... all seven failed with exit 1 ...
REFUSING: at least one live-path state does not behave as its own contract says.
```

All seven because the **gate refused first**, before any state could run:

```
REFUSING: run_and_capture.sh's wait section can return 7, which is not a code this gate's reading
explains (2 3) - read section 4 and update this gate before spending a boot on a run whose status
it cannot narrate
```

That is a **better finding than the one being sought**, and it is now measured rather than assumed:
the gate holds a check *across the two files* — it reads the runner's exit sites at gate time and
refuses if the runner can return a code the gate's own section-4 wording does not explain. Its census
printed `distinct code(s) 2 3 7` and then refused, which is exactly the property that makes the gate
worth running on the broken build rather than the working one. 584 built the census; this step is the
first measurement of it *firing*.

The second doctoring changed a **message** instead (`The device did not come back` →
`The device did not return`), which the gate does not read, so the rehearsal's own refusal was the one
that had to speak:

```
  ok    happy-adb                    exit=0  reading the log this run captured
  ok    host-log-return-no-adb       exit=3  REFUSING to call this a hang
  FAIL  no-return                        did not say (either stream): The device did not come back
  ok    host-log-unreadable          exit=1  the host could not read its own USB log
  ok    no-fastboot-after-reboot     exit=1  device did not appear in fastboot
  ok    happy-reads-the-log          exit=0  reading the log this run captured
  ok    exit3-log-was-parked         exit=3  step 2b parked the previous run
  6 ok, 1 failed
```

**One state, its own name, and the line it did not say** — and the `exit N, promised N` part is
absent because the code still matched, so the *message* check is what caught it. That is the property
the two doctoring attempts between them establish: the exit-code half is caught by the **gate**, before
a boot, and the message half by the **rehearsal**, after it; neither can be changed silently.

The runner was restored with `git checkout` after each, and the restoration verified by sha256
(`7da957d4…` before, `7da957d4…` after) rather than by reading the diff — the file being doctored is
the file the tool reads, so leaving it modified would make every later reading in this phase suspect.

## 6. What this does not do

* **It does not boot anything, does not build anything, and changes no arm, payload, gate or
  prediction.** `out/stage90/stage90-qcdt.img` is still `60063c47…`, and the press is still the user's.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus and no host
  action substitutes for a power press.
* **Its states are models, not the machine.** The stub reproduces the sequencing the return criterion
  reads and the marker files the contract turns on; it does not reproduce USB, the watchdog, timing, or
  Android's real bring-up. A state it does not model is refused **by name** at the moment it is
  reached, which is what keeps the model honest — and the refusal fail is a failure of the row.
* **It is slow for a reason.** Each state runs the real gate (~30 s), so a full pass is about five
  minutes. The gate is the first step of the live path and caching it would be inventing a bypass that
  could diverge from what the run actually does.
* **It does not sweep the runner's other unsuppressed counts** (601 §7), or 597's two deferred
  `entry_trace.c` repairs, or 535/572's non-return. Those still need what they needed.

## 7. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One new
host-side tool (`tools/rehearse_live_path.sh`, 342 lines) and, for the refusal in §5, one single-line
edit to `stages/stage90/run_and_capture.sh` **reverted and verified by sha256 in the same step**. The
harness reads `run_and_capture.sh` and writes only inside a `mktemp -d` directory that its own `trap`
removes. The payload, the parked frozen pair at `/tmp/r594/frozen-payload/` and the arm the next press
sends are unmodified. `fastboot boot` only — never `flash` — so no outcome of any of this can write to
storage.
