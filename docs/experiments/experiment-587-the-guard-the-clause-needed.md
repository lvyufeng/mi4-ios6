# 587: the guard the clause needed, and the branch that told the operator to fix a reader that was not broken

586 added a clause to the preflight gate that reads `run_and_capture.sh` and tells the operator, per
branch, whether the coming run's summary will print the two `UNREAD` lines that name
`xnu_live_slot_pre_calls` or will say nothing about the wrapper's own capture at all. It is the right
clause: the arm in `out/` cannot publish those words, and a silence there reads as agreement.

**This is the guard for that clause's own read.** `grep -q` returns 2 for a file it cannot open - absent,
a directory, mode 000 - and 586's clause had no guard, so an unreadable runner took the `else` and the
gate printed:

> **And …/run_and_capture.sh does not name xnu_live_slot_pre_calls anywhere**, so the summary of a run of
> this arm says NOTHING about the slot's own capture …

about a file it had never read, with `grep: … No such file or directory` printed immediately above it the
two lines contradicting each other on screen. That is 584 section 2's R7/R8 shape - **a property asserted
of an artifact by a comparison that never ran** - arriving one commit later, and inside the clause 586
added for the opposite reason. It is the same costly direction too: the sentence that follows tells the
operator to go and fix the reader, so the false branch sends someone to edit a file that may not be
broken, while the one thing that was actually unreadable is named nowhere in the message.

Nothing is built here, no arm is changed, and the device is not touched: the gate's output on the frozen
574 arm is **byte-identical** before and after this change.

## 1. The measurement

Five states, rehearsed against the gate in a scratch farm whose `stages/stage90/` holds a symlink to the
gate and a mutable copy of the runner (the real tree is never written; `$STAGE_DIR=$PWD`, so a farm is
built by providing the directory the gate `cd`s into):

| state | before (586's clause, `128b33a`) | was that answer the gate's? |
| --- | --- | --- |
| **S1** runner absent | the **false** "does not name … anywhere" branch; `grep: … No such file or directory` above it | no - a content claim from a read that could not happen |
| **S2** a *directory* of that name | same false branch (`grep` exits 2 with "Is a directory") | no - and this is the state `_readable`'s `-e`/`-r` pair would have admitted, which is why the fix carries a `-f` |
| **S3** runner mode 000 | same false branch (`grep: … Permission denied`) | no |
| **S4** runner readable, the key renamed away | the warning branch, correctly | **yes** - unchanged by this change |
| **S5** runner readable, the key present | the positive branch, correctly | yes - unchanged |

The healthy-tree case is the fifth: the live gate on the frozen 574 arm is `EXIT=0`, 23 sections, 0
`UNREAD` clauses, 0 stderr bytes, and its stdout is **byte-identical** to 586's run on the same arm.

## 2. The repair

The ninth guarded read, at the site 586 added, in the idiom the other eight already use:

```bash
[[ -f $STAGE_DIR/run_and_capture.sh ]] \
  || fail "no $STAGE_DIR/run_and_capture.sh, or it is not a regular file - … a gate that cannot open it has no verdict about what it does or does not name; and its absence also means the next command of the procedure cannot run at all. Nothing is rebuilt by this refusal"
_readable "$STAGE_DIR/run_and_capture.sh" "the summary text this branch's sentence describes is read out of it"
if grep -q 'xnu_live_slot_pre_calls=' "$STAGE_DIR/run_and_capture.sh"; then
```

Two things about the shape:

- **Why `fail` rather than a softer third message.** The rule 584 established is that a clause whose
  input could not be opened has no verdict and stops instead of reporting one. This clause's output is
  not decoration: it is what the operator uses to decide whether to **spend the boot**, and its `else`
  branch is an instruction to go and change another file. And the file is not incidental - it is the
  runner that performs and captures the boot, so if it is absent the documented next command cannot run
  at all. Refusing is the honest failure and the cheaper one.
- **Why a `-f` here as well as `_readable`.** The eight artifact sites have `[[ -f ]]` above them already,
  so `_readable`'s ordering reads as absent > nonregular-or-directory > unreadable when the two are
  combined. This site had no `-f`, and `_readable` alone is **true of a directory of that name** (`-e` and
  `-r` both hold), which would then take the false branch by a second route. S2 is that state, and it is
  the reason the guard is two lines rather than one.

## 3. What did not move

- **Pure insertion:** `git diff --numstat` is **`14 0`** against `128b33a` - no deletions, in a file whose
  records are long physical lines ([[mi4-an-edit-on-a-prefix-deletes-the-suffix]]). `bash -n` clean.
- **Inert on a healthy tree:** the gate's full stdout on the frozen 574 arm is byte-identical to 586's run
  - the clause's own two branches are unchanged in the readable case (S4, S5).
- **No new exit-code producer.** The refusal goes through `fail`, the file's only refusal, so the contract
  584 made true is untouched: `0` green, `1` a `REFUSING:` line, `2` the argument parser and nothing else.
- **Frozen artifacts untouched before and after every rehearsal:** `xnu_arm_entry.bin` `151425c4…`,
  `xnu_arm_entry-config.txt` `bcacf065…`, `SHA256SUMS.txt` `aeb7862a…`. No rebuild, no `./build.sh`, no
  device command.

## 4. Scope, and the fourth time this class has landed in this file

This is the fourth instance of one class inside this one file, and the pattern is worth naming: 573's
`UNREAD` printed inside a `GATE EXIT=0` run, 584's unguarded reads whose status became the gate's, 586's
silence in the reader, and now 586's own clause reading a file it could not open. Each was found in the
artifact *added by the previous fix*. That is not a coincidence about gates; it is what happens when a
file's job is to make claims about other artifacts, because every new claim is a new place to claim
something one did not establish.

What this does **not** claim: that a wrong `else` branch is now impossible in this file. It claims that
this site cannot report on a file it did not read, and that the ninth guard is in the same order as the
other eight. The general form - *a status is a verdict only if the process that produced it also produced
a verdict* - is the rule, and the check is per site.

## 5. Safety

No device action, no build, no edit to any arm, no write under `out/`. The device is off the bus waiting
for the power press (572 section 8); the gate is green on the frozen arm and byte-identical in output;
nothing in this change alters which bytes the next `fastboot boot` would carry. TWRP stays withheld:
「如果os已经能进去了的话」 is unmet.
