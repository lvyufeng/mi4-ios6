# 620: the call that spends the press had no guard, and the status it passed through was invisible

`run_and_capture.sh` pins the serial on every device call and guards every one of them - except the
one that boots the payload:

```sh
  sudo fastboot boot -s "$SERIAL" "$IMAGE"       # section 3, the last thing it does
```

616 pinned *this* call to the serial, 618 guarded the *list* it acts on, 619 guarded the `adb` call a
few lines above it - and this line, the one that spends the press, was left bare. Under `set -euo
pipefail` a non-zero `fastboot boot` therefore ends the script with **fastboot's own status**: no
sentence from this file, and - because the gate's census counts `exit N` literals and nothing else -
a code the gate could not see either. That is 584's defect ("a tool that could not read an artifact
passed its own status out as the gate's verdict") arriving in the runner, on the call that matters
most.

**The repair is deliberately not 619's, and that is the finding.** There, `|| die` was right: a failed
`adb reboot bootloader` means the boot did not happen. Here a non-zero status does not establish that,
because `fastboot boot` **sends the image and then waits for the device to acknowledge** - a non-zero
status can come from either side of the send, and which side is **unmeasured**. Aborting is therefore a
*claim* ("it failed, so nothing was sent"), and if the claim is wrong the abort discards sections 4 and
5, which are the only things that produce a reading, after which the payload's log dies at the phone's
next power cycle: **the press is spent and the reading is lost.** So the status is captured as a value,
the wait and the capture run either way, and the *non-return verdict* is conditioned on it.

Host-side only: one script, one rehearsal, one document, one index row. No boot, no build, no device,
no `fastboot`, no `adb`, **nothing written to storage**; the arm and both parks were hashed and never
modified. **TWRP stays withheld.**

## 1. The measurement: the guard that was missing, and the number nobody had

Every other device-touching call in the file carries a guard, and the pattern is 616-619's:

| site | guard | what a failure means |
| --- | --- | --- |
| `sections/stage90/run_and_capture.sh` `sudo adb -s "$SERIAL" reboot bootloader` | `\|\| die` (619) | the boot did not happen |
| `sudo fastboot devices \| grep -q "^$SERIAL"` (presence) | `\|\| die` | the device is not there |
| the ambiguity guard (`fastboot_pinned_only`) | `die` (618) | the list is not this phone alone |
| **`sudo fastboot boot -s "$SERIAL" "$IMAGE"`** | **none** | *unmeasured* |

The consequence is not a wrong message, it is a **missing section**: `set -e` ends the script at
section 3, so section 4's wait and section 5's capture never run. Section 5's own comment says what
that costs - *"the payload's log is in the top of DRAM and survives until the phone's next power
cycle"* - so a run that aborts here has spent the press and thrown away the reading it was spent for.

## 2. Why `|| die` would have been the wrong repair, and the two sides of the send

`fastboot boot` is not `adb reboot bootloader`. It **sends** the image to the device and then **waits
for the acknowledge**, so a non-zero status has two possible origins:

* **the send side** - the file could not be read, or the serial is not on the bus. Nothing was booted,
  no return can come, and the run genuinely has no verdict about the payload.
* **the acknowledge side** - the image was sent, the payload is running, and the status is about the
  handshake. Here a run that aborted on the status would exit non-zero and **never take the reading**.

**This project has never observed either.** `fastboot boot` has succeeded on every run this phase
records, so the number, the message and which side it comes from are all unmeasured - and that is
exactly why the code must not decide the question for itself. `die` on the status would be 618a's
defect (an assumption stated as a fact); ignoring the status entirely would let a run report a normal
verdict on a boot that never happened.

So the status becomes a **value** - `FB_BOOT_RC`, declared beside `RETURN_TIMEOUT` so that section 4's
verdict can read it under `set -u` and the call site is the only thing that assigns it - the run says
what it was and carries on:

```sh
  sudo fastboot boot -s "$SERIAL" "$IMAGE" || FB_BOOT_RC=$?
  if (( FB_BOOT_RC != 0 )); then
    say "note: fastboot boot exited $FB_BOOT_RC, and **that is not a verdict about the payload**."
    ...
```

The `||` is what keeps `set -e` from ending the script, and the assignment is the whole difference
between a foreign status and a recorded one.

## 3. The verdict is conditioned, not the abort

Section 4 ends in three states, and the last two both return **exit 2** - whose definition in the
file's header is *"the payload ran and the device did NOT come back"*, the one code that spends the
device by telling the operator to press power. A boot call that did not report success cannot support
that claim: the image may never have been sent, so the operator would be sent to press power on a
phone that is probably still sitting in fastboot with nothing booted.

The new block therefore sits **after** the two return branches (a return outranks it - if the device
came back, the payload ran and the status was about the acknowledge) and **after** the UNREAD test
(exit 1 already, with a finer thing to say), and **before** both exit-2 tests, which it overrides:

```sh
  if (( FB_BOOT_RC != 0 )); then
    say "The device did not come back, AND fastboot boot itself did not report success (exit $FB_BOOT_RC)."
    say "**Those two facts together are not the exit-2 state.** ..."
    die "fastboot boot exited $FB_BOOT_RC and no return was seen, so this run has no verdict ..."
  fi
```

The code is **exit 1** - this file's own code for "a host-side failure that says nothing about what
the payload would have done", the reading 511/512's unwritable `$LOGFILE` and the UNREAD case get -
and the message's last lines tell the operator the check that distinguishes the two sides of the send
without a re-run: `sudo fastboot devices`, because **if the phone is still listed, the press was not
spent**. The header's exit-1 clause gained this producer.

It is spelled `die` and not `exit 1`, which is this file's idiom for that code (the UNREAD site above
uses it), and **the choice is load-bearing for a reason that has nothing to do with the shell** - see
§4.

## 4. The gate's census could not see `die`, and the number it printed was invariant to the change

`preflight_boot_check.sh` reads a run's exit vocabulary out of the runner and refuses a code it cannot
narrate. Its census pattern was

```sh
CODES=$(grep -oE '^[[:space:]]*exit [0-9]+' <<<"$REGION" | awk '{print $2}' | ...)
```

and `run_and_capture.sh:137` defines `die() { printf 'run_and_capture: %s\n' "$*" >&2; exit 1; }`. A
`die` call carries **no `exit N` on the line**, so the census could not see it - and section 4 has
contained one since 2026-09-22 (the UNREAD case, whose own message ends *"exit 1, not 2"*), while the
gate printed `distinct code(s) 2 3` and the narration below it said *"Only the code whose own message
says the device did not come back owes a power press."* Section 4 could return **1** the whole time,
and 1 is the code whose message says **do not** press.

The census was written by 584 to close exactly this hole one level up (`set -e` passing a foreign
status through), and it reopened it with the one pattern that misses the file's other exit-1 producer.
Measured by the clause's owner, extracting `RUNNER=...` through the clause verbatim from each revision:

| gate | runner | printed |
| --- | --- | --- |
| old | 619 | `3 exit site(s), code(s) 2 3` |
| old | **620** | `3 exit site(s), code(s) 2 3` |
| new | 619 | `4 exit site(s), code(s) 1 2 3` |
| new | **620** | `5 exit site(s), code(s) 1 2 3` |

The old-gate column is the defect and it is exact: **the operator-facing number is invariant to the
section gaining a state.** The repair (the gate owner's step 621, `90468a3`) counts both spellings,
reads `die`'s code out of its own definition rather than assuming 1 (520's rule - a fixture whose
`die` exits 7 makes the listing print `die -> exit 7` and then refuses, which is the drift alarm), and
puts 1 in `READ_CODES` with a narration sentence naming both producers. Reverting that one token makes
the gate refuse a code its own narration has just explained, which is why 1 had to go in.

**And the refusal fired on the wrong half of my own change, which is how the defect was found.** This
step's first spelling of the new verdict was a literal `exit 1` in section 4; the gate refused it with
*"read section 4 and update this gate before spending a boot on a run whose status it cannot narrate"*
- exactly the intended behaviour. Changed to `die` for the file's-idiom reason above, the gate went
**green**: not because section 4's vocabulary had become `2 3`, but because the census had stopped
seeing it. A green gate whose greenness comes from the reader is not a green gate, which is why this
half is written down here and was reported to the gate's owner rather than left as a passing run.

## 5. A companion on the same path: the plan line was false where the press lands

Section 3's first output line was unconditional:

```sh
say "sudo adb -s $SERIAL reboot bootloader   # then fastboot boot, never flash"
```

and the `adb` call it names is skipped whenever `MODE == fastboot`, because the device is already
where it needs to be. The press this phase is waiting on **always** takes that path - Vol-Down +
Power lands in fastboot and adb lists nothing - so the run's own record named a command it never
issued, in the normal case and not a corner. It is now selected by mode:

```sh
case ${MODE:-} in
  fastboot) say "fastboot boot -s $SERIAL $IMAGE"
            say "  (the device is already in fastboot, so this run issues no adb command at all)" ;;
  *)        say "sudo adb -s $SERIAL reboot bootloader   # then fastboot boot, never flash" ;;
esac
```

`${MODE:-}` because the dry run never sets it and `set -u` is on. This is 520's rule one layer out: a
`say` line describing an action the run does not take is the same defect as one describing an image
that does not exist.

## 6. The cells: three, because the boot call had none

The battery had cells for every **pre**-boot refusal and none for the call itself. `tools/rehearse_live_path.sh`
gained `boot_fail_rc:N` (the send side: nothing booted, so no return can appear, status N) and
`boot_sent_rc:N` (the acknowledge side: the payload **is** running and the status is still N - the
side that makes the abort cost the reading), and `FORBID` became an **array**, because
`boot-call-fails` has to forbid two different sentences at once and a single-value `FORBID` would have
kept only the last one, silently.

| cell | what it asserts |
| --- | --- |
| `boot-call-fails` | send-side status 1 and no return → **exit 1**, the message saying the status is not a verdict, and **not** `It needs a power press` (exit 2's action) nor the `adb` plan line (a path that issues no adb command) |
| `boot-call-fails-after-send` | acknowledge-side status 1 with the return appearing on its own → **exit 0** and the status recorded: the reading was taken, which is precisely what the abort destroyed |
| `adb-plan-line` | the presence half of the plan-line pair: on the adb path the line **is** the plan, so it must be printed |

Battery **33 → 36 ok / 0 failed** (19 live-path + 13 reading + 4 path), exit 0, on the exact bytes
committed.

| mutation | result |
| --- | --- |
| the boot call back to bare, nothing else | live path **17 ok / 2 failed** - `boot-call-fails` on *"did not say: did not report success"* **and** `boot-call-fails-after-send` on *"exit 1, promised 0"* plus *"did not say: fastboot boot exited"*. Both new boot cells, nothing else |
| the `case` back to the unconditional `say` | live path **18 ok / 1 failed** - `boot-call-fails` on *"said what this state must NOT say: `sudo adb -s 4a2fe00b reboot bootloader`"*, with `adb-plan-line` still green, so the plan-line pair is a pair |
| the `FB_BOOT_RC != 0` block deleted | live path **18 ok / 1 failed** - `boot-call-fails` on *"exit 2, promised 1"*, on *"did not say: did not report success"*, **and** on *"said what this state must NOT say: It needs a power press"*. That last one is the false hang reproduced rather than described, in the operator's own words |

Each measured once, on a **copy** (see the paragraph below) with the live runner verified
`e742ba4d…` after every step. **The three counts are section A's, and sections B and C did not run in
them**: the harness stops a section whose cells fail, before the next one. So the mutation runs show
*which* cells moved and cannot be quoted as "the remaining cells stayed green" - that was measured
once, in the 36/0 baseline, not three times here.

**And the falsification harness had a defect of its own, found by the gate session, and it is the same
rule as m636 one layer out.** m636 says rename-never-in-place, because bash reads a running script by
fd and an in-place edit can make the live process execute at a shifted offset - which protects a
process already **reading** a file. Nothing protected a file a live process **fires by path**: the
press-watcher does `cd stages/stage90` and runs `./run_and_capture.sh`, and neither it nor the runner
holds a lock (`grep -c flock` = 0 in both). So the first version of this batch, which mutated the live
runner in place three times, described a safety property backwards: *"the runner is restored
byte-identically afterwards"* is a statement about the **after** and says nothing about the **during**,
which is the interval a press can land in. Two ways that loses the press - a mutant that `die`s before
`fastboot boot` (a press spent on nothing) and a mutant that boots and classifies wrongly (a press
spent on a reading of the mutation, which is worse because it produces a plausible log). The run was
stopped mid-mutation and its output discarded.

The repair is in the harness and it is a refusal rather than a convention: `rehearse_live_path.sh`
takes `RUNNER=${RUNNER_OVERRIDE:-$LIVE_RUNNER}` with the live path named separately, **and refuses if
the override resolves to the live runner** (`readlink -f` on both); the batch mutates a copy at
`stages/stage90/.r620-mutN.sh`; and `mutate_620.py` refuses any path ending in `/run_and_capture.sh`.
The copy must sit in that directory and not `/tmp`, because the runner is `cd "$(dirname "$0")"` and
derives `STAGE_DIR`/`REPO_ROOT`/`OUT` from `$0` - measured by running the copy's own dry run, which
prints `image: .../out/stage90/stage90-qcdt.img` and exits 0, so the gate and `out/` both still resolve
with the copy present. It cannot collide with the press either: the gate's freshness scan is `-type f`
with a `-name '*.c' -o '*.h' -o '*.S' -o '*.ld'` list (`preflight_boot_check.sh:177-179`), so a `.sh`
there is not scanned, and the catcher fires the name `run_and_capture.sh`, never the copy. The batch
asserts the live runner's sha after every step and removes the copies on `EXIT`.

**One earlier run of this battery is discarded, and the reason belongs here.** A run launched while
the gate's owner was editing `preflight_boot_check.sh` read the file **mid-edit**: it saw the new
listing (`distinct code(s) 1 2 3`) and the old `READ_CODES="2 3"`, so the gate refused the runner and
`no-return` failed with *"exit 1, promised 2"* - a state that is green on the frozen bytes. The tree
was shared and the file was being written; no output of that run is used. The numbers above are from a
run on a tree whose gate had stopped changing.

## 7. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…` (8,540,160 bytes), the entry still `696a0f39…`,
  the arm is still the sleeper, and the press is still the user's.
* **It does not measure `fastboot boot`'s failure status or which side of the send it comes from.** It
  makes the runner independent of the answer, which is the only thing this file can do about it. A run
  that reports exit 1 with a "exited N" note is where that measurement would come from, and none is
  available yet.
* **It does not fix the gate's remaining gap, which the gate's owner wrote into the gate**: the census
  checks the *code set* against `READ_CODES`, not the *count of producers within one code*. A third
  exit-1 producer in section 4 would change the narration sentence and move nothing.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus; `adb
  devices` and `fastboot devices` are both empty; the only phone-class device on the host is the
  neighbour on `usb 3-3`. The one event that can move the goal is the user's **Vol-Down + Power**, and
  what this step changes is what the run *after* it is allowed to conclude when the boot call itself
  does not report success.

## 8. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One
script edited (`stages/stage90/run_and_capture.sh` - `FB_BOOT_RC` and its declaration, the guarded
`fastboot boot` and its note, the conditioned non-return block, the mode-selected plan line, and the
header's exit-1 clause), one rehearsal extended (`tools/rehearse_live_path.sh` - `boot_fail_rc:N`,
`boot_sent_rc:N`, `FORBID` as an array, three cells, **and `RUNNER_OVERRIDE` with its refusal**, which
is the finding §6 records and the peer session's) with the gate owner's step 621
(`90468a3`) in `preflight_boot_check.sh` as its gate half. **The gate reading below is against the
gate as it stood at measurement, `128b66a` (`701700d7…`)** - later gate steps (622's arm block, 639's
console bound) are narration and do not touch the census this step is about; naming the revision is
the point, because a line count quoted without one is a claim about a moving file. The gate was
re-run -> **EXIT=0 / 610 lines / 0 stderr**;
the runner's own `--dry-run --allow-xnu-entry` -> **exit 0 / 0 stderr**; `rehearse_live_path.sh` ->
**36 ok / 0 failed**, exit 0. **All three mutations were measured on copies with the live runner
verified `e742ba4d…` after every step**, so the path a press fires was never the mutant at any
instant - not merely restored afterwards (§6). Both parks verify **exit 0 / 11 files** against their
recorded sets, hashed in place with no file modified; the arm is `60063c47…` in the live tree and in
the park by two routes. `fastboot boot` only - never `flash` - so no outcome of any of this can write
to storage.
