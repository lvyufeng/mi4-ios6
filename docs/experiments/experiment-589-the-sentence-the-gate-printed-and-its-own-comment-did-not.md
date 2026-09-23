# 589: the sentence the gate printed and its own comment did not, in the state where the log is in DRAM

The gate's log-state section exists because the file that both exit-3 instructions send the reader to is
**not** the coming run's. 562 wrote it as a warning; 564 removed the trap at the source by making
`run_and_capture.sh`'s step 2b **park** the file before the boot. This is a two-sentence repair to what
that section *prints*, and both sentences matter for the one boot this project has staged.

## 1. The measurement

Two things were wrong with the printed text, and the first is the one that costs.

**(a) It asserted a park that will not happen.** The narration said, unconditionally and before the state
test:

> **And if the name is gone after the run, the PREVIOUS log was parked rather than lost:** step 2b moves it
> to `/tmp/cancro-last_kmsg.txt.prev` (then `.prev.2`, …), which is where the earlier bracket lives.

`mv` in step 2b sits behind `elif [[ -e $LOGFILE ]]`. With nothing at the name at gate time there is
nothing to move, step 2b takes `nofile`, and `.prev` is not written by this run at all. **That is the
state `/tmp/cancro-last_kmsg.txt` is in as this is written** — the name is absent, the bytes are at
`.prev` (the 2026-09-22 log, 596,665 B) and `.prev.2` (533's, 597,641 B), and the last several runs did
not return, which is what left it absent. So on the coming run the paragraph asserted a `mv` that will not
run, and — worse — framed the observation "the name is gone after the run" as *parked, not lost* one
paragraph above the state branch that reads exactly that observation as **the capture failed**. Two
adjacent sentences, opposite implications, and the reassuring one was the false one. That is 588's
defect one file over: a sentence about what is on disk, in the one place where being wrong costs the run
(the log is in DRAM and the next power press destroys it).

It is also a prediction in a block whose own first line disclaims predicting the run — *"this is a
reading of THIS moment, not a claim about the run"*.

**(b) It said nothing about the one state where the sha *is* the test.** The comment block above the
`echo`s has described that state since 564: when step 2b's park **fails** (a sticky `/tmp` and an
identity that does not own the file — 511/512), the earlier log stays at the name, a failed capture
leaves it looking untouched, and *then* the sha the operator was told was "not the test" is the test.
**The comment knew; the output did not say it.** Whether a park succeeds is a fact about `/tmp`'s sticky
bit and the file's identity, which the gate cannot see from where it stands, so this one cannot be made
conditional on a reading — it belongs in the printed text.

## 2. The repair

Both paragraphs are printed text and nothing else: `git diff --numstat` is **`26 4`**, and every added or
removed line is a comment or an `echo` (checked mechanically, not by eye). No branch, no read, no `fail`,
no exit code, and the park's `mv` remains the runner's.

- the "test is…" sentence gains the exception it was missing — that the park's *success* is not
  gate-visible, and that in that state the fingerprint is the test ("the runner says so in its own words
  when it takes that branch", which it does);
- the `.prev` paragraph is rephrased from **asserting** a cause to **conditioning** one — "parks the file
  at `$LOG.prev` … **whenever there is one to park**" — and now says outright that *gone with nothing
  parked* is the capture failing rather than a log tucked away.

## 3. What did not move

- **The exit contract and every structural reading.** `EXIT=0`, **23 sections, the same 23**, 0 real
  `UNREAD`, 0 stderr bytes on the frozen 574 arm. The section list against 588's run is **identical**.
- **Nothing outside this one block differs.** The whole output diff against 588's run is a single hunk,
  lines 439–450, inside `== the log those instructions name, as it stands at gate time ==`.
- **Not claimed: byte-identity.** 587 could claim it because its guard was inert on a healthy tree. Here
  the edited lines are in the branch that *is* taken, so the output differs **by design** — the honest
  statement is that the behaviour is unchanged and the text is not. 16 lines differ.
- **Three states rehearsed**, one of them the live one: absent (`EXIT=0`, the new text), present-and-
  readable (a copy of the real 2026-09-22 log, `EXIT=0`, the sha and the pre-registered-bracket clause
  printed), present-and-mode-000 (`EXIT=0`, 0 stderr, the UNREAD branch). The stubs were written under
  the job's `tmp/` and removed; **`/tmp/cancro-last_kmsg.txt` was not created** — writing a stand-in at
  that name is precisely the trap 562's line about a stale copy names.
- **Frozen artifacts untouched:** `xnu_arm_entry.bin` `151425c4…`, `xnu_arm_entry-config.txt`
  `bcacf065…`, `SHA256SUMS.txt` `aeb7862a…`. No rebuild, no `./build.sh`, no device command.

## 4. Scope

The claims the gate makes about `run_and_capture.sh` were re-read against the runner **at `e1551a4`**
(588) rather than trusted, because a claim about a peer's file is a claim about that file's future
([[mi4-a-claim-in-a-comment-is-not-a-check]]). All five still hold: step 2b parks before the boot; it
prints `no log at ... yet` in the state the gate's absent branch names; its `nopark` branch tells the
reader to compare the sha the gate printed; step 5 reads into `$LOGFILE.new` and renames only on a
non-empty read; and the exit-3 hand retry is a shell redirect, which creates the file before `adb` can
fail. So no claim in the gate needed moving with 588 — what moved is what the gate *says about itself*.

This is the fifth pass over this one block and the third in a row in which a fix was found inside the
artifact the previous fix added. The pattern that keeps producing it is narrower than "the gate is
wrong": **where a block narrates several states at once, a sentence that is true in one of them reads as
true in all of them** — and the state it is false in is the one the reader is in.

## 5. Safety

No device action, no build, no edit to any arm, no write under `out/`, nothing created at the log's name.
The device is off the bus waiting for the power press; nothing here changes which bytes the next
`fastboot boot` would carry. TWRP stays withheld: 「如果os已经能进去了的话」 is unmet.
