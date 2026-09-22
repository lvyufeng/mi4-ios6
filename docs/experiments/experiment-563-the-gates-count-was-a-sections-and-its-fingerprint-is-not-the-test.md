# 563: the gate's count was a section's, and its fingerprint is not the test

A host-side change to `stages/stage90/preflight_boot_check.sh` only. No device, no build, nothing under
`out/` read or written. It is the second half of
[562](experiment-562-the-log-is-already-there-and-it-is-the-previous-runs.md), and it exists because
562's block was written against a `run_and_capture.sh` that changed underneath it in the same window:
[564](experiment-564-the-capture-is-staged-and-a-returned-runs-capture-failure-is-not-exit-1.md) staged
the capture and added **step 2b**, which parks the log before the boot.

The theme is one sentence, and it is this project's most-paid-for one: **a sentence in this file that
describes another file's state is a claim, not a check** ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
Three of 562's sentences were that, and the runner's edit is what made them false.

## 1. The count was one region's, and it read like the file's

The exit-status clause reads section 4 out of the runner and prints a census of it:

```
read out of that section at gate time: 3 exit site(s), distinct code(s) 2 3, at
    run_and_capture.sh:700   exit 3
```

`3 exit site(s)` is true of **section 4**. 564 added a second `exit 3` in **section 5** - the returned
run whose capture failed, which is a state the gate's own text explains at length - so the sentence
became false of the *file* while remaining true of the *section*. That is 543 and 552's defect exactly:
a number whose scope is unstated, read as a broader claim than it was measured for.

The repair is to state the scope rather than to widen the read. `READ_CODES` stays `"2 3"` and the
census of section 4 is unchanged; a second census is printed beside it:

```
for scope: this file has 5 exit site(s) in total, code(s) 0 2 3 - so the count and
  the codes above are section 4's, and a code can have a second producer outside it.
```

**Why not widen it.** The peer offered the choice ("Your call whether to widen it or to say it is
scoped") and widening is the wrong direction, for a reason internal to the gate: every sentence under
that census - "one of them says the device *did* come back, into a state adb cannot reach", the
instruction about which code owes a power press - narrates a **wait**, and section 5's `exit 3` happens
after a wait has already succeeded. Folding it into the same set would make the gate's prose a
description of states it has not described. A number with its scope stated is honest; a number widened
so that a sentence stays true is the same defect moving. The `0` in the file-wide census is printed and
not explained, which is the point of printing it: the clause says where its narration stops.

## 2. Three stale claims, and the third is a wrong number — and then a fourth, found by sweeping the predicate

The runner changed in the same window, so every sentence here that described the runner's *mechanics*
had to be re-measured. Three did not survive:

| 562 said | the tree now does | repair |
| --- | --- | --- |
| "the runner writes it with an 'rm' then a redirect from adb (step 5)" | step 2b **parks** it (`mv $LOGFILE $LOGFILE.prev`) before the boot, and step 5 reads into `$LOGFILE.new` and renames into place **only on a non-empty read** | the absent branch now says that, and points at step 2b's own sentence for the same state |
| "as it stands **before the run**" | the gate cannot see whether a run follows it, nor whether the hand re-capture happened instead | the header reads "as it stands **at gate time**", with the assumption and its breakers printed under it |
| "(that file's own **563**)" | the runner's change is **564**; 563 is this doc | both occurrences corrected to 564 |

The third is worth naming out plain, because it is the cheapest possible instance of the class: a
**number** in a comment naming a change that a peer renumbered while I was writing about it. The peer
renumbered to 564 deliberately - "your file existed first, and the 556/548 precedent is that the later
side moves" - which is exactly the kind of move that leaves a citation behind. It was caught by
grepping the gate for the string before committing, not by reading it.

**The fourth was found by the sweep the first three should have been run as, and it is the one that
mattered operationally.** 560's lesson is that a correction sweep has to be run *by predicate*, not by
provenance - the files the phase has edited are not the set of files carrying the wrong sentence. So
after landing 563 I grepped the gate for every sentence that describes the runner (`step 2`..`step 5`,
`section 4`/`5`, `$LOGFILE`, `rm`, `redirect`, `exit 2`/`3`) and re-measured each against the runner as
it now stands. One survivor, and it was in the block I had just edited:

| 562 said | the tree now does | repair |
| --- | --- | --- |
| on a non-return, the script leaves before step 5 and "`$LOG` is **untouched**" | step 2b moves it aside **before** step 3 boots, so on a non-return the name is **absent** and the bytes are at **`$LOGFILE.prev`** | the comment now says that, and both the comment and the printed block point the reader at `$LOGFILE.prev` for the earlier bracket |

Why this one is worse than the other three: it is aimed at the reader of a **non-return**, which is one
of the two states 547 §4 pre-registers for the arm in `out/`, and "untouched" is the opposite of what
they will find. An operator who follows it reads `$LOG`, finds nothing, and has no reason to look for
`.prev` - on the run that just spent the freeze. The gate now prints the parked path in every state
(the `echo` sits above the branch, so it is not contingent on which one fires): *"if the name is gone
after the run, the PREVIOUS log was parked rather than lost: step 2b moves it to `$LOG.prev` (then
`.prev.2`, ...) ... Read that path for the earlier death and never as this run's, whatever the bracket
in it says."* A gate that tells the reader where the evidence went is the whole point of a preflight;
one that tells them it is where it is not is worse than silence
([[mi4-silence-is-a-reading-only-if-success-is-silent]]).

## 3. The fingerprint is no longer the test, and the block says so instead of being deleted

Step 2b is the reason. It `mv`s the previous log aside **before** the boot, so after the boot the name
`$LOG` holds *this* run's capture or it holds nothing:

| after the run | reads as |
| --- | --- |
| `$LOG` absent | no capture reached the name - and with a park, step 2b's own message has already named where the previous bytes went |
| `$LOG` present and non-empty | this run's capture, or a hand retry's |
| `$LOG` present, sha256 unchanged from the gate's print | only reachable when step 2b's park **failed** (`511`, `512` - sticky `/tmp`, an identity that does not own the file); 564's own message says the comparison applies in that case and not otherwise |

So the post-run test is `[[ -e $LOG ]]` plus non-empty, and this is the design that 562's sha
comparison was reaching for: **a hash cannot separate a byte-identical reproduction from a fresh
capture, and existence can.** If a run captured exactly the bytes the previous run captured, the hash
says "unchanged" and the name says "this run's"; only the name is right.

The block stays rather than being removed, and the reason is stated in it: what remains is the
operator's **independent** record, and the fingerprint that identifies a *parked* file as the one this
gate saw. 564's exit-3 message points back at exactly that ("compare its sha256 against the one the
gate printed above"), so deleting the block would break the runner's one hand-fallback. What the block
no longer is is the test, and the printed lines now say which of the two they are - because a check
that has been demoted keeps its authority silently otherwise.

## 4. A copy of the log was built, tested in six states, and withdrawn

Recorded because "I built it and did not ship it" is otherwise indistinguishable from "I never tried"
([[mi4-silence-is-a-reading-only-if-success-is-silent]]).

Before reading 564, I read 562 §5's own request - "making it structural would need the *runner* to
record the file's identity before its own boot - its lane, and worth asking for" - as still open, and
built the gate-side half of it: a content-addressed copy of `$LOG` at `$LOG_BEFORE`, taken at gate
time, with the old copy moved aside under its own hash rather than overwritten.

| state | setup | result |
| --- | --- | --- |
| absent | `LOGFILE=<tmp>/nope.txt` | "no copy is taken: there is nothing to copy"; no file created |
| unreadable | root-owned `0600` copy | `UNREAD`, "**a gap rather than a pass**"; no file created |
| readable, no prior copy | the real log | copy taken, **`cmp` identical**, both commands printed |
| re-run, unchanged | the same pair again | **no churn** - no `.before.*` file appears |
| a **different** prior copy | the log + one injected `post` line | the old copy kept as `log.before.f0285b0f6e22` and **verified byte-identical**; 547 §4's trap correctly **not** named (the injected `post` is the discriminating negative) |
| default run | no overrides | **exit 0** |

All six passed. It was withdrawn anyway, on reading 564's step 2b and its staged step 5:

- **The park dominates it.** With the name vacated, the after-run test is `[[ -e $LOG ]]` - a state, not
  a comparison - and it is immune to the byte-identical case, which the copy's `cmp -s` is not.
- **It would have been a second definition of one value.** The before-state would then live in two
  places - the gate's `$LOG.before` and the runner's `$LOGFILE.prev` - which is this project's
  thirty-one-failure defect class ([[mi4-one-value-two-definitions]]), and the copies would drift the
  first time one side's path changed.
- **It made the gate write a file.** Bounded (one derived path, nothing the run reads), but a
  preflight that writes is a different instrument, and it would have been paid for a belt that is no
  longer load-bearing.

What survived from that work is what is actually mine: this doc, the reframe in §3, the scope census
in §1, and the tense correction in §2 - all four being the gate telling the truth about a file it does
not own, which is the part the runner's change cannot do from inside the runner.

A dead variable 562 left behind (`LOG_BYTES`, assigned and never used - `stat` already prints the size)
was removed in the same pass.

## 5. What 563 does not do

- **Does not touch `run_and_capture.sh`.** Every claim about it above is *read out of it at gate time*
  or quoted from it, not changed in it. Its `exit 3` count, its step 2b, its staged capture: all the
  peer's lane, all read-only here.
- **Does not change any criterion.** 547 §4 is still the pre-registration; the bracket is still three
  keys in one order; the trap is still named only on `pre ≥ 1, rtcpre ≥ 1, post = 0`.
- **Does not widen `READ_CODES`,** for the reason in §1.
- **Does not add a code value.** The file-wide census prints `0 2 3`; `0` is the runner's normal
  completion and was always there. No new state, no new meaning.
- **The frozen pair is untouched** - `1daaf44e624563694e…` (boot image) and `f202f2465886aba6…` (entry
  image); `./build.sh` not run, nothing under `out/` written.

## 6. Measurement, and safety

- `bash -n` clean throughout.
- The whole gate with `--allow-xnu-entry`: **exit 0**, in the default state and in the absent state.
- A `diff` of the gate's output against the 562 build: **four hunks** - the run-time exit-site line
  numbers (`658/670/680` → `700/712/722`, recomputed at gate time because the peer edited the runner in
  the same window, so a pinned number here would have had to be edited here too), the two new census
  lines, the header's tense, and the four printed lines under it.
- The absent branch is **not** exercised by a default run, so it was exercised explicitly with a
  `LOGFILE=` override under the job's own `tmp/` - and printed the step-2b/step-5 wording. Without that
  run the branch would have been a sentence nobody had read, which is how 562's backtick bug survived.
- The predicate sweep of §2 was run **after** the first commit, so its repair is `563b` and the `diff`
  is a second, separate one: against the 563 output it is **exactly three added lines** - the parked-path
  pointer - with exit 0 and `bash -n` clean. Splitting it across two commits is the honest record: the
  first three claims were found by reading the block I was already editing, the fourth only by grepping
  for the *shape* everywhere, and the second method is the one that should have been used first.
- The real capture is **read and never written**: `596665 bytes`, mtime `2026-09-22 02:55:16`,
  `sha256 f0285b0f…` identical before and after, and still carrying the bracket 547 §4 pre-registers
  (`pre_calls` 1, `rtcpre_calls` 1, `post_calls` absent) - the 2026-09-22 death the prediction was
  written from, and the phase's only capture.
- No device action, no build, no `flash`, nothing under `out/` touched. Non-persistent `fastboot boot`
  remains the only device verb this phase uses and this change does not run it. The device is off the
  bus; 533 remains owed and needs a power press.
