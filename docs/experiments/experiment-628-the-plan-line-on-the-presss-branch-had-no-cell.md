# 628: the plan line on the branch the press takes was asserted by nothing

`run_and_capture.sh`'s boot step prints a plan line, and 620 made it mode-selected, because the
unconditional `sudo adb -s $SERIAL reboot bootloader` was **false on the fastboot path** - that path
issues no adb command at all:

```sh
case ${MODE:-} in
  fastboot)
    say "fastboot boot -s $SERIAL $IMAGE"
    say "  (the device is already in fastboot, so this run issues no adb command at all)" ;;
  *)
    say "sudo adb -s $SERIAL reboot bootloader   # then fastboot boot, never flash" ;;
esac
```

620 added a cell for the adb half (`adb-plan-line`, which asserts the line's presence). **Nothing ever
asserted the fastboot half's text** - the only cell that named it was `boot-call-fails`, which
**forbids** the adb line. A forbidding expectation is satisfied by a `case` that prints nothing, and
this is 613's rule: to assert a phrase's absence, the detector must first be shown to see it when
present.

**And this is the branch the press takes.** Every state in the live-path section starts with the phone
in fastboot and no adb (`fastboot_up` is the section's default marker, `adb_up` is opt-in), which is
exactly what Vol-Down + Power produces - so the unasserted branch is the *normal* path, not the corner.
The same asymmetry 620 found in the plan line itself, one layer up: twenty steps of "the operator's
own record must not name an action the run does not take", and the record for the action the press
*does* take was the one nobody read.

**Measured, not argued.** With the `fastboot)` arm replaced by `: ;;` on a copy, and the harness as it
stood before this step:

| section | mutant |
| --- | --- |
| A, the live-path states | **19 ok / 0 failed** |
| B, the reading | **15 ok / 0 failed** |

Both green. The press's plan line could have been deleted with no cell noticing.

Host-side only: one harness edited, one document, one index row. No boot, no build, no device, no
`fastboot`, no `adb`, **nothing written to storage**; the arm and both parks were hashed in place and
never modified. **TWRP stays withheld.**

## 1. The two other defects the same run measured

Both are in the capability 620 added (`RUNNER_OVERRIDE`), and both were found by *using* it rather than
reading it.

**(a) A relative `RUNNER_OVERRIDE` breaks section C.** The path-argument section runs the runner from a
**different directory on purpose** (`cd "$dir" && bash "$RUNNER"`), so a relative runner path is
resolved against that directory and all four of its cells report `bash: <path>: No such file or
directory` - exit 127, four red cells. The live path is absolute, so the default never showed it; the
override is what made it reachable. The failure mode is the dangerous one: a mutation measured with a
relative path reports **section C broken** rather than reporting what it was measuring, and a reader
who did not know the harness would read that as a defect in the runner. Fixed by resolving `RUNNER`
once with `readlink -f` at parse time, so every section fires the same file - 615's defect (a path is
the caller's or the tool's) arriving in the harness that exists to catch it.

**(b) An unrecognised state marker was silently ignored.** The marker `case` had no `*)` default, so a
typo in a cell's marker list--or a marker renamed in one place and not the other--was dropped, and the
cell then ran in a **different state than the one its label names while still printing `ok`.** That is
m639's shape (a cell that cannot reach the branch its label names) in the harness's own wiring, and a
green table cannot show it. Found by writing exactly such a typo into the new `fastboot-plan-line` row
and noticing that nothing objected. The default is now a refusal that names the marker and the cell.

## 2. The cell, and why it carries no marker

```sh
run_state fastboot-plan-line  0 "the device is already in fastboot, so this run issues no adb" \
                                'forbid:sudo adb -s 4a2fe00b reboot bootloader'
```

It passes **no state marker at all**, because the section's default state already *is* the press's:
fastboot lists the serial and adb lists nothing. Its `forbid:` half makes it the converse of
`adb-plan-line` rather than a second assertion of the same line - so the pair now reads: on the adb path
the adb line is present, on the fastboot path it is absent and the fastboot line is present. A `case`
that printed nothing would fail the row's positive half; a `case` that printed the wrong arm would fail
one half or the other.

## 3. The falsifications

| run | harness | runner | result |
| --- | --- | --- | --- |
| the defect | the harness **before** this step | no-plan-line mutant | A **19 ok / 0 failed**, B **15 ok / 0 failed** - nothing red |
| the repair | the harness **after** | same mutant | A **19 ok / 1 failed**: `fastboot-plan-line` on *"did not say (either stream): the device is already in fastboot, so this run issues no adb"*, and its `forbid:` half green (the adb line stays absent), which is what makes it the converse of `adb-plan-line` rather than a restatement of it |
| the baseline | the harness after | the real runner | **20 ok / 0 failed** in A (19 + the new cell), **15 ok / 0 failed** in B, **4 ok / 0 failed** in C, exit 0 |

The first two rows are the pair that matters: **the same mutant, one harness apart, and the difference
is the cell.** The third is the row that keeps the new expectation honest - an expectation added for a
mutant and never run against the real artifact would be a cell asserting the mutant's own behaviour.

**The counts are section-scoped**, and in the repair row that is visible rather than incidental: a
section whose cells fail stops the harness before the next one, so sections B and C did not run under
the mutant and the `19 ok / 1 failed` line cannot be quoted as "everything else stayed green". That was
measured once, in the 20/0 baseline, not twice here. The old harness run is section-complete only
because nothing failed in it.

**The mutant was written to `stages/stage90/.r628-mut.sh`**, which is the shared checkout - the peer
session noticed it and flagged it as an uncommitted file with no owner, which is correct behaviour for
them and a note for this step: the batch's `EXIT` trap removes it, and the flag came from a window in
which the batch was still running. Named here so the next reader knows the name and that it is
transient.

## 4. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still the sleeper arm, still **UNRUN**, and the press is still the user's.
* **It does not change the runner.** The runner's own bytes are `8aad5d12…` before and after, asserted
  at every step of both runs; the mutant lived only in the copy.
* **It does not find any other unasserted branch.** It finds *this* one, by falsifying a branch rather
  than by auditing the table - and the audit method that would generalise ("every cell that only
  forbids has a partner that asserts") is stated here and not mechanised. A cell whose only expectation
  is a forbidding one is now refused **in the reading section** (626's rule for `reader_state`); the
  live-path section still allows them, which is the gap this step leaves behind.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus; the only
  event that can move the goal is the user's **Vol-Down + Power**.

## 5. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One file
edited (`tools/rehearse_live_path.sh` - the absolute `RUNNER` resolution, the marker refusal, and the
new cell). Both batteries ran with the runner stubbed (`sudo`, `adb` and `fastboot` resolved into the
harness's own stub directory, asserted before any state runs), and the live runner's sha was asserted
unchanged before and after each. `fastboot boot` only - never `flash` - so no outcome of any of this
can write to storage.
