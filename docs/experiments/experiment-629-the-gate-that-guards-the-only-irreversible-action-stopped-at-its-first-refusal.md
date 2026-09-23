# 629: the gate that guards the only irreversible action stopped at its first refusal

`stages/stage90/preflight_storage_write.sh` is the gate on the first PERSISTENT write this project
would ever make - the step the user's condition names (「如果os已经能进去了的话，可以twrp写入到存储里了」).
Every device touch in this tree so far has been `fastboot boot`, which writes nothing to storage, and
that is the whole reason a brick has been impossible by construction. This file is where that ends.

It was written as **a chain of `refuse` calls**: six preconditions, each checked in order, each one
`refuse`-ing and exiting. That shape has a property nobody had measured: **the checks after the first
failure never run.** And in the state the phone is actually in, the failing check is the *third* - the
evidence that the OS stayed up - so checks 4, 5 and 6 (the image exists and is hashed, the rollback
verifies against its own manifest, the tool image is *the* image this tree records) **had never been
evaluated on this host at all**. They would have run for the first time on the day the OS finally stays
up: one booted phone, one spent press, and three checks whose first execution would be in front of the
decision they exist to inform. That is `[[mi4-the-step-after-the-point-of-no-return]]` arriving in the
gate that guards the only action in this project that can brick the device.

**Nothing about the permit changed.** The plan and `CLEARED` are still printed only when every check
passes, and the exit is still 1 when one does not. What changed is that all six now run, each records a
named verdict, and the refusal is the *conjunction* of the verdicts rather than the first one reached.

Host-side only: one script edited, one document, one index row, plus transient mutant copies in the
shared checkout that the batteries' own traps remove (named below). No boot, no build, no device, no
`fastboot`, no `adb`, **nothing written to storage**. The arm and both parks were hashed and never
modified. **TWRP stays withheld.**

## 1. The restructure, and the arithmetic that makes it safe

Six named checks, in the order they always ran:

| # | check | input |
| --- | --- | --- |
| 1 | the target is a low-risk partition | `--target` against `boot`/`recovery` |
| 2 | the criterion strings are still in the runner | two clauses grepped out of `run_and_capture.sh` |
| 3 | the evidence is a capture where the OS stayed up | `--evidence`, read through the runner's own summariser |
| 4 | the image to write exists and is hashed | `--image` |
| 5 | the rollback exists and verifies | the backup directory and its manifest |
| 6 | the tool image is the recorded one and is booted, never flashed | `--boot-image` and `tools/verify_tool_image.sh` |

Every check now records `ok`, `REFUSED` or `not-reached` and the table prints all six. Two properties
are preserved from the chain and one is new:

* **The criterion is not re-derived here.** Checks 2 and 3 still read the *runner's* two clauses, and
  check 2 still refuses if the runner stops printing either one - a phrase nothing prints is not a
  criterion (602's class).
* **The refusal text is the text the chain printed.** Each `REFUSED` row carries the same sentence the
  first-refusal form carried, so a reader who knew this gate's refusals still sees them verbatim.
* **New: a check that could not look is not a check that passed.** The third verdict counts in the
  refusal, not only in the table. Section 3 is why.

## 2. The three defects, and the measurements

All rows below belong to **one frozen revision of the gate**: `88088457c69414a6b53cbdfe71960bf82d203b82ab2c6c4b01acea529c112b16`,
30,191 bytes, 413 lines, `bash -n` clean, hashed before and after the battery and asserted unchanged -
because 626 paid for editing a file while a cell was in flight. The runner was the committed
`8aad5d12…` throughout, and `tools/verify_baseline_readings.sh` is green on it.

### (a) `--help` printed nothing, and then printed not enough

Two defects in one function, and the first one is 615's class exactly.

```
$ bash stages/stage90/preflight_storage_write.sh --help
sed: can't read stages/stage90/preflight_storage_write.sh: No such file or directory
```

The file did `cd "$(dirname "$0")"` at line 64 with `$0` **still relative**, and `usage()` read the file
through `$0`. So from the repository root, `$0` was the *caller's* relative path, the `cd` made the
working directory the *tool's*, and the read looked for
`stages/stage90/stages/stage90/preflight_storage_write.sh`. A path is the caller's or the tool's, and a
`cd` before argument parsing silently makes it the tool's - and the file it happened in is the one whose
first reader's first move is `--help`.

Measured: with an absolute path (where the `sed` works) the output still stopped at **line 20**, inside
precondition 2 - so the `Usage:` block at line 52 and the exit contract at line 60, the two things a
reader asks `--help` for, were past both bounds of `sed -n '2,50p' … | head -20`. Silently: a slice that
stopped early still exits on its own terms. That is 613's pair (a help text pinned to a line number, and
a completeness claim made by a command that cannot see what it missed).

Repaired: `SELF=$(readlink -f "${BASH_SOURCE[0]}")` before the `cd` (628's repair for `RUNNER`, applied
to `$0`), and `usage()` now reads the header **bounded by the file's own comment block** - `awk` until
the first line that is not a comment - instead of by two line numbers. Measured after: **62 identical
lines, md5 `0b54ddf29565…`, from all three invocation forms** (relative from the root, absolute, and
from inside the stage directory), no `sed` error, and the tail present - `Usage:`, the exit contract,
and the new sentence that `--help` also exits 1 because 0 is the only status here that clears a write.

### (b) The refusal condition ignored a check that never ran

The verdict was a conjunction over refusals alone. A `skip` - a check whose *input* was absent, which
the file's own comment calls "neither a pass nor a failure of what it tests: it could not be made to
look" - was printed as `not reached` in the table and **counted for nothing in the exit**. So a run with
zero refusals and one skip printed `all 6 check(s) passed` and exited **0: a cleared persistent write**.

Falsified by mutating **one** skip site on a copy of the real gate - check 5's `elif [[ -z $TARGET ]]`
made unconditional, the rest byte-identical - and running the *same mutant* under the old and the new
condition:

| condition | exit | what it printed |
| --- | --- | --- |
| old, `NFAIL > 0` only | **0** | `all 6 check(s) passed, and none was skipped: 0 refused, 1 not reached.` - **while the same output carries a `skipped` row**, so the sentence is falsified by its own table |
| new, `NFAIL > 0 \|\| NSKIP > 0` | **1** | `REFUSING: 0 of 6 check(s) refused and 1 could not be made to look at all.` |

With the six checks this file has, every skip is *also* accompanied by a refusal (check 2's skip runs
into check 3's summarise; check 5's skip needs `--target` empty, which check 1 refuses), so the clause
changes **no current verdict** - it removes a shape rather than a case, and the mutant is what makes that
a measurement instead of a claim. The passing sentence was also made explicit about its own absence
(`…and none was skipped: 0 refused, 0 not reached`), because a claim of completeness is exactly what a
run that never happened also produces (613).

### (c) A limit, measured and written down: check 5 is consistency, not authenticity

The manifest's entries are **absolute paths** written when the backup was taken, so check 5 answers "do
these bytes match this manifest" and not "is this the phone's factory image". Falsified against a
faithful copy of the backup whose manifest names the copy:

| case | exit | verdict |
| --- | --- | --- |
| the copy untouched | 0 | 6 ok - the anchor: the mutant is *equivalent*, so the rows below measure the check |
| one byte of `boot.img` moved | 1 | check 5 refuses, manifest red |
| the byte moved **and the manifest re-written to match** | **0** | 6 ok - **a self-consistent backup passes** |
| the manifest verifies but `boot.img` is gone | 1 | "the manifest verifies but there is no boot.img in …" |
| no backup directory | 1 | refused by name |

Nothing about hashes could do better - a self-written record is not a constraint, since the record and
its subject would agree - and the third row is the honest statement of what a green check 5 means. What
makes this rollback known-good is the **date it was taken** (in the directory's own name and in the
reference), not this check. The tool image does not have this limit, because its record is
`stages/stage90/tool-images.txt` **plus** TWRP's own detached signature; the rollback has no signature to
appeal to. The limit is now in the file's precondition 2 and in check 5's own ok line, so the operator
reads what the green cell does and does not establish.

## 3. The falsifications, per check, on one revision

Every row is `EXIT / ok / REFUSED / skipped` from the battery. The gate is inert in all of them: with
`sudo`, `fastboot`, `adb`, `dd` and `sh` replaced by recording stubs on `PATH`, the positive path prints
the plan and `CLEARED` and **the recording file is empty - 0 device commands executed**.

| case | EXIT | ok | REFUSED | skip | the check that fired |
| --- | --- | --- | --- | --- | --- |
| positive path, the harness's predicted window as evidence | **0** | 6 | 0 | 0 | none - plan printed, `CLEARED` |
| the phone's real state (no `--evidence`) | 1 | 5 | 1 | 0 | 3 |
| no `--allow-storage-write` | 1 | 0 | 0 | 0 | none - refuses before the table |
| `--evidence` = the real capture **520** (goal clause, no arm clause) | 1 | 5 | 1 | 0 | 3 - "shows the goal's clause but NOT the arm's" |
| `--boot-image` = an existing, unrecorded copy | 1 | 5 | 1 | 0 | 6 - the tree's own record |
| `--boot-image` == `--image` | 1 | 5 | 1 | 0 | 6 - the identity test |
| `--target=modem`, `--target=aboot` | 1 | 5 | 1 | 0 | 1 |
| `--image` missing / absent | 1 | 5 | 1 | 0 | 4 |
| `--boot-image` missing | 1 | 5 | 1 | 0 | 6 |
| `--target` missing | 1 | 4 | 1 | **1** | 1 refuses, 5 records the skip |
| the backup cases of section 2(c) | - | - | - | - | 5 |
| the skip mutant of section 2(b), old condition / new | **0** / 1 | 5 | 0 | 1 | the refusal condition |

The row worth pointing at is **`--target` missing**: it is the one reachable case where a skip appears,
and it shows the third verdict doing what it is for - the check that could not decide is named as not
decided rather than printed as a pass.

## 4. Three of my own rows were wrong, and how

Recorded because the wrong versions were the ones that looked right first.

* **The unrecorded-tool case used the same file for `--boot-image` and `--image`** - so it hit the
  identity branch, not the record branch, and its label named a branch it never reached (m639's shape).
  The exit was 1 either way, which is exactly why reading the output and not the code was the only thing
  that could catch it. Redone with a distinct copy: the record branch fires.
* **Two mutants were made outside the tree**, in `mktemp -d`, and this gate derives `REPO_ROOT` from
  `$(dirname "$0")/../..`. From `/tmp/tmp.XXXX` that is `/`, so `RUNNER` became
  `/tmp/tmp.XXXX/run_and_capture.sh` (exit 127) and the tool verifier became `//tools/verify_tool_image.sh`
  - two extra refusals, and a row that measured my harness's relocation rather than the gate's check. The
  mutants now live at `stages/stage90/.629-mut*.sh`, where the arithmetic is the gate's own, and the
  battery asserts the gate's own hash unchanged and the mutants' removal afterwards.
* **The first backup falsification was measuring the live backup, not the copy.** It copied the backup
  directory and tampered with the copy's `boot.img`, and the `sed` meant to point the copy's manifest at
  the copy matched **nothing**. The reason is the manifest's absolute paths: `sha256sum -c` on the copy's
  manifest hashes the **original** files, so the check was green for both the untouched and the tampered
  copy. That is 611's rule ("never verify a park with `sha256sum -c`, whose absolute paths hash the live
  tree") arriving in my harness one step before the same property became section 2(c)'s finding about the
  gate. Redone with a copy whose manifest names the copy, and with the anchor row (untouched copy → 6 ok)
  that proves the mutant is equivalent.

## 5. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still the sleeper arm (`60063c47…`), still **UNRUN**, and the press is still the user's. The park at
  `/tmp/r594/frozen-payload` verifies against `--set=frozen-574` (11 files, 6 manifest-member checks).
* **It does not change the runner.** `8aad5d12…` before and after, unchanged on disk and in git.
* **It does not make the gate clear anything.** The evidence check reads the *runner's* verdict on a
  capture; the positive path above used the harness's own `predicted` window fixture, which satisfies
  both clauses **by construction** - which is a measured statement about the criterion being
  phrase-based, and not a claim that the OS has stayed up. Nothing in this step did.
* **It does not close the phrase-based-criterion question.** The gate clears on the runner's two
  clauses, and a fixture that carries those phrases satisfies it; the runner's own readings are what
  make the phrases mean something, and check 2 only asserts the phrases still exist.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus; the only
  event that can move the goal is the user's **Vol-Down + Power**.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One file
edited (`stages/stage90/preflight_storage_write.sh`). Every measurement was made with the gate's
`sudo`/`fastboot`/`adb`/`dd` unresolved to anything real - and the one that matters is the stub run:
**0 device commands executed** on the positive path, where the plan and `CLEARED` are printed. The
mutants were transient files in the shared checkout, named here
(`.629-mut.sh`, `.629-mut-gate.sh`, `.629-mut-skip.sh`, `.629-mut-pair.sh`) so a peer who sees one
mid-battery knows whose it is and that its trap removes it; `git status` after the battery shows only
the one intended modification. `fastboot boot` only - never `flash` - so no outcome of any of this can
write to storage.
