# 560: the same wrong key in four more files

A host-side correction to four committed documents, no device and no build. It is the **third iteration of
one sentence**, and the reason it needed a step of its own is that the first two iterations each stopped at
the wrong boundary:

| step | what it fixed | what it stopped at |
| --- | --- | --- |
| 557 (host-side reading) | the *table* - it listed `slot_ab_calls`/`rtcab_calls` as "**not** per-pass - they track the nine abort episodes" | did not go back to the reader that was keyed on the key |
| 558 (the peer's) | `run_and_capture.sh`'s clause (3), plus 533 and 540 | the files the phase had *edited* |
| **560 (this step)** | **546 §5, 547 §4, 554 §1's table row, 557 §4c** | - |

**The enumeration has to be "every file that names the key", not "the files this phase has touched".** The
four documents below were all committed *before* 558 and none of them was in the set 558 worked from,
because 558 swept the phase's recent edits instead of grepping for the name. The whole of the census is
one command - `grep -rn rtcab docs/ stages/stage90/` - and it now returns only correct usages and the
correction notes (§4).

## 1. The four, and what each one said

All four are **pre-registrations**: sentences written before the run that fix the criterion the run will be
read against. That is what makes the wrong key worse here than in a narrative block.

| file | where | as written | corrected to |
| --- | --- | --- | --- |
| [546](experiment-546-the-pop-is-cacheable.md) | §5's prediction for 533 | "`pre_calls` and `rtcab_calls` published with `post_calls` absent" | `rtcpre_calls` |
| [547](experiment-547-the-load-succeeded-and-the-value-is-wrong.md) | §4's second bullet | "`slot_pre_calls` and `slot_rtcab_calls` published with `slot_post_calls` absent" | `slot_rtcpre_calls` |
| [554](experiment-554-the-reader-called-the-prediction-a-failure.md) | §1's row quoting 533 §5.2 | "`pre` + `rtcab` present, `post` absent ⇒ inside `platform_cache_idle_exit`" | `pre` + `rtcpre` |
| [557](experiment-557-the-cache-window-is-entered-exactly-once.md) | §4's "one consequence for the phase" | "533's instrument predicts `pre_calls` and `rtcab_calls` published with `post_calls` absent" | `rtcpre_calls` |

547 §4 is the one the gate *cites by name* as the enable-off arm's pre-registration, and 546 §5 is the
mechanism's own falsifier - so the wrong key sat in the two documents that decide how the next run is read,
not only in commentary about them.

**Why the name is not cosmetic in any of the four.** The exit wrapper's second publisher is `g_slot_rtcpre`
(`entry_trace.c:1973`, `entry_slot_rtc_note(&g_slot_rtcpre, entry_tpidrprw())`); `g_slot_rtcab` is published
from `entry_note_sleh` (`entry_stubs.c:1792`, `entry_slot_rtc_note(&g_slot_rtcab, thread)`) - the **abort**
path - so `rtcab_calls` is a *storm* count. 520's own capture is the separator: `rtcab_calls` has **five**
records (`1,2,3,4,8`) while `pre_calls` and `rtcpre_calls` in the same pass have **one** each, and a
per-pass counter cannot have five records while its siblings in the same pass have one. A criterion
written on `rtcab` is therefore satisfied by any pass that took an abort - which every dying pass does - so
"`rtcab` present, `post` absent" reads as *inside the exit* for a death that is anywhere. **The prediction
was, in all four, checkable by the wrong question.**

## 2. 557 §4c is the sharpest of the four, because it contradicted its own table

Ninety lines above the sentence, 557's own table (§2) already says exactly the right thing:

```
| `xnu_live_slot_ab_calls` / `rtcab_calls` | 5 each (`1,2,3,4,8`) | the abort-path notes | **not** per-pass - they track the nine abort episodes |
```

and ninety lines below it the same file's §4 uses `rtcab_calls` as the per-pass key it had just denied.
That is **one value with two definitions inside one document** ([[mi4-one-value-two-definitions]]) - the
project's oldest defect class - and it is the reason this step exists at all: a file that contains both
spellings is a file where a reader can find whichever one they were looking for. Corrected in place with
the contradiction named, not silently overwritten.

## 3. The two README rows, which are a description of a description

`docs/experiments/README.md`'s cells for **546** and **540** restated the key in their summaries of those
steps ("`pre`+`rtcab`", "`g_slot_rtcab`"), so the index carried the same name one level out from the files
it indexes. Both cells now say `rtcpre`, name the abort path's counter as what was written, and point at
the step that corrected it (546 → this document; 540 → 558, which corrected 540 itself, and this row's own
spelling). **An index that repeats a claim is a second definition of it** - the same trap as the gate's
bracket paragraph, one layer further out, and it was found by grepping the whole tree for the name rather
than by reading the rows.

## 4. What is not changed, and one report now discharged

- **No prediction changes.** 546 §5, 547 §4, 554 §1 and 557 §4c each state the same three sites in the same
  order with the same localization; only the middle key's *name* is corrected. The run 533 owes is read
  exactly as it was pre-registered.
- **The gate and the runner are untouched from here.** The gate's own copy was fixed under
  [559](experiment-559-the-brackets-middle-key-was-named-wrong-in-the-one-block-that-governs.md) (my lane);
  `run_and_capture.sh`'s clause (3) was fixed under 558 (the peer's).
- **558 §5's report is now discharged, and its quotation is a quotation of a file that has since changed.**
  558 §5 reproduces the gate's bracket block as it stood then (`xnu_live_slot_rtcab_calls (the same site's
  rtcpre spelling is the other key set …)`) and asks for it to be patched; 559 is that patch. The quoted
  `echo` lines are therefore **not** the gate's current text, and 558 §5's heading ("still has it") is
  true of `e362a28` and false of the tree. Recorded here rather than edited across the file boundary
  (558 is the peer's step, and this step's lane is the four docs above) - the two documents should be read
  together.
- **Correct usages are left alone**, from `grep -rn rtcab docs/ stages/stage90/`: 521 §"Symbols" lists
  `g_slot_rtcpre` `0x805100b0` 52 **and** `g_slot_rtcab` `0x80510064` 52 as two of the six tables (a
  symbol census, not a bracket claim - and the two being separate 52-byte objects is the fact the whole
  trap rests on); `build_entry.sh`'s key enumerations name both spellings in the same alternation
  deliberately; the runner's comment and `say` and the gate's bracket paragraph name `rtcab` in order to
  say it is **not** the bracket's.
- **The frozen pair is untouched** - `1daaf44e624563694e…` (boot image) and `f202f2465886aba6…` (entry
  image); `./build.sh` not run, nothing under `out/` written, `stages/stage90/` not edited by this step.

## 5. Safety

No device action. Five file edits (`sed`/`grep`/python3 string replacement over four documents and the
README) with every replacement asserted to match exactly once before writing, so no edit could land on an
ambiguous target. `grep -rn` over `docs/` and `stages/stage90/` for the census. Nothing written outside
`docs/experiments/`; no build run; no file under `out/` touched; `flash` not used and nothing written to
storage. Non-persistent `fastboot boot` remains the only device verb this phase uses and this step does not
run it. The device is off the bus and owes a power press before 533 can run.
