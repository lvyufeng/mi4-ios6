# 581: the run reads the log it just captured

The runner opened by summarising the **previous** run's log and, after spending a boot to capture a new
one, ended at `done. Full log: <name>`. The reading of the log the run had just paid for was a command
the operator had to know to type. That asymmetry is removed here: **the captured log is read where it is
produced.**

This is a runner change and nothing else. No device action, no arm touched: the entry image is still the
frozen 574 arm (`151425c4…` / `3bc72605…`), the gate is green on it (`EXIT=0`, 0 UNREAD), and the phone
has been off the bus since 535's boot.

## 1. The asymmetry, and why it is this project's kind of defect

`run_and_capture.sh` calls `summarise_log` twice before it ever reaches the device - once as
`--summarise FILE` for an already-captured log, and once in step `payload output` over `$LOGFILE`
as it stands at the start of the run. Every failure path in the wait section prints its reading in
full: section 4 prints the two counts and the sentence that says which producer fired, step 5 prints
the capture failure and where the previous log was parked, and clause (5)'s three-way dispatch lives in
`summarise_log`.

The one path that **spends a run and produces a reading** printed least: `wrote N bytes to <name>`, then
`done. Full log: <name>`. On this phase's runs the boot is a single chance - the payload's log lives in
the top of DRAM and a second boot overwrites it (574 section 3) - so "the operator knows to type
`--summarise`" is a reading held in a person's memory at the one moment it is least recoverable. The
project's own rule is that the reading is printed where it is produced rather than left to be
remembered, and the file already had the function; it was only not called there.

## 2. The change, and where it sits

Inside the capture arm, **after** the payload-line check and not before it:

```sh
say ""
step "reading the log this run captured"
summarise_log "$LOGFILE"
```

Three placement decisions, each with a reason:

- **Inside the capture arm** (`DRY_RUN -eq 0`, capture succeeded), so it cannot run on a dry run, cannot
  run on any of the four exit paths in the wait section, and cannot run when the capture failed - the
  cases where there is no new log or where the message that matters is the failure's.
- **After** the "fewer than 8 payload lines" warning, so a suspect file is read *under* that warning
  rather than instead of it. That warning is about whether the file is this run's at all; the summary is
  about what it says, and reading it first would bury the caveat under a table.
- **A read of a file already on disk.** It touches no device, cannot spend anything, and cannot change
  the capture. `--summarise` remains for re-reading a log later, and both paths print the same table
  from the same function - one definition of the reading, not two.

No exit code, no device call and no capture logic changed: only `say`-producing output moved into a path
that already existed.

## 3. Why this is safe to land one step before the expensive run

The hazard in touching the runner now is that a change to it could break the run it exists for, so the
check is not "does the new block print" but "did anything else move".

- `bash -n` clean; the block is `say`/`step`/one function call, and the function's last statement is an
  `if` with no `else`, i.e. status 0 on both branches - the property that was *itself* a defect once
  (243-249: a trailing `[[ ... ]] && say ...` returned 1 with an abort-free log and, under `set -e`, the
  caller exited 1 **before booting anything**, which is how four exp-267 "runs" re-summarised a stale
  log and reported nothing).
- **The success path still exits 0** and now prints the reading
  (`581-rehearsal-success-path-now-prints-the-reading.txt`): `wrote 399 bytes` -> the new block -> `done.
  Full log:`, exit 0, 0 FAIL.
- **All six reachable outcomes of section 4 and step 5 are unchanged**, exit code for exit code, with
  `new-block=0` in every one of them (`581-rehearsal-six-states.txt`) - which is the check that the new
  block did not leak into a path that must not have it: `1 / 1 / 2 / 2 / 3-from-section-4 /
  3-from-step-5`, exactly as 580 measured.
- **`--dry-run` exits 0** and does not print the block (`581-rehearsal-dry-run-no-block.txt`).
- **The gate is green after the change** (`EXIT=0`, 0 UNREAD, the section-4 census still
  `3 exit site(s), code(s) 2 3`) - the gate reads the runner's exit sites at gate time, so a change to
  that file is exactly what it exists to re-read.

## 4. The same path exercised on a 574-shaped log

The reading that matters on the coming run is clause (5)'s dispatch on `xnu_live_seam_op`, and the new
block is where it will now be printed. So the whole success path was rehearsed with a stub whose
`exec-out` returns a **574-shaped** log - 520/568's real capture with the seam keys, the arm's `sp`
corrected to the slot (`0x8054fec8`) and `xnu_live_seam_op=0x00000000` added - and the block printed the
whole reading, **exit 0**, taking the `op=0` branch:

```
xnu_live_seam_op=0x00000000  ARM 572: the interception with NO operation behind it
PASS  seam_lr=0x800462dc is the exit's own call's return address ...
PASS  seam_sp=0x8054fec8 is the exit's frame slot: it is sleh_sp-8 ...
THE L1 FLUSH WRITES IT BACK  the pair changed (b=0xc819bfa0/0x80017330 -> ...)
```

**(That log is synthetic and is parked under a name that says so**
(`581-SYNTHETIC-NOT-A-CAPTURE-574-shaped-log.txt`) because a file that already carries the expected
shape is the artifact-as-prediction defect this project has been bitten by twice (562, 563): it is an
input to a rehearsal, it must never be read as a capture, and the bracket it carries is 520's.
The branch it took is the one that input produces and **not** a prediction of the run: with `op=0`, an
unequal pair is Apple's own L1 flush writing the line back and an equal pair is the arm working as
designed, and 574's run is the measurement that decides which.)

## 5. Safety, and what is still owed

No device action, no `fastboot`, no `adb`, nothing written to storage, no arm changed: one runner edit,
seven rehearsals against stubs in `/tmp`, and one read-only gate run. The frozen arm is untouched and
still unrun, and the phone is still off the bus.

**The boot still waits only on the user's power press.** The automatic path is armed for it (a
session-only watcher that detects the return, runs the gate, fires **one** boot, reports the exit code
against the contract, deletes itself, and does not touch the device on any other path). What the run
should be read for, in order: the **exit code** (0 captured / 1 a host-side failure that says nothing
about the device / 2 a non-return, power press needed / 3 it returned and the capture failed - **not** a
hang), then the death's shape, then `xnu_live_seam_*` with `op=0`.

TWRP stays withheld: 「如果os已经能进去了的话」 is unmet.
