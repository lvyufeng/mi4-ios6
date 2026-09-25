# 667: the park is found from the bytes, and the arm is joined to the payload record

666 section 7 measured three stale rows in `tools/verify_press_ready.sh` and recorded them without
repairing them. This step repairs all three, and the reason it is a step rather than a tidy-up is that
none of the three was a true statement about the tree: one refused a good tree by comparing it with a
historical arm, one refused it while narrating a *different* line as the reason, and one was **green
while naming the wrong arm**. The tool is the only thing that says the one press will not be wasted, so
a red row on a good tree buys nothing and a green row on the wrong arm buys the wrong press.

Nothing was fired, nothing was sent anywhere, and no byte under `out/` was written: this is a host-side
change to a host-only tool (`preflight_boot_check.sh` never runs fastboot and never touches the device -
634's measurement, by stubbing `sudo`/`adb`/`fastboot` and getting byte-identical output).

## 1. The three defects, each as a reading and not as a description

Measured 2026-09-25 against `out/stage90` holding the recorded set **`armed-selftest-wdog-ef0361a2`**
(11 files, the gate itself printing *"the bytes in /mnt/data/mi4-ios6/out/stage90 are the recorded set
'armed-selftest-wdog-ef0361a2', file for file"*).

**(a) The default park named a historical arm, so row 1 refused the recorded arm.** `PARK` and `SET`
defaulted to `armed-sleepless-696a0f39` - the sleeper arm, live two steps before this file's last edit.
Run with no arguments, `live arm is the recorded arm` went red listing **ten members** as *"the LIVE
file(s) are not the recorded bytes"*, with `stage90-qcdt.img` live `ef0361a2…` against record
`60063c47…`. Every one of those ten was, in fact, the recorded bytes of the arm in `out/`. The refusal
was about the default.

**(b) Row 3 could not be made green, and its refusal text was a line that did not contain a refusal.**
The row invoked the gate as `"$GATE" --allow-xnu-entry`. On this arm the gate refuses that set -
`REFUSING: the hardware-watchdog SELFTEST skips the normal boot path and spins until a net reboots it;
needs --allow-hw-watchdog-selftest` (`preflight_boot_check.sh:1570`) - so the row printed `exit 1`. Its
*detail* line was built by `grep -m1 -E 'REFUS|source newer|not |no '`, which matched the gate's
**passing** line `no source file is newer than the image` and printed that as the reason. A FAIL row
whose text is a line that is not the refusal is worse than a FAIL row with no text, because it reads as
a reason.

**(c) The arm row was green on the wrong arm.** The row's two inputs are the extractor's reachability
sentence and the *entry* record (`xnu_arm_entry-config.txt`). Both are **identical** between 653's
acting arm and 666's self-test arm - the self-test arm carries the acting arm's entry byte for byte,
`a43304f2…` - so the row printed *"the ACTING arm (653: …)"* with the acting arm's whole
pre-registered reading attached. Measured as the thing that separates them:

```
$ diff out/stage90/frozen/armed-seam-poc-a43304f2/stage90-build-config.txt \
       out/stage90/stage90-build-config.txt
16c16
< #define STAGE90_HW_WATCHDOG_SELFTEST 0u
---
> #define STAGE90_HW_WATCHDOG_SELFTEST 1
$ diff out/stage90/frozen/armed-sleepless-696a0f39/stage90-build-config.txt \
       out/stage90/frozen/armed-seam-poc-a43304f2/stage90-build-config.txt
(no output)
```

One line of the **payload's** record, and two byte-identical payload records for the two arms 646 knew
about. That is why 646's sentence - *"the payload's record ... is byte-identical ... because the
separating switch is an entry switch"* - was true and is still true, and why it was the wrong thing to
conclude from: it is a fact about those two arms, not about the payload record.

## 2. The repair

**(a) The park is found, not remembered.** With no `--set`, row 1's block hashes the live
`stage90-qcdt.img` - the one file `fastboot boot` sends - and asks the record which set records that
hash. A set that two sets record, and a payload that no set records, are both refusals that name what
they found; the park's directory then follows the set's name. The two alternatives were tried and are
worse. A default name is a name nobody typed. And the record's own `role=` text cannot answer it:
**three sets** in `revert-set.txt` claim in their `role=` to be *"the arm the next press sends"*
(`armed-sleepless-696a0f39` at line 126, `armed-seam-poc-a43304f2` at 137, `armed-selftest-wdog-ef0361a2`
at 200 - measured, each grep'd out of the file). A claim in a record is not a reading, so the bytes are
asked instead. `--set`/`--park` remain, as the way to ask about an arm that is not the one in `out/` -
and an explicit `--set` naming a different arm does **not** make a green row: run with
`--set armed-seam-poc-a43304f2` it refuses on the bytes (`stage90-qcdt.img: live ef0361a2…, record
a5997bae…`).

**(b) The flag set is derived from the arm's own switches, and printed as the command to type.** The
`--allow-*` flags are a function of the payload's record: the gate refuses each hazard without its flag
(`:1570`, `:1592`, `:1605`, `:1649`, `:1684`, `:1948`, `:1954`, `:1975`, `:1985`) and refuses
`--allow-xnu-entry` on a ladder image the other way round (`:1680`). So the row derives a flag list from
`stage90-build-config.txt` - one switch-to-flag table - invokes the gate with exactly that list, and the
header prints it as the two commands the operator types:

```
  gate flags  --allow-xnu-entry --allow-hw-watchdog-selftest
  the run     ./preflight_boot_check.sh --allow-xnu-entry --allow-hw-watchdog-selftest   then   ./run_and_capture.sh --allow-xnu-entry --allow-hw-watchdog-selftest
```

That is the point of doing it here rather than in the operator's memory: the runner passes unrecognised
arguments straight to the gate (`run_and_capture.sh:142`), so the flags the gate is checked with *here*
and the flags the run is given *there* are one value with one definition.

The table is a **copy** of the gate's conditions, which is a real cost, and two checks bound it:

* the gate's own flag vocabulary is read out of the gate - its usage line and every refusal that names a
  flag (`grep -oE 'needs --allow-[a-z-]+|without --allow-[a-z-]+'`) - and **any flag this table cannot
  produce is a refusal that names it**. An eleventh flag therefore turns into "I cannot derive this",
  not into a silently under-supplied set;
* the derivation is falsifiable with `--gate-flags`, so a green gate row is not the only thing this code
  can print.

The residual risk is stated in the code rather than papered over: the gate could give an existing
`--allow-X` a new meaning while keeping its name, and no check here would see it. Under-supply is always
loud (the gate refuses); over-supply requires the arm's own record to name the hazard, which is the same
act that built the arm.

**(c) The arm is the ELF reading joined to the payload's record.** The entry reading still names the
entry, and the join then decides the arm. On a self-test arm the row now says so and says what that
means: the payload spins **before** the handoff (`stage90_main.c:1224-1249`, after the DT build), the
spin returns only by rebooting, so there is no `xnu_live_*` key in the log at all and the seam pair,
638 section 3's `a1`/`b1` table and 642's `sleh_pc` join are **unread** on this arm. The arm's own keys
are the `hw_watchdog_*` block the payload logs before it spins - and that block is the *precondition*:
`hw_watchdog_enabled=0` says in the log that the press tested no hardware net. The question is the time
to return, 25.0 s bark / 28.0 s bite against 90.0 s for `STAGE90_SELFTEST_DEADLINE_US`, with the
deadline line's absence **not** being the verdict (it can also mean the log was read before 90 s had
elapsed, since the deadline is measured from the spin's start).

The join is required, not best-effort: if that record cannot be read, or a switch the join reads is not
named exactly once with a value, rows 3 and 4 refuse instead of falling back to the entry reading -
because the fallback is the wrong answer, and it is the one that was green.

**(d) One reading, taken once.** Three rows and the header need the payload's record, so it is parsed
once into `payload_sw`/`payload_sw_n`; the value is extracted with a character-for-character copy of the
gate's own `value_of` `sub()` (`preflight_boot_check.sh:132-135`), because a hand-rolled second parse of
the same line would be a second definition of one value. The record's `set= sha256= bytes= file=` lines
are parsed by `kv_of`, hoisted out of row 1 where it was defined inside the loop - the same function is
what the new set-finding block reads with, and two copies of it would be this project's most repeated
defect one file over. `kv_of` also does its word-split under `set -f`, so a `*` in a role text cannot be
pathname-expanded against the current directory.

## 3. The falsifications

Every new branch was run, because a branch that has only ever been seen to work is not a reading.

| what was falsified | how | what it printed |
| --- | --- | --- |
| the set is found from the bytes | a scratch tree with a **truncated copy** of the qcdt, built by `head -c 4000000` **reading** the original | row 1: *"the live `stage90-qcdt.img` (8ee56311…) is not the `stage90-qcdt.img` of any set in … (frozen-574 armed-sleepless-696a0f39 armed-seam-poc-a43304f2 armed-selftest-wdog-ef0361a2)"*, and row 2 refuses because there is no park |
| the payload record is required | the same scratch tree with `stage90-build-config.txt` removed | rows 3 and 4 both refuse, each naming the record: *"… is not readable, so the payload's own switches cannot be read"* |
| a switch must be named once, with a value | a copy with a duplicate `STAGE90_HW_WATCHDOG_SELFTEST` line **and** a bare `#define STAGE90_CACHE_MODE` | rows 3 and 4: *"`STAGE90_HW_WATCHDOG_SELFTEST` is named 2 time(s) … `STAGE90_CACHE_MODE` is named with no value after it"* |
| the derived set is what makes row 3 green | `--gate-flags '--allow-xnu-entry'` - the old constant, through the seam | row 3 red: *"exit 1 under '--allow-xnu-entry' (the --gate-flags seam, NOT the derivation) - REFUSING: the hardware-watchdog SELFTEST skips the normal boot path and spins until a net reboots it; needs --allow-hw-watchdog-selftest"* - the defect reproduced on demand |
| the seam is not a red-only knob | `--gate-flags '<the derived set>'` | row 3 green; the run is 4 of 5, the fifth being the device read |
| a flag the gate does not know | `--gate-flags '--allow-bogus'` | row 3: *"exit 2 … - unknown argument: --allow-bogus"* |
| the vocabulary check | a fake gate whose usage line names `--allow-newthing` | row 3: *"the gate names flag(s) this file has no switch for: --allow-newthing …"* |
| an explicit `--set` for a different arm | `--set armed-seam-poc-a43304f2` | row 1 refuses on the bytes, as above |
| the live tree, unmodified | the tool with no arguments | 4 of 5 green: rows 1-3 ok, row 4 naming **"the HARDWARE-WATCHDOG SELFTEST arm (666: `STAGE90_HW_WATCHDOG_SELFTEST=1`), and the entry it carries is the ACTING arm (653 …)"**, row 5 red on the physical state |

**One defect this step made itself, kept because the shape is the lesson.** The new consequence strings
are assigned with `conseq="…"`, and a backtick inside a double-quoted string is a **command
substitution**: the first draft printed twelve `… : command not found` lines and a detail line with the
identifiers removed (`there is no  key in it at all, so 638 section 3's / pair table`). The file's older
strings never had a backtick for exactly this reason, and the rule is the one this project keeps
re-learning from the other side: a string that is *printed* is still *parsed*. A second, smaller one:
the header printed `(set armed-selftest-wdog-ef0361a2 - )` because the found case had no reason string -
a silence in the successful direction (630), now filled.

## 4. What row 5 is still saying, and what blocks the press

The only red row on the live tree is the device row, and it is a true reading: `fastboot devices` and
`adb devices` both name nothing, so a press fired now would not be caught. The blockers are unchanged
from 666 section 7 and none of them is a file: the neighbour `33e80afe` off the bus (the ambiguity guard
refuses at 60 s and boots nothing), a **fresh arming** of the catcher (none is alive since 652 spent the
cover), and the operator's press. What *has* changed is that rows 1-4 now pass on the arm that is
actually in `out/`, so the operator's verdict is about the press and not about the tool.

R4 (654 section 6: pinning the arm's identity around the gate call in `run_and_capture.sh`) still waits
for a window with no watcher.

## 5. A finding in the peer's file, reported and not edited

`preflight_boot_check.sh`'s dead-man self-test guard is `if [[ $SELFTEST == "1u" ]]` (`:1605`), and the
payload's switch record is produced by the preprocessor (`build.sh:395-397`, `$CC … -E -dM`), which
prints a command-line definition **verbatim**: the live record reads `#define STAGE90_HW_WATCHDOG_SELFTEST
1`, with no `u`, for an arm built with `-DSTAGE90_HW_WATCHDOG_SELFTEST=1`. Every documented build form
for the dead-man arm is the same shape - `-DSTAGE90_DEADMAN_SELFTEST=1` (`README.md:162`,
`docs/status/roadmap.md:424`, experiment-93, experiment-94, `docs/reference/recovery-and-rollback.md:277`)
- so a record reading `1` would **not** trip that guard, and the gate would pass a payload that hangs on
purpose *without* requiring `--allow-selftest`. The documented form pairs it with
`-DSTAGE90_HW_WATCHDOG=0`, which removes the second net as well. The watchdog self-test's own guard is one
line up and takes both spellings (`case "$HWSELFTEST" in 1|1u)`, `:1569`), with no branch for any other
value - so a third spelling would build the self-test (the C `#if` is true) and skip the refusal.

This is the peer lane's file, so it goes to `run-experiment-526` as a message and is recorded here rather
than edited. My side of it cannot be silent: the tool refuses an unrecognised spelling of any switch it
reads (exactly once, with a value), and `--gate-flags` exists so the flag set can be falsified.

## 6. Safety

No device action of any kind: no gate-by-proxy, no boot, no `fastboot`/`adb` beyond the two device *list*
reads row 5 has always made. No byte under `out/` was written - the falsification trees live outside the
repository, under `/mnt/data/scratch-verify-press-ready/`, and the truncated payload there was produced
by **reading** the live qcdt through `head -c` into a new file, never by an in-place write to it. The
sole `--gate` invocation remains host-only. `fastboot boot` only, never `flash`; nothing written to
storage; the neighbour's serial `33e80afe` was not touched and does not appear in any command run here.
**TWRP-to-storage stays withheld**: 「如果os已经能进去了的话」 is still unmet - the OS is not observed
booting, and this step does not advance that.

## 7. Files

* `tools/verify_press_ready.sh` - the three repairs, the derived flag set, the header and usage, and the
  falsification seam `--gate-flags`.
* `docs/experiments/README.md` - this step's row, placed immediately after 666's. **The placement was
  measured, not guessed**: `tools/check_experiment_index.py` reports stage90 as strictly unimodal
  (ascending to one peak, descending after), and a new row is the new maximum, so where it goes decides
  whether the prefix stays ascending. After 665's row the count went **6 → 7**; after 666's row it stays
  **6 → 6**. The six are pre-existing, in a historical block at lines 504-513 (`568,567`, `578,577,576,
  574,572,569`), and are not this step's to rewrite - a listing is not a check of order (defects 138
  and 139, the checker's own docstring), and the checker's exit is 1 on the committed index too.
* No source under `stages/stage90/` was touched: the arm in `out/` is unchanged, its park is unchanged,
  and the gate is unchanged.
