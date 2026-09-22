# 562: the log the gate tells the reader to read is already there, and it is the previous run's

A host-side change to `stages/stage90/preflight_boot_check.sh`, no device and no build. It closes a
**silent false positive** on the one reading the next run exists to produce - and the false positive is
not hypothetical: the file that would produce it is on disk right now, and it already carries the exact
bracket 547 §4 pre-registers.

## 1. The defect: an instruction to read a file that the run may not have written

The gate's exit-status clause ends, in both of its branches, by naming the log and telling the reader to
read it first:

```
Only the code whose own message says the device did not come back owes a power press - not the
one that refuses to call this a non-return - and the log at $LOG survives only until that
press, so read it first.
```

`$LOG` is read out of `run_and_capture.sh` itself (`LOGFILE=${LOGFILE:-/tmp/cancro-last_kmsg.txt}`), which
is right - one value, one definition. The problem is *when that file gets written*, and it is not on every
path:

| runner path | reaches step 5? | `$LOG` afterwards |
| --- | --- | --- |
| the device returned (`RETURNED`) | yes - `rm -f` then `adb exec-out … > $LOGFILE` | **this run's log** |
| exit 3, "REFUSING to call this a non-return" | no, it exits at :644 | **untouched - the previous run's** |
| exit 2, both branches | no, they exit at :656 / :666 | **untouched - the previous run's** |

So on every non-return path the file the gate has just told the reader to consult is **the last run's** -
and the code whose message names it (exit 3) is exactly the one where the device *did* come back, which is
the case the phase most wants to read. Nothing in the gate, the runner or the reader established which run
a file belongs to, and this project's own rule for that situation is unambiguous: **a line number or a
count quoted from a log must say which run and which block it is in**
([[mi4-measurement-defects]]).

## 2. Why this pair of runs is a false positive and not a general caution

Measured, before the change:

```
$ ls -l --time-style=full-iso /tmp/cancro-last_kmsg.txt
-rw-rw-r-- 1 lvyufeng lvyufeng 596665 2026-09-22 02:55:16.601456234 +0000
$ sha256sum /tmp/cancro-last_kmsg.txt
f0285b0f6e22eb0a097b5756cc6a5e11f827e1d6b20be03c9eb0b1a0510b2612
$ for k in pre rtcpre post; do printf '%s %s\n' $k "$(grep -ac xnu_live_slot_${k}_calls= /tmp/cancro-last_kmsg.txt)"; done
pre 1
rtcpre 1
post 0
```

That is **547 §4's prediction, verbatim**: `slot_pre_calls` and `slot_rtcpre_calls` published with
`slot_post_calls` absent. And it is not a coincidence - it is the death that prediction was *written from*.
520's run is the only capture in this phase, so the stale file and the prediction are the same artifact:

> a run whose capture failed, read through `/tmp/cancro-last_kmsg.txt` as it stands, would **confirm 547 §4
> with the data 547 §4 was derived from.**

Three things make that worse than an ordinary stale file:

1. **The gate's own sentences invite it.** They say the log "survives only until that press, so read it
   first" - which is an instruction to read *now*, in the state the failure left behind, with no test
   attached.
2. **Exit 3's message asks for a hand re-capture, which is the step that gets skipped.** It prints
   `sudo adb -s $SERIAL exec-out 'cat /proc/last_kmsg' > $LOGFILE` and then exits. If the phone settles
   into Android that works; if it does not, or if the human reads the file instead of re-capturing it, the
   old file stays and is now labelled by context as the new run's.
3. **The log's own layout cannot separate the two runs.** The ram console has two writers in two blocks and
   line order is chronological only *within* a block (557 §3), so the position of the keys relative to the
   panic text proves nothing about which boot wrote them - and 520's log already holds the keys **and** the
   console-text copy of the same death.

## 3. What the gate now does, and why that and not a refusal

The gate is a preflight, so it cannot check the file *after* the run. It does the two things it can:

- **records what the file is at the only moment a "before" exists** - path, size, mtime, sha256, and for
  each of the three bracket keys the record count *and the last published value*, because the reader's own
  rule is `tail -1` (`run_and_capture.sh`'s `keyval`) and a count alone cannot say which record a reader
  would take;
- **names the bracket already in it** rather than describing the hazard, with the test stated in one
  command: *this sha256 must have changed; unchanged means no capture happened, which is exit 3's state,
  and the bracket read from it is the one that was already there.*

The three-state shape the block prints, and why each is not a failure of the gate:

| state of `$LOG` | printed | why |
| --- | --- | --- |
| absent | "absent - and that is the useful part" | after the run, **existence itself is the first check**, since step 5 removes then creates it |
| present but not readable (`$(id -un)` cannot read it) | `UNREAD` + the note that a sudo hand re-capture leaves it root-owned | a reading that could not be taken must not look like one that was |
| present and readable | the fingerprint, the three counts, and - **only if pre ≥ 1, rtcpre ≥ 1, post = 0** - the trap named | the condition is the prediction's own shape, so it discriminates rather than always firing |

**It does not refuse, and that is deliberate.** A log carrying the predicted bracket is the *normal* state
of this tree today; a refusal here would stop a legitimate armed boot to prevent something that is only a
threat to a *failed* capture, and the arm is the thing that has been owed since 533. This is a record and a
name, not a proof - and the honest limit is stated in the block: the gate cannot see after the run.

## 4. Tested in five states before it is spent, and one of them was a bug in the edit

| state | file | result |
| --- | --- | --- |
| absent | `LOGFILE=<tmp>/nope.txt` | the "absent" branch, with the backtick bug below |
| present, unreadable | a root-owned `0600` copy | `UNREAD … not readable by lvyufeng`, and nothing else claimed |
| present, **the predicted bracket** | the real `/tmp/cancro-last_kmsg.txt` | fingerprint printed **and** the trap named |
| present, **`post` published** | 520's log + one injected `xnu_live_slot_post_calls=0x00000002` | fingerprint printed, **trap not named** - the discriminating negative |
| default run | `--allow-xnu-entry`, no override | **exit 0**, gate GREEN |

The fourth state is the one that matters: without it, a condition that always fired would look identical to
one that discriminates, and this project has paid for that distinction more than once
([[mi4-silence-is-a-reading-only-if-success-is-silent]]).

**The first state found a real bug in the edit, and it is worth recording.** The "absent" sentence was
written as `echo "… writes it with a \`rm\` then a redirect …"` - and backticks inside a double-quoted
string are command substitution, so the gate **executed `rm` with no operand** on every run and printed the
shell's `rm: missing operand` into the middle of its own narration. The gate's exit status was still 0 and
the sentence still read as prose, so nothing else would have caught it: it took running the branch that
contains the line. Fixed by quoting it as `'rm'`. The general form is the file's own oldest rule - a
sentence about a command is not a comment when the shell can execute it.

## 5. What this does not do

- **It cannot check the file after the run.** The comparison is a human's or a reader's, by sha256. Making
  it structural would need the *runner* to record the file's identity before its own boot - its lane, and
  worth asking for; noted rather than crossed.
- **It changes no criterion.** 547 §4 is still the pre-registration and the bracket is still three keys in
  one order. The change is to the *attribution* of a bracket, not to what the bracket means.
- **It does not touch `run_and_capture.sh`.** Everything it reads out of that file it still reads out of
  that file.
- **The frozen pair is untouched** - `1daaf44e624563694e…` (boot image) and `f202f2465886aba6…` (entry
  image); `./build.sh` not run, nothing under `out/` written.

## 6. Safety

No device action. `bash -n`; the whole gate with `--allow-xnu-entry` in five states, **exit 0** in the
default one; a `diff` against the pre-change output of **one hunk, twelve added lines, no removals**, all
of it the new block. The three non-default states used `LOGFILE=` overrides and copies under the job's own
`tmp/` - **the real `/tmp/cancro-last_kmsg.txt` was read and never written**, which matters because it is
the phase's only capture and is not reproducible. Nothing written outside `stages/stage90/preflight_boot_check.sh`
and `docs/`; `flash` not used and nothing written to storage. Non-persistent `fastboot boot` remains the
only device verb this phase uses and this step does not run it. The device is off the bus and owes a power
press before 533 can run.
