# 631: the rule was stated in three sections and enforced in none of the sections it named

`tools/rehearse_live_path.sh` has three kinds of cell - `run_state` (section A, the live path),
`reader_state` (section B, the reading) and `path_state` (section C, the path argument) - and all three
let a cell assert an **absence**: `forbid:PHRASE`, `MUST_NOT_SAY`. An absence is the weakest kind of
expectation there is, because **a command that failed to run at all also "did not say" the thing**: a cell
whose only expectation is `forbid:X` passes on a runner that reached nothing, printed nothing and called
nothing.

626 put a refusal against that shape into `reader_state`, and gave it a comment which named the other
sections:

> **And an absence needs a positive expectation beside it**, the guard **the path cells below carry** for
> the same reason: a command that failed to run at all says nothing, so "it did not say X" is true of a
> run that produced nothing.

**The path cells did not carry it.** 628, in its own last section, recorded the same gap from the other
side ("a cell whose only expectation is a forbidding one is now refused *in the reading section*; the
live-path section still allows them, which is the gap this step leaves behind"). And `path_state`'s
comment makes the same claim a third time - "So `MUST_NOT_SAY` is only ever asserted together with a
positive expectation" is a sentence about every caller, and nothing checks it.

So the rule existed in three places and was enforced in one, and the two places where it was merely
*written down* each asserted that the guard was already there. That is 604's class - **a claim in a
comment is not a check** - in its most obstructive form: a claim that names the *other* section as
already guarded is a reason for the next reader not to look there.

Host-side only: one harness edited, one document, one index row, plus transient probe copies in the
shared checkout that the batch's own cleanup removes (named below). No boot, no build, no device, no
`fastboot`, no `adb`, **nothing written to storage**. The arm and both parks were hashed and never
modified. **TWRP stays withheld.**

## 1. The measurement: a cell with nothing but an absence was accepted, at exit 0

A probe cell was inserted into the live path section - no expectation, one forbid:

```sh
run_state PROBE-ABSENCE-ONLY 0 "" 'forbid:The device did not come back'
```

Measured against the harness as it stood:

```
  ok    PROBE-ABSENCE-ONLY           exit=0
```

**Accepted, on a green row.** No refusal, no warning, and the table gave it the same `ok` any real cell
gets. The row asserts nothing about the run it fires: a `run_and_capture.sh` that died before its first
`say` would satisfy it exactly.

## 2. The repair: the same refusal, three times, with one wording

Each section now refuses the shape **before it runs anything**, so a cell that cannot assert anything
positive is reported at registration rather than as a row:

| section | expectation arguments | guard |
| --- | --- | --- |
| `run_state` | `expect_text` (`$3`) | refused when `FORBID` is non-empty **and** `expect_text` is empty |
| `reader_state` | `WANT[]` (since 626) | refused when `FORBID` is non-empty **and** `WANT` is empty |
| `path_state` | `MUST_SAY` (`$3`) | refused when `MUST_SAY` is empty **and** `MUST_NOT_SAY` (`$4`) is non-empty |

All three print `… asserts only an absence; give it a positive expectation too`, name the cell, and say
why in the same two lines - so the three are one rule with three call sites rather than three rules that
happen to agree. `reader_state`'s comment, which claimed the path cells carried the guard, now says what
is true: that the sentence used to be false and that this section is where it was measured.

## 3. The falsifications

Each probe is the first cell of its own section (so the guard is reached without waiting out section A's
twenty cells, which is what the first attempt got wrong - see section 4), and each is the real harness
with **one cell added and nothing else changed**:

| section | probe cell | result |
| --- | --- | --- |
| `run_state` | `run_state PROBE-ABSENCE-ONLY 0 "" 'forbid:The device did not come back'` | **exit 1**, `rehearse: run_state PROBE-ABSENCE-ONLY asserts only an absence`, refused after the first cell of section A rather than at the end |
| `reader_state` | `reader_state PROBE-ABSENCE-ONLY 'forbid:the largest recorded timeout is 40 ms'` | **exit 1**, `rehearse: reader_state PROBE-ABSENCE-ONLY asserts only an absence` |
| `path_state` | `path_state PROBE-ABSENCE-ONLY 0 "" "is a relative path" "$RELDIR" --summarise capture.txt` | **exit 1**, `rehearse: path_state PROBE-ABSENCE-ONLY asserts only an absence` |

All three figures are from the batch that placed each probe as the **first** cell of its section; the
guard fires before the probe's own runner invocation, so the exit 1 is the refusal and not the section's
own end-of-section verdict.

And the direction that keeps the guard honest: **zero of the harness's real cells trip any of the three**
- checked two ways, by re-deriving each cell's arguments from the harness source (20 `run_state`, 15
`reader_state`, 4 `path_state`, **0 trips**), and by the anchor run below, which is the whole battery on
the unmodified harness.

| run | harness | result |
| --- | --- | --- |
| the anchor | after the repair, real cells | section A **20 ok / 0 failed**, section B **15 ok / 0 failed**, section C **4 ok / 0 failed**, exit 0 |

## 4. One of my own probes measured the harness's schedule, not the guard

The first falsification batch placed the `reader_state` and `path_state` probes where the *other* probes
had gone - inside their sections but after section A had already been laid out - and ran each under a
30 s `timeout`. Both returned **exit 124** with no guard message. That is a timeout, not a refusal: the
harness runs section A's twenty cells first, and each of those costs 30-55 s because every one of them
invokes the runner, which invokes the peer's `preflight_boot_check.sh`. The probes were ~10 minutes away
from being reached.

The batch was rewritten to place each probe as the **first** cell of its section and to give it a
900 s window, and the results in section 3 are from that version. The discarded batch's output is not
quoted as a verdict anywhere in this document - a `124` from my own `timeout` is a fact about my window,
not about the guard, which is `[[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]`'s
shape arriving in a falsification harness: the status came from a producer (`timeout`) whose verdict is
about the *invocation*, and reading it as a verdict about the guard would have been wrong in the
direction that looks like a pass-to-be-explained.

## 5. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still the sleeper arm (`60063c47…`), still **UNRUN**, and the press is still the user's. The park at
  `/tmp/r594/frozen-payload` still verifies against `--set=frozen-574`.
* **It does not change the runner.** `8aad5d12…` before and after, unchanged on disk and in git.
* **It does not make an absence assertion *sound* - only non-vacuous.** A cell with one expectation and
  one forbid can still have a `forbid:` that no runner could ever print, and the phrase would then be
  satisfied by everything. That is the *other* half of 613's rule, and it is audited by hand rather than
  enforced: 630 ran that audit over the whole harness's kept outputs and found **zero gaps** on all ten
  forbids, and that audit remains a rule written down and not a check (602).
* **It does not mechanise "the rule is stated in N places" detection.** This step found the three
  sections by reading them. A fourth cell kind added later would start unguarded, and nothing would say so.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus; the only
  event that can move the goal is the user's **Vol-Down + Power**.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One file
edited (`tools/rehearse_live_path.sh`). Every probe ran through the harness, which asserts that `sudo`,
`adb` and `fastboot` resolve to its own stubs before any state runs - so even the probe cells could not
reach a device. The probe copies were transient files in the shared checkout
(`tools/.631-probe-run_state.sh`, `.631-probe-reader_state.sh`, `.631-probe-path_state.sh`,
`.631-guard.sh`) and are removed by the batch that made them; `git status` afterwards shows only the one
intended modification. `fastboot boot` only - never `flash` - so no outcome of any of this can write to
storage.
