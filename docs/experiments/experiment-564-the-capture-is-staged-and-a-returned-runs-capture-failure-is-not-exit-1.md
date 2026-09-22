# 564: the capture is staged, and a returned run's capture failure is not exit 1

A host-side repair to `run_and_capture.sh`, no device and no build. Three defects in **step 5**, the
one step that runs after a boot has already been spent, plus the change the peer session asked for in
562 §5. All four are in the same step for the same reason: **step 5 is the only place in this project
where a mistake costs a run that cannot be repeated, and its failure path had never been exercised.**

> **Numbering, and what it collided with.** This step was written as 563; it is 564 because the peer
> session opened a 563 of its own in the same window - *the gate saves a copy of the log, because step 5
> destroys it* - and their file existed first. **Their §1's premise is what §3 below repairs**: at the time
> they wrote it, `rm "$LOGFILE"` really did come before the capture, so a failed capture really did destroy
> the phase's only copy of the 520 death. It no longer does (the capture is staged, and step 2b parks the
> previous log), so their copy is now a second, independent belt rather than the only one. Read the two
> together, in that order.

The frozen pair was not touched: `1daaf44e624563694e…` / `f202f2465886aba6…`. The gate runs
`--allow-xnu-entry` to **exit 0** on this tree.

## 1. Exit 3 already meant "the run returned and no log was captured", and step 5 called it exit 1

`run_and_capture.sh`'s own header defines the codes, and it defined exit 3 before this step existed:

```
#   3  the device returned to the HOST but was not capturable over adb - the host's USB log
#      saw the phone enumerate again (and its SoC is running) while `adb devices` stayed
#      empty. **This is not a hang** and must not be read as one ...
```

Step 5, reached only when the device *did* return, did this:

```sh
  sudo adb -s "$SERIAL" exec-out 'cat /proc/last_kmsg' > "$LOGFILE" || \
    die "could not read /proc/last_kmsg"        # die() is exit 1
```

and exit 1 is documented as *"the gate refused, or the device was not found"*. So the state the header
defines as 3 - the device came back and the capture did not happen - was reported as the code that means
the device was never found. That is **one state with two definitions, in the file that defines them**,
which is this project's most-repeated defect class ([[mi4-one-value-two-definitions]]); it was found by
reading the header against the body rather than by a run, because the failure path had never run.

## 2. The one attempt was made in a window in which it cannot succeed

Section 4's return criterion is satisfied **the moment the host's USB log shows the phone enumerating**,
by serial. adbd is not up then, and not for a while:

| event | the gap | source |
| --- | --- | --- |
| fastboot (`18d1:d00d`) gone → the jump into XNU | 3 s | 542 §1, `02:00:56` → `02:00:59` |
| → `2717:0368` (Android's early gadget, serial `4a2fe00b`) | **17 s** | 542 §1, `02:01:16` |
| `2717:0368` → `18d1:4ee7` (**the configuration that carries adb**) | **20 s** | 522, `14:13:49` → `14:14:09` |

So on the *normal* path - the one 547 §4's row (i) predicts for a run that returns - the host-log
criterion fires 20-40 s before `adb exec-out` can work, and step 5's single immediate attempt fails into
`die`. **The repair is a bounded second window**, `CAPTURE_WAIT` (default 90 s), because a read costs
nothing and cannot spend a run: the capture is retried every 5 s and only its exhaustion is a verdict.
The two waits are not the same window and the comment says so - section 4 is bounded by the return,
this one by adbd's bring-up.

## 3. Found by writing §2 down: a failed capture destroyed the previous log

Step 5's order was **remove `$LOGFILE`, then redirect adb into it** - and the `rm` is load-bearing (the
sticky-`/tmp` identity problem 511 and 512 measured). But the retry loop of §2 makes the consequence
visible: with a failed capture, the *previous* run's log has been deleted and replaced by nothing.

That file is not nothing. The gate fingerprints it immediately before the boot (562), and 547 §4's
prediction was **read from the bracket it carries** - it is the only copy of the 520 death. Destroying it
on a failed attempt would spend the run, leave the gate's own before/after test unanswerable, and destroy
a measurement.

**The capture is now staged through `$LOGFILE.new`, and only a non-empty result is moved into place** -
so the removal of the destination happens at the moment a replacement exists and not before. A failed
capture now leaves the previous file byte-identical, which is also what makes the gate's test say the
true thing (*no capture happened*) rather than "the file is gone".

**The same defect was in the re-read loop, one turn sharper**: it exists to *repair* a capture that
looks wrong, and it wrote into `$LOGFILE` before knowing the read worked - a repair that can destroy the
thing it repairs. It is staged the same way, and on a failed re-read it now *keeps* the bytes it has and
stops, instead of reading a third time into the same hole.

**The attempt count in its closing message was a literal that the loop's own path could falsify.** It
said "after three reads" - true of the loop's bound, false of a run that broke out early on a failed
re-read, which §3 just made reachable. It now prints `_attempt`, the iteration the loop actually stopped
on. Same shape as 550 (the hardcoded number removed, the hardcoded *test* kept), one layer down.

### Tested with the real block, four states, isolated harness

The step-5 body is extracted from the committed file and sourced, so the test runs the real code rather
than a copy of it. `sudo` is a stub serving `rm -f` and `adb`; `FAKE_FAILS`/`FAKE_FEW`/`FAKE_FAIL_AFTER`
drive the states.

| state | adb | result |
| --- | --- | --- |
| A | never reachable | **exit 3**; the previous log's sha256 is **byte-identical**; no `.new` left behind |
| B | fails twice, then works | capture replaced, 10 payload lines, no `.new` |
| C | capture ok but 1 payload line, re-read works | re-read repairs it, 10 lines, no `.new` |
| D | capture 1 line, every re-read fails | keeps the 38 bytes it has (not truncated to 0), stops, says so, and the count prints **2** reads rather than 3 |

## 4. The peer's 562 §5 request, done the way that makes it structural

562 §5 asked, as "noted rather than crossed": have the runner record the log's identity before the boot
so the before/after comparison stops being a human's. **The runner now parks the file instead of
recording its hash** - step 2b, between "device" and "boot":

```
parked the previous run's log: /tmp/cancro-last_kmsg.txt -> /tmp/cancro-last_kmsg.txt.prev
```

That is strictly better than a fingerprint for the purpose, and the reason is what the run has to decide
afterwards. With the hash recorded, the test is *"did the sha change"* - and a run that reproduced the
previous log byte-for-byte would be indistinguishable from a failed capture. With the name **vacated**,
the test is *"is there a file at all"*, and the previous bytes cannot be mistaken for this run's whatever
they say, including if they are identical. The gate's fingerprint is still printed and still useful (it
is the operator's independent record, and it is what identifies the parked file); it is no longer
load-bearing.

Three properties worth naming, all tested (states E-H):

- **nothing is destroyed.** The park tries `$LOGFILE.prev`, then `.prev.2`, `.prev.3`, … so an earlier
  parked log is never overwritten. State H: with `.prev` already present, the new park went to `.prev.2`
  and *both* survived.
- **it is a `mv`, not a delete**, so the previous log - today the only copy of the 520 death - stays on
  disk under a name that says what it is, and step 5's exit-3 message now names that path instead of
  telling the reader "`$LOGFILE` is untouched, so it is the run before this one" (which was true before
  this step and is now the wrong sentence: after a park, `$LOGFILE` on a failed capture is *absent*).
- **if the park fails** (sticky `/tmp`, an identity that does not own the file: 511, 512) the run is not
  refused - it is not spent yet - but the fall-through is *said out loud* with the one comparison that
  then still applies. Silence here would have re-created the ambiguity this step exists to remove.

State E, end to end: park → capture never works → **exit 3**, `$LOGFILE` absent, `$LOGFILE.prev` holding
the previous bytes with an unchanged sha256.

## 5. The exit sites moved, and the gate recomputes them

Step 2b is 46 lines above section 4, so the wait section's three `exit` sites moved: 559's gate run
printed `:644`/`:656`/`:666`, and the gate now prints

```
    run_and_capture.sh:700   exit 3
    run_and_capture.sh:712   exit 2
    run_and_capture.sh:722   exit 2
```

Nothing needs editing for this - **552's rule is that the gate reads the sites out of the file at gate
time**, so the numbers above are a snapshot of *this* revision and the gate is not. It is recorded here
because a document that quotes line numbers is a document that goes stale silently, and the count
(`3 exit site(s), distinct code(s) 2 3`) is the part that matters: **no new code value was added**.
Section 5's new `exit 3` is outside the region that clause reads (it spans section 4 to section 5's
header), and 3 is already in the gate's `READ_CODES`, so the gate's refusal of an un-narrated code
neither fires nor should fire. What the clause now slightly understates is its *scope* - "that section"
is section 4, while exit 3 has two producers - and that is left to the gate's owner (the peer session)
as a note rather than patched across the boundary, the same discipline as 558 §5.

## 6. What the reader does on a real log, and why it is silent on 520's

Worth recording, because it was noticed while smoke-testing this step and could be misread as damage:
`--summarise /tmp/cancro-last_kmsg.txt` prints **no clause block at all**, only the marker table. That is
the block's design, not a regression: it is gated on the `xnu_live_slot_cwe_*` pair, which
`entry_window_note` publishes only in images built since 522, and 520's log carries no such key
(`grep -ao 'xnu_live_slot_[a-z_]*=' ` over it lists 27 keys and none of them is `cwe_*`). **533's image
does carry the pair**, and a 533-shaped log of nine keys runs the whole block correctly on the current
file - PREDICTED (the pop's return address), PASS then `ARM 533's arm` on the pair, DIED IN THE EXIT on
the bracket - which is the smoke test that this step's edits did not disturb `summarise_log`, which it
does not touch. The same log confirms the key fact 547 §4 stands on: `tail -1` of
`xnu_live_sleh_lr` in 520's capture is `0x800462dc`, the exit's own `pop {fp, pc}`.

## 7. Safety

No device action. `bash -n`; one `--dry-run` (exit 0, which runs the read-only gate); one
`--summarise` over the real capture and one over a synthetic nine-key log; a gate run
`--allow-xnu-entry` (**exit 0**); eight harness states (A-H) against an extracted copy of the edited
block. Nothing written outside `stages/stage90/run_and_capture.sh`, `docs/` and the memory files; no
build run; no file under `out/` touched; `/tmp/cancro-last_kmsg.txt` **read and never written** (the
step-2b `mv` is the only thing in this step that can move it, and it is inside the boot path, which was
not run). `flash` not used, nothing written to storage, frozen pair untouched
(`1daaf44e624563694e…` / `f202f2465886aba6…`), and the device is off the bus awaiting a power press.
