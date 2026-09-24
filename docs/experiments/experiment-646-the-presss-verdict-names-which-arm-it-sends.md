# 646: the press's verdict names which arm it sends, and the record cannot

The owed press sends **one** arm, and the project has **two** gate-clean candidates that answer
different questions:

| arm | what its log can decide |
| --- | --- |
| the **sleeper** (594/595, `STAGE90_XNU_IDLE_NO_SLEEP=1`) | whether the idle exit's `pop {fp, pc}` is survived at all - and it carries **no** `xnu_live_seam_*` key, because its seam's acting site is inside `platform_cache_idle_exit`, unreachable behind 599's closed `SIGPdisabled` gate (640 §2) |
| the **574 park** (`IDLE_NO_SLEEP=0`) | 638 §3's `a1`/`b1` pair, the pre-registered reading that chooses between 597's candidate repairs (A) and (B) |

`tools/verify_press_ready.sh` said the press would not be *wasted* - the bytes are the recorded bytes,
the gate accepts the tree, a press would be caught - and said nothing about **which** of those two
questions `out/` is armed to answer. That is the gap this step closes, from a proposal by
`run-experiment-526`: one more row, reading the arm out of the **ELF** rather than trusting the record.
**It is the last row that can be added before the press**, and it changes no byte of the arm: nothing
was built, nothing was run, `out/stage90/stage90-qcdt.img` is still `60063c47…` and still **UNRUN**.

## 1. Why the record cannot answer it, measured on both arms

The natural place to look is the record - and there are two of them, which is the trap:

| file | armed sleeper | 574 park | |
| --- | --- | --- | --- |
| `stage90-build-config.txt` (the **payload's** switches - what the gate prints) | `6c2b6038…` | `6c2b6038…` | **byte-identical** |
| `xnu_arm_entry-config.txt` (the **entry** image's switches) | `6636e2d1…` | `5073b0c4…` | differs |
| `STAGE90_XNU_IDLE_NO_SLEEP` inside it | `1` | `0` | the separating line |
| `xnu_arm_entry.bin` | `696a0f39…` | `151425c4…` | 5,519,996 B each |
| `xnu_arm_entry.elf` | `7f80c2cb…` | `3bc72605…` | |

The switch that separates the two arms is an **entry** switch, so the payload's own switch record - the
one `preflight_boot_check.sh` prints, and the one a reader who asks "what is armed?" is most likely to
open - is the same file on both arms. `[[mi4-one-value-two-definitions]]` in its plainest form: one
question, two records, and only one of them is about the arm.

## 2. The reading, and its positive control

`tools/check_idle_window_unreachable.py` already turns "which arm is this" into a property of the
image. It is now row 4 of the press verdict, and it reads the ELF's own instruction stream:

```
tools/verify_press_ready.sh
  ok    the arm is named by a reading      the SLEEPLESS arm (594/595):
        'the window is UNREACHABLE in this image.' and the entry record's
        STAGE90_XNU_IDLE_NO_SLEEP=1 agrees, so this press's log carries NO
        xnu_live_seam_* key, so 638 section 3's pair table and 642's sleh_pc
        join are UNREAD on it (640)
```

and, pointed at the park instead of `out/` (`--live /mnt/data/mi4-ios6-export/arm-574-idle-no-sleep-0`,
the positive control, which exists precisely so the row is not a sentence that can only be green):

```
  ok    the arm is named by a reading      the arm that ENTERS the window (574's park):
        'the window is reachable EXACTLY ONCE in this image.' and the entry record's
        STAGE90_XNU_IDLE_NO_SLEEP=0 agrees, so this press's log carries the seam pair,
        which is what chooses between 597's candidates (A) and (B) (638 section 3)
```

Both directions of the instrument have now been seen on this host, so the row is a reading of the
artifact and not a re-statement of the arm's name.

**The row reads the sentence, not the exit code.** Both of the extractor's verdicts exit **0**
(`check_idle_window_unreachable.py:381` and `:388`), and its refusals exit 1 - so the exit code answers
"could it read the image" and never "which arm". A row that keyed on the status would report *ok* on
either arm, which is the assumption-of-output-shape defect this project ranks first to suspect. So the
`VERDICT:` sentence is parsed, exactly two sentences are known, and **a third shape is refused** rather
than folded into the nearer arm - the state the extractor itself calls out at `:389` (more than one
software clearer) is a state this row has no reading for, and it says so.

## 3. What it refuses on, falsified in five directions and green in two

| direction | what was done | the row printed |
| --- | --- | --- |
| **ok** | the live `out/` arm | *the SLEEPLESS arm (594/595) … and the entry record's `…=1` agrees* |
| **ok** | `--live` at the 574 park | *the arm that ENTERS the window (574's park) … `…=0` agrees* |
| FAIL | 574 ELF beside the sleeper's entry record | *the reading says the arm that ENTERS the window (574's park), which is `…=0`, and the entry record says `…=1` - one quantity with two readings, and they disagree* |
| FAIL | an entry ELF that is not an ELF (4 KB of urandom) | *the reachability check exited 1 … `REFUSING: command failed: arm-none-eabi-nm -n …`* |
| FAIL | a stub extractor printing an unknown verdict sentence | *the extractor's verdict is a sentence this row has no reading for … so the arm is not named rather than named wrongly* |
| FAIL | a stub extractor printing no `VERDICT:` line | *produced no line beginning `VERDICT: ` … which arm it is cannot be inferred from either direction* |
| FAIL | the entry record with the switch line removed | *carries 0 `STAGE90_XNU_IDLE_NO_SLEEP=` line(s); the record must name that switch exactly once for the reading to be compared with anything* |

Every falsification was driven with `--live` pointed at a scratch directory - **no repository file was
doctored**, and the stubs live in `/tmp` and are deleted. The extractor is invoked with a timeout,
because a row that hangs is a row that never reaches its verdict.

## 4. The row that was *refuted* before it was written, and it is the better-known one

The obvious fifth row is the **witness**: `poll_seq` reaching 3 with `poll_timeout_ms >= 1000`. It is
what the owed run is for, so a row keyed on it looks like the row this file needs. It would be green
**whichever arm the press sends**, and that is measured rather than argued:

* `xnu_live_poll_seq` has exactly one publisher, `entry_note_poll` (`entry_stubs.c:4538`);
* its only call site is `entry_trace.c:1303`, which is **after** both guards - the first opens at
  `:1253` and closes at `:1264`, the second opens at `:1283` and closes at `:1296`, and they hold only
  514's repair and its note;
* so the witness is published by the sleeper and by the 574 park alike.

A check that cannot fail in the direction it was written for is not a check. **Consequence for the
reading order, and it does not change the press**: the witness is still the goal of *this* press (a
`poll_seq=3` with `poll_timeout_ms >= 1000`), because the sleeper can answer it; what the sleeper
cannot do is answer 638 §3's pair or 642's `sleh_pc` join, and a log with no `xnu_live_seam_*` key must
not be scored on either.

## 5. One hazard checked in this lane and not found here

`run-experiment-526` measured, in the same message, that six `xnu_arm_entry.bin` hits under its own
scratch trees are **symlinks** into the live tree - `sha256sum` and `stat` follow a symlink, so those
trees hash the live arm while holding none of its bytes. The hazard maps directly onto row 1 of this
tool, whose `cmp` is what makes the park a second field of the same bytes: **a park that were a symlink
into `out/` would make that comparison vacuous.**

It is not one here, and that is a reading rather than an assurance: `find out/stage90 -type l` returns
nothing, `find out/stage90/frozen/armed-sleepless-696a0f39 -type l` returns nothing, the set is **11
regular files and 0 symlinks**, and so is the export copy
(`/mnt/data/mi4-ios6-export/armed-sleepless-696a0f39/`). The tools in this lane that name the job
directory at all - `tools/check_ambiguity_predicates.sh:50` - use it to locate `press-watcher.sh` and
hash nothing inside it.

## 6. What this does not do

* **It does not fire anything.** Nothing was built, nothing was run, no `fastboot`, no `adb`, no
  `sudo`, nothing written to storage. The arm is unchanged: `out/stage90/stage90-qcdt.img` is
  `60063c47…`, entry `696a0f39…`, still **UNRUN**.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The OS is not observed booting. It
  removes one way for the press that could show it to be *misread* - a run scored against the wrong
  arm's table - and that is all.
* **It does not make the press possible.** The press is still blocked by the neighbour: at the time of
  writing `fastboot devices`/`adb devices` name nothing, which is the state a press is taken *from*,
  and `33e80afe` returns to the list on its own (634/635 measured the list at 3 h 35 m open). It must
  be **unplugged**, not merely absent.
* **It does not touch the two repaired-owed files.** `stages/stage90/run_and_capture.sh` (clause (5)'s
  predicate, 640 §5) and `stages/stage90/preflight_boot_check.sh` (the gate's seam caveat, the peer's
  lane) are untouched: both are fired by the armed catcher (620).
* **TWRP-to-storage stays withheld** - 「如果os已经能进去了的话」 is unmet.

**Two checks were run on this landing, and one of them is red for a reason that is not this row.**
`tools/verify_baseline_readings.sh` is green - 4 logs and 38 readings checked, 0 FAIL. And
`tools/check_experiment_index.py` reports **6 violations**, all of them `stage90 is not ascending
before its peak` at `README.md:504` and `:508-512` (`568` then `567`; `578, 577, 576, 574, 572, 569`).
**They are not this row's**: the same six, at the same six line numbers, are in `HEAD`'s copy of the
index - checked by running the checker against `git show HEAD:docs/experiments/README.md` and grepping
its order lines, since the links it also reports are an artifact of that copy having no sibling docs.
This row sits at `:536`, above all six, and the peak it joins is 646 itself, so the row is
order-clean; the column's pre-existing red is left where it is rather than silently "fixed" by a row
that is not about it.

## 7. Safety

Host-side only. The only device reads are the two device *lists* the row above it already read, and
they act on nothing. `fastboot boot` only, never `flash`, so no outcome of this step can write to
storage and a brick is impossible by construction. Both catchers were confirmed alive (pid 1344846
watcher, pid 4067419 relay). One file changed - `tools/verify_press_ready.sh` - plus this record.
`tools/rehearse_live_path.sh` is green on the tree as it stands: **20 ok / 0 failed** on the live-path
states, 4 ok / 0 failed on the path argument, and the reading table.
