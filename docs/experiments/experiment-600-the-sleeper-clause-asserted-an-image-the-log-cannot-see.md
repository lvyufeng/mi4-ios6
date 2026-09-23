# 600: the sleeper clause asserted an image the log cannot see, and 513's own captures are the counterexample

594's arm needed a reader before it could be run, and the reader that was written for it opened with a
claim about the *image*: "This log's image skipped 514's one-shot repair, so `SIGPdisabled` stays set".
The branch it opens, though, is entered by a predicate over the **log** — six `grep`s in
`stages/stage90/run_and_capture.sh:459-465` — and a log carries no build marker (549). So the sentence
was a claim about an artifact the predicate cannot see, in the one place a reader goes to learn which
arm ran.

This step is the measurement that turns that into a fact rather than a misgiving, because the project
already has two logs that satisfy the same conjunction from a *different* image: **513's two captures**,
from 2026-09-21 — two days before the arm existed, on an image with no repair to skip. They take the
sleeper branch and are scored by its whole ladder, which is correct behaviour and an incorrect
sentence. Three pieces of printed text and one comment are corrected; nothing is built, nothing is run
on hardware, and the one gated boot still waits on the user's power press.

**TWRP stays withheld** — 「如果os已经能进去了的话」 is unmet until a boot says otherwise.

## 1. The predicate, and the sentence it did not match

The branch is a conjunction of six tests on the log, and every one of them is a *behavioural* test:

```sh
idle_no_sleep_arm=0
if ! grep -a -q 'xnu_live_slot_cwe_' "$log" \
   && grep -a -q 'xnu_live_door_seq=' "$log" \
   && ! grep -a -q 'xnu_live_repair_seq=' "$log" \
   && ! grep -a -q 'xnu_live_sip_seq=' "$log" \
   && ! grep -a -q 'xnu_live_pce_seq=' "$log" \
   && ! grep -a -q 'xnu_live_wfi_seq=' "$log"; then
  idle_no_sleep_arm=1
fi
```

`door_seq`'s presence says `cpu_idle` ran at all; the four absences say the window's own family did not
publish. **"The window did not run" is what those six tests measure**, and on 594's arm the window did
not run *because the switch removed the repair* — a cause, not the test. The one image-level fact the
arm has is the arm switch in the build's record, and the gate reads it (`preflight_boot_check.sh`); the
log cannot, and neither can this clause.

The old text, verbatim:

```
  594's arm - the idle does not sleep. This log's image skipped 514's one-shot repair, so
  SIGPdisabled stays set, cpu_idle leaves by its first door on every pass, and the window
  whose pop {fp, pc} this phase has been measuring is never entered. So the whole family
  that clauses (1)-(5) score is absent on purpose, and this clause reads what is left.
```

and the replacement is the same reading with its subject moved from the image to the machine, plus the
counterexample named in it (§2). The comment above the branch (`run_and_capture.sh:469-476`) said
`(6) 594's arm: the idle never slept` — it now records that the branch is entered from the log's own
keys, that 513's captures land here too, and that the two cases differ in *cause* and not in *reading*,
which is why the ladder scores them alike.

## 2. The counterexample, measured on 513's two captures

`/tmp/513-run1-kmsg.txt` and `/tmp/513-run2-kmsg.txt`, 2026-09-21 14:31 and 14:46, both returned on
their own. Measured on both:

| key | run 1 | run 2 |
| --- | --- | --- |
| `xnu_live_door_seq` | 25 records, largest `0x01000000` | same |
| `xnu_live_poll_seq` | 4, timeouts `5` / `40` / `0x7d0` / `0x7d0` ms | same |
| `xnu_live_poll_error` / `_retval` | `0` / `0` on all four | same |
| `xnu_live_poll_ticks` at seq 3 / seq 4 | `0x024c37ca` / `0x024c3e14` | `0x024c072a` / `0x024c0c95` |
| `xnu_live_repair_*`, `_sip_seq`, `_pce_seq`, `_wfi_seq`, `_slot_cwe_*` | **0 occurrences** | **0 occurrences** |

So all six tests pass, and this is not a marginal shape: it is 25 door records and four *returned*
polls, two of them the 2000 ms park, with **no repair instrument in the image at all**. Run on the
reader, both print the branch's ladder — rung 0 PASS, rung 1 PASS, rung 1b PASS, rung 2 its NOTE, rung 3
PASS, and then the goal block. The sentence's claim about *their* image ("skipped 514's one-shot
repair") is false — there was no repair on 2026-09-21 to skip — and the claim about their *machine* is
exactly right.

That is the same defect 598 §4.3 found one clause over (a reader printing "520's is pc=…" while holding
another log) and the same one the project's defect table keeps at the top: **one value, two
definitions** — here, one predicate with two causes, and a sentence that named the cause it was written
for rather than the thing the predicate tests.

## 3. Rung 0 is a progress test, not an identifier

Rung 0 asks whether `xnu_live_door_seq` passed `32768`, and 595a's correction (`> 32768`, not `>=`) is
about the baseline stopping at exactly `0x8000`. On a boot whose log takes this branch, the record read
is `${door_max}`, which for 513's captures is **`0x01000000` = 16777216 — 512 times the threshold**.
An image that predates the repair by two days clears this test by 512×, so passing it says the machine
kept going and *not* that 594's arm is what ran.

That was the reason to add a sentence rather than trust the rung's name: printed alone, `PASS` beside
`xnu_live_door_seq reaches 16777216` reads like an arm identifier, and it is a progress test. The added
text says so and names the number.

## 4. Rung 1 is a measured outcome of this state, and 599's derivation now has hardware

598's note under rung 1 argued that a `poll_seq` of 2 on this arm could not be explained by a stopped
clock, because the two asks that *did* return in the archived pair returned before the repair was made,
with `SIGPdisabled` set and `cpu_idle` leaving by door 1. That argument is about two 5 ms and 40 ms
asks.

**513 is a stronger witness and it is the same machine state**: its two 2000 ms parks ran to
completion, with `SIGPdisabled` set, the window never entered, and (this image having no repair) no
`repair_seq` in the log at all. So the park's return is not an extrapolation from short asks — it is a
measured return of the park, on hardware, from the state 594's arm freezes the machine in.

And it is the hardware half of **599 §6**: `rtclock_intr` must arrive without the IPI, because that
image's parks expired with the IPI handler unable to run (`SIGPdisabled` set closes the path both ways,
599 §4). 599 derived that from the disassembly (three of the image's four `bl rtclock_intr` sites are
outside `cpu_signal_handler_internal`); 513's two captures are the same claim, measured.

## 5. The guard that caught its own author, on the two lines that were meant to fix this

The first draft of the rung-1 note quoted two identifiers in backticks inside `say` strings, and the
runner **refused to start**:

```
run_and_capture: a printed string contains a backtick, so bash will RUN it:
622:        say "        window never entered (their `poll_ticks` 0x24c072a and 0x24c0c95, ~38.6 M ticks"
626:        say "        from the disassembly: three of the four `bl rtclock_intr` sites are outside the"
```

`bash -n` was clean — this is a runtime refusal at script start (`run_and_capture.sh:93-120`), the
structural check 594 §4 built after its own backtick defect, and it exists precisely because the
mistake is invisible by reading. It fired on a step *about* a reader being wrong, which is the useful
kind of embarrassment: the file's own check caught the file's own author, unasked, before the text was
committed.

The fix is wording and not escaping, which is 594 §4's rule: `their poll_ticks 0x24c072a` and
`the four bl rtclock_intr sites`.

## 6. An attribution defect inside the sentence that was written to fix attribution

The replacement sentence's first draft read

> 513's two captures each ran two 2000 ms parks to completion … (**their** `poll_ticks` 0x24c072a and
> 0x24c0c95, ~38.6 M ticks …)

and both quoted values are **run 2's**. Run 1's are `0x024c37ca` / `0x024c3e14`. A plural possessive
followed by one capture's numbers is exactly the shape §2 is about — a claim about two artifacts
carrying one artifact's values — reproduced inside the sentence written to remove it.

Found by reading the four records out of the logs instead of trusting the sentence just written, and
the fix is all four values with the capture named for each. The rounding was wrong in the same
direction: the four counts are `38,549,450` / `38,551,060` / `38,537,002` / `38,538,389` — **38.54–38.55
M**, so "*~38.6 M*" rounded away from the measured range and is now `~38.5 M`.

## 7. Verified on 22 states, and the two defects in the harness that measured them

The oracle is HEAD's own runner (`git show HEAD:…`), run beside the edited file at the *same depth* in
the tree — `stages/stage90/zz-head-cmp.sh` — because the script does `cd "$(dirname "$0")"` and resolves
`REPO_ROOT` from there, which is the mistake 598 §5 made by one directory (`REPO_ROOT` became
`stages/`, the old half read nothing, and every line would have looked changed). The copy is deleted
before the gate runs.

| state | what it is | result |
| --- | --- | --- |
| 513 run 1, run 2 | the counterexample, artifacts | differ; **narrative only** |
| `syn-A`, `syn-B`, `syn-C`, `syn-E`, `syn-F`, `syn-G` | 594's sleeper synthetics | differ; **narrative only** |
| `syn-D` (no `door_seq`), `syn-empty` | 594's other shapes | **byte-identical** |
| 514 run 1/2, 515 run 1/2, 516 run 1/2/3 | baseline-shaped logs, older instrument | **byte-identical** |
| 533-A, 533-D, 533-J | 533's archived captures | **byte-identical** |
| `cancro-last_kmsg.txt.prev`, `t566-real520.txt` | **520's** captures | **byte-identical** |

**14 of 22 byte-identical, 8 differ, and the 8 are the sleeper branch only.** In all 8 the replaced
lines are narrative: zero of them carry `PASS`/`FAIL`/`UNREAD`, and the *set* of verdict lines
(`^  (PASS|FAIL|UNREAD|NOTE|DIED|PREDICTED)`) is character-for-character identical before and after —
6 lines on six of the states, 3 on `syn-B` and `syn-C` (which reach fewer rungs). So no verdict moved
anywhere, and the half of the block 600 is not about is proven untouched by byte comparison rather than
by reading the diff.

**The harness had two defects of its own, and both would have produced a wrong conclusion.**

1. **The HEAD copy was not executable.** `cp` gives the copy the umask's mode, so the first pass ran
   `zz-head-cmp.sh` and got **exit 126** on every "before" run, with empty output — which would have
   made every one of the 22 states read as a difference. Caught by asserting the exit codes explicitly
   (`> 3` ⇒ harness failure) instead of letting `cmp` on an empty file speak: the empty side is not a
   verdict, it is a producer that delivered nothing ([[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]).
2. **The verdict-line filter matched nearly everything.** The first predicate included a bare
   `^  ` alternative, so it extracted almost every line of the output; under it, 7 of the 8
   commentary-only diffs were labelled `VERDICT-CHANGED`. The corrected predicate is the explicit token
   list above, and the label flips for all 7 — a filter whose pattern was broader than the thing it
   tested, which is the shape the check was supposed to be detecting.

**And the gate, which reads this file:** `preflight_boot_check.sh --allow-xnu-entry` → **EXIT=0**, 518
lines, 0 stderr. Its exit census still reports **3** sites, codes `2 3`, now at
`:1884`/`:1916`/`:1926` (598 recorded `:1844`/`:1876`/`:1886`; 600 added 45 lines and deleted 5). No
`exit N` and no `die` was added. `out/stage90/stage90-qcdt.img` is still sha256 `60063c47…`, and the
gate still says so.

## 8. What this does not do

* **It does not boot anything, and it changes no arm, payload, gate or prediction.** The image the next
  power press sends is unchanged, and rung 0 and rung 1 still decide it.
* **It does not make the log able to identify the arm.** No log can (549), so the correction is to stop
  claiming it rather than to publish more. An image-level marker would be a new instrument and a new
  build — and a build would replace the parked arm.
* **It does not decide 「能进入操作系统」.** The goal block (596) is untouched; its criterion is still
  user mode reached and a driver answering, and its ceiling is still that the machine stays up across
  the idle pass.
* **It does not retire rung 2's NOTE on this arm.** On a capture whose image has no repair, the park's
  console group cannot exist — so the NOTE is a property of the image and not of the run, which is one
  more reason the whole branch is a reading of the machine and not of the arm.
* **It does not touch 597's two deferred `entry_trace.c` repairs, 535/572's non-return, or 574's `b1`
  axis.** All still need an arm.

## 9. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One
host-side file edited (`stages/stage90/run_and_capture.sh`, +45/−5, which is not in
`xnu_arm_entry-sources.txt` — the gate's freshness scan deliberately does not match `*.sh`); reads of 22
archived and synthetic logs, of 513's two captures in detail, and of the runner itself. One temporary
same-depth copy of HEAD's runner existed for the oracle and was deleted before the gate was run; the
working tree now holds only the intended edit. The payload, the parked frozen pair at
`/tmp/r594/frozen-payload/` and the arm the next press sends are unmodified. `fastboot boot` only —
never `flash` — so no outcome of any of this can write to storage.
