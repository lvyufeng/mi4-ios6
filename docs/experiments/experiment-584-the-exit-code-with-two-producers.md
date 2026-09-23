# 584: the exit code with two producers, and the verdict a read that never happened

The gate in `stages/stage90/preflight_boot_check.sh` is the thing that decides whether the next
`fastboot boot` is allowed to happen, so its exit code is read by an operator who was taught a
vocabulary: **`0` green, `1` a refusal printed above, `2` the argument parser and nothing else.** This
change makes that vocabulary true. It was false in a way that the exit code alone could not show, and it
was false for the *reason the gate exists*: a tool that could not read an artifact passed its own status
out as the gate's verdict.

Nothing is built here and no arm is changed: the entry image is byte-identical after the change
(`151425c4…` / `3bc72605…`, the frozen 574 measurement arm), the gate is green on it, and the device
stays where 572 left it - off the bus, waiting on a power press.

## 1. `set -e` made the contract false, and the failure was invisible in the exit code

`fail()` is the gate's only refusal (`:50`, `REFUSING: …` then `exit 1`), and the argument parser's
`exit 2` is the only exit 2 in the file - `grep -nE '^[[:space:]]*(exit|die) [0-9]'` finds exactly those
two literals in the body. So the contract *looked* total. But `set -e` means every **unguarded** command
that fails ends the script with **that command's own status**, and no literal `exit` is involved. An
`awk` that could not open the entry record exits 2 - so a status the operator reads as "you typed the
arguments wrong" was, in the same process, also "I could not read the file that says which arm this
image is".

This is the defect class this project keeps meeting, arriving inside the gate itself. The house rule is
that **a red gate is read by which section refused** - and a bare tool status has no section, so a red
gate whose only line is a tool's error cannot be read at all. It is 573's `UNREAD` line printed inside a
`GATE EXIT=0` run, one layer up: a status that does not carry its own reading. 580 named the same shape
on the runner's side, where `grep -c` prints `0` for input it never read; here the status is passed
through rather than zeroed, and the repair is the same in kind - **make the carrier say which reading it
is.**

## 2. What the gate actually answered an unreadable input, measured (four answers, none of them its own)

`[[ -f ]]` is true for a path this process cannot open - mode 000, another owner, a directory wearing the
file's name - and the *tools* below each guard (`cat`, `sed`, `awk`, `cmp`, `nm`, `sha256sum`, the
entry-blob python, the disassembler) are what actually open it. Nine states were rehearsed against the
committed gate in a scratch tree whose artifacts are symlinks swapped under a `trap restore EXIT`
(`$CLAUDE_JOB_DIR/tmp/reh576/readstates.sh`; the real `out/stage90/` is never written). Four of them
produced four different answers, and only one of the four carries a reading:

| state | before (committed `33b6c63…`) | was that answer the gate's? |
| --- | --- | --- |
| **R4** record present, mode 000 | `exit=2`; stderr is `awk: fatal: cannot open file '…/xnu_arm_entry-config.txt' for reading: Permission denied`; 0 sections, 0 `UNREAD` | **no** - `awk`'s status, wearing the usage error's code |
| **R6** build config present, mode 000 | `exit=1`; stderr is `cat: …/stage90-build-config.txt: Permission denied`; **no REFUSING line, no section** | no - `cat`'s status, indistinguishable by code from a refusal |
| **R7** entry bin present, mode 000 | `exit=1`; stderr is the python `PermissionError` traceback **followed by** `REFUSING: the image does not carry the arm … holds, byte for byte` | no - worse: **a finding about the artifact, produced by a read that never happened** |
| **R8/R9** checksum list absent / mode 000 | `exit=1`; stderr is `sha256sum: SHA256SUMS.txt: No such file or directory` (or `Permission denied`) **followed by** `REFUSING: image does not match SHA256SUMS.txt; rebuild before booting` | no - a finding about the image, produced by a list that was never read |

R7 and R8/R9 are the two that matter most, because their output is not merely unreadable - it is
**wrong in the direction that costs the most**: an operator sees the gate assert a property of the
artifact (the payload does not embed the entry image; the image does not match its hashes) when the
actual fact is that the gate could not open a file. That is the "a measurement can be the thing that is
wrong" class, produced by the gate, about the very artifacts the gate exists to vouch for.

The other five states are the controls and none of them changed: R1 (entry ELF mode 000), R2 (ELF
absent), R3 (decoder not on `PATH`) are `exit=0` with the 579 clause's `UNREAD` lines, which is
non-fatal *by design* - that clause runs last and everything above it has already been judged - and R5
(record absent) is already the gate's own `REFUSING: no …` refusal, because absence had a guard where
unreadability did not.

## 3. The repair, in two layers

**Layer one, the invariant: an ERR trap that restates the command as this file's own refusal.** This is
the total repair - it covers every command in the file, including the log, the fixture, the decoder and
anything added later - and it is what makes the exit-code contract hold rather than hold-for-the-cases-I
-thought-of:

```bash
_errtrap() {
  local st=$?
  echo "REFUSING: the gate stopped with status $st at line $1, before any verdict; the command that failed was:" >&2
  echo "          $2" >&2
  echo "          That is the gate failing to READ something - not a finding about the artifact, and not the" >&2
  echo "          argument parser (the only exit 2). The last reading that completed is the one printed above" >&2
  echo "          this line; nothing below it was judged. Check that the path named above is readable." >&2
  exit 1
}
trap '_errtrap "$LINENO" "$BASH_COMMAND"' ERR
```

`$LINENO` and `$BASH_COMMAND` are both correct at trap time, which is why the trap is installed **after**
the argument loop and after `fail`: installed above the loop, the parser's own error would be reported at
the wrong line and with the wrong command text.

**Layer two, the message: `_readable` where existence is already checked.** The trap turns an unreadable
input into a refusal, but the refusal it can write is the tool's message; the gate knows *what it reads
out of each artifact*, so it says so at the eight sites where it already asks whether the artifact is
there:

```bash
_readable() {  # _readable <path> <what the gate reads out of it>
  [[ -e $1 ]] || fail "no $1 - $2"
  [[ -r $1 ]] || fail "$1 exists but this gate cannot read it; the file's own mode is $(stat -Lc '%04a' "$1" …) (octal, the form chmod takes) and its owner $(stat -Lc '%U' "$1" …), which is the leaf's fact only - a parent directory or an ACL can deny the open with these at their normal values. $2. A clause whose input could not be opened has no verdict about the artifact, so it stops here instead of reporting one; fix the permissions and re-run"
}
```

The sites are `$CONFIG`, `$IMAGE`, `$OUT/SHA256SUMS.txt`, `$ENTRY_BIN`, `$PAYLOAD_BIN`,
`$PAYLOAD_ELF`, `$ENTRY_CFG`, `$ENTRY_SRC_MANIFEST`. Two deliberate omissions:

- **the entry ELF is not in the list.** Its unreadability is 579's four-way `UNREAD`, which is
  non-fatal by design; adding a fatal guard here would convert a clause that runs last on purpose into a
  stop, and R1/R2/R3 are the states that would have changed.
- **the checksum list is the one site with an `-e` branch and no `[[ -f ]]` guard above it**, because
  the command that reads it - `sha256sum -c` - used to be the thing that reported it, as a finding about
  the image. It is not a regular-file check either: nothing needs it to be regular, only readable.

The ordering the two guards establish is **absent > nonregular-or-directory > unreadable**, and the `-e`
branch keeps the pre-existing `no …` message at the seven sites that already had it (R5 shows it).

Two things were fixed inside `_readable` after being measured wrong in its first draft:

- **`stat` without `-L` reports the symlink's mode, not the target's** - GNU `stat` does not dereference
  by default, and `out/` is full of symlinks, so the line that exists to report a read that could not
  happen was printing `777` for a mode-000 file. This is "one value, two definitions" landing inside the
  message about a value. The fix is `stat -Lc`.
- **`%a` prints `0`, not `000`**, so a mode-000 file displayed as `mode 0` - not the form `chmod` takes.
  The fix is `%04a`.

And the message says explicitly that the leaf's mode is the *leaf's* fact: a parent directory or an ACL
can deny the open with the leaf at 644, so the sentence gives a fact rather than implying a cause.

## 4. What the trap cannot fire for, measured in isolation before it was written in

`ERR` is not raised in several positions, and the whole design rests on that. Each was measured in
standalone probe scripts (`$CLAUDE_JOB_DIR/tmp/lntest.sh`, `lntest2.sh`) rather than assumed:

- **not when the failing command is part of a `||`/`&&` list other than the last.** So `fail` - which is
  the command *following* the final `||` - raises nothing when it runs, and its own `exit 1` raises
  nothing either: **refusals are not doubled.**
- **not in an `if`/`while` condition, and not under `!`.** So both guarded idioms the file leans on
  (`cmd || true`, and a test in an `if`) are outside `ERR`, and a read that *succeeded* cannot reach the
  trap.
- **not for the argument parser's `exit 2`** - a builtin `exit` raises no `ERR`, so exit 2 keeps **exactly
  one producer** and its vocabulary is unchanged. Rehearsed on both gates: `--bogus` → `exit=2`, 0 bytes
  of stdout, `unknown argument: --bogus`, 0 `REFUSING` lines.

`_errtrap` is also not triggerable from outside the gate: with a healthy tree the unguarded commands
cannot fail (the live run below is `EXIT=0`), and with a broken tree a `_readable` refusal or an existing
`-f` guard fires first. It is a floor, not a code path with a fixture - which is why it is written to
report "the last reading that completed is the one printed above this line" rather than to be diagnosed
from its own text.

## 5. After: nine states, one vocabulary

Re-running the same nine states against the changed gate (`REH_GATE=…/gate-581.sh`, same scratch tree,
same frozen snapshot):

| state | after | the reading |
| --- | --- | --- |
| R4 record mode 000 | `exit=1` | `REFUSING: …/xnu_arm_entry-config.txt exists but this gate cannot read it; the file's own mode is 0000 … and its owner lvyufeng, which is the leaf's fact only …` |
| R6 build config mode 000 | `exit=1` | same shape, naming `stage90-build-config.txt` |
| R7 entry bin mode 000 | `exit=1` | same shape, naming `xnu_arm_entry.bin` - and **the false byte-for-byte verdict is gone** |
| R9 checksum list mode 000 | `exit=1` | same shape, naming `SHA256SUMS.txt` - and **the false "does not match SHA256SUMS.txt" is gone** |
| R5 record absent | `exit=1` | unchanged, the pre-existing `no … - the entry image's switches are recorded there by build_entry.sh` |
| R8 checksum list absent | `exit=1` | `no …/SHA256SUMS.txt - every artifact this run will load is checked against its hashes` |
| R1/R2/R3 | `exit=0` | unchanged, the 579 clause's `UNREAD` lines |

Every non-green state now exits 1 with a `REFUSING:` line and names the artifact and the reason; no state
produces a verdict about an artifact from a read that did not happen. The exit-2 vocabulary is untouched
(section 4).

## 6. Nothing that did not have to move, moved

- **The change is a pure insertion.** `git diff --numstat` is **`63 0`** - no deletions - which is the
  guard against the "an edit on a prefix deletes the suffix" class in a file whose records are long
  physical lines. `bash -n` is clean.
- **The whole-gate rehearsal is byte-identical.** Four fixture states (A-D, `reh576/whole.sh`) printed
  byte-identical output between the committed gate and the changed one.
- **The section-by-section rehearsal is byte-identical except one line-number offset** (M1-M11,
  `reh576/rehearse.sh`): the region extractor's `region:` line reads `1915-2024` before and `1978-2087`
  after - same 110 lines, and the extracted text re-`cmp`s clean. That is the insertion's own line count
  (63) minus what the header absorbed, and it is the only difference in eleven sections.
- **The live gate is green on the frozen arm:** `EXIT=0`, 23 sections, 0 `UNREAD`, 0 stderr lines, the
  seam section still last.
- **The frozen artifacts are untouched before and after every rehearsal**: `xnu_arm_entry.bin`
  `151425c4…`, `xnu_arm_entry-config.txt` `bcacf065…` (thirteen keys, `XNU_SEAM_POC=0` /
  `XNU_SEAM_MEASURE=1`), `SHA256SUMS.txt` `aeb7862a…`. No rebuild, no `./build.sh`, no device command.

## 7. The census's boundary, and what this does not claim

This is a change to **the gate**, and it closes a status defect *in the gate's own exit code*. It is not a
census of every status the run can produce: the runner (`run_and_capture.sh`) is a separate process with
its own contract - 580 named one of its carriers (`grep -c` printing `0` for input it never read) and the
`exit 4` producer question is being settled on that side. Nothing here touches that file.

Nor does it claim that an unreadable artifact is now impossible to misread *by a reader*: it claims that
the gate no longer produces a status, or a finding, that a reader could take for a verdict about the
artifact. The distinction the change rests on is the one the whole phase rests on - **a status is a
verdict only if the process that produced it also produced a verdict.**

## 8. Safety

No device action, no build, no edit to any arm, no write under `out/`. The device is off the bus waiting
for the power press (572 section 8), the gate is green on the frozen arm, and nothing in this change
alters which bytes the next `fastboot boot` would carry. TWRP stays withheld: 「如果os已经能进去了的话」
is unmet.
