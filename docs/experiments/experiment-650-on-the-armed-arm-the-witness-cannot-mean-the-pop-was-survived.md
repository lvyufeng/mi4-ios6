# 650: on the arm that is armed the witness cannot mean "the pop was survived", and the runner had one guarded cell and one unguarded

Two things measured before the press, both in the **reader** — the file that turns the owed run into a
reading. One is a reading defect that a peer found and I verified and repaired; the other is a
sentence in the *pre-registration* that the arm's own reachability contradicts, and it is the one that
would have been carried into the log's verdict.

Everything here is host-side: four `--summarise` A/B pairs over archived captures, one synthetic log
that says in its name what it is, one patch staged and not landed, and reads of two arming scripts.
**No device action, no build, no byte of `out/` changed** — `out/stage90/stage90-qcdt.img` is still
`60063c47…`, still **UNRUN**.

## 1. The witness, and the two mechanisms that produce it

The pre-registered reading for the owed press is the witness: `poll_seq` reaching 3 with
`poll_tmo_max_ms >= 1000`. The sheet's Step 6 draws its conclusion explicitly:

> **`poll_seq` reaches 3 with `poll_tmo_max_ms >= 1000` (witness satisfied).** The idle exit survived
> its `pop`.

**On the arm that is armed, that inference is not available, and the arm's own reachability proof is
what removes it.** 594's switch removes 514's repair, so `cpu_idle`'s gate never opens, so
`platform_cache_idle_exit` is never entered — established three ways and measured again this cycle
(599's fixed point, `tools/check_idle_window_unreachable.py` → *the window is UNREACHABLE in this
image*, and 649's census finding no indirect route). **A `pop` that never executes cannot be
survived.** So on this arm a satisfied witness has exactly one available mechanism: the window was
never entered, and the boot ran past the park without it.

**And that is precisely what the only hardware instance of the witness did.** Measured, counting the
window family in the archived captures (`grep -ac` per key):

| capture | door_seq | repair_seq | sip_seq | pce_seq | wfi_seq | slot_cwe_ | poll_seq values | timeouts (ms) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 513 run 1 | 25 | **0** | **0** | **0** | **0** | **0** | 1, 2, 3, **4** | 5, 40, **2000** |
| 513 run 2 | 25 | **0** | **0** | **0** | **0** | **0** | 1, 2, 3, **4** | 5, 40, **2000** |
| 520 | 16 | 1 | 1 | 1 | 1 | 0 | 1, 2 | 5, 40 |
| 533 | 16 | 1 | 1 | 1 | 1 | **3** | 1, 2 | 5, 40 |

513 is the no-repair regime — 648 §1's point, and the reason the sleeper reinstates it. Its two
captures satisfy the witness (a 2000 ms park returned, twice each) **with every window-family key at
zero**, i.e. the park returned because the window was never entered. 520 and 533, which *did* enter
the window, stop at `poll_seq = 2` with no ask above 40 ms: the third poll is entered and never
returns.

**So the two mechanisms are the two rows of that table, and the pre-registration names the one the
armed arm cannot exhibit.** This is `[[mi4-one-value-two-definitions]]`'s shape one level out from
where the project usually finds it: one observation (a published `poll_seq > 2`), two causes (entered
and survived vs never entered), and the record selecting the cause that belongs to the *other* arm.

**And the reader itself already knew.** Its rung-1 PASS narration, on 513, prints:

> **And the same state has already returned from a park on hardware**: 513's two captures each ran two
> 2000 ms parks to completion with SIGPdisabled set and **the window never entered** … so rung 1's
> shape is a measured outcome of this state and not an extrapolation

So the reader's own text contradicts Step 6's parenthetical on the arm Step 6 is about — and the two
sit in one document set that will be read together, on one log. **The repair is on both sides**: the
reader's rung-1 narration now says, on the sleeper arm, that a return here is *not* the death point
passed (§4, change A); Step 6 is `run-experiment-526`'s lane and this record is the report.

**What is not claimed.** This does not say the sleeper's press is worthless. It says the witness is
not the reading that carries it: on this arm a satisfied witness is a *reproduction of 513 on the
current payload*, which is regression evidence (the payload has moved a long way since 513) and is
**not** evidence about the `pop`. The reading that would carry the press is the one the sheet's Step 6
second bullet already names — the *new stopping point*, where the log stops next — and that is read
off the arm table and the ladder (648), never off the witness.

## 2. The second defect: one cell guarded, its neighbour not

`run_and_capture.sh:1075` opens `if [[ -n $poll_seq_max ]] && (( poll_seq_max > 2 ))`. Inside that
arm, rung 1b is a nested chain opened at `:1109` (`if [[ -n $park_min ]] && (( poll_tmo_max >= park_min ))`,
`elif :1112`, `elif :1119`, `else :1128`); the `$live_trunc` caveat is the **outer** `if`'s `elif`
at `:1134`. So the rung-1b `else` at `:1128-1132` — which prints *"the largest recorded timeout is
… ms, below the park's own threshold of … ms - so the poll that returned is not the park"* — has no
truncation caveat, while the premise it needs (the channel is a property of the **log**, not of a
branch) applies to both exits equally.

**Why it is reachable, and it is one specific route.** `poll_tmo_max=$(maxhex poll_timeout_ms)`
(`:915`) is the maximum over *all* published records, not the third one's. The fixture can publish
only 5 ms, 40 ms and the park's 2000 ms, so on a channel that did not fill, `poll_seq > 2` implies
the third record is the park and rung 1b cannot fail — the cell is a restatement of rung 1 rather
than an independent confirmation. **The one route to it is a cap landing between the park's two
writes**: `xnu_live_poll_seq` and `xnu_live_poll_timeout_ms` are separate `entry_live_write` calls
(`entry_stubs.c:4538` then `:4542`), so a cap between them publishes exactly `poll_seq = 3` with a
maximum timeout of 40 ms.

Reproduced, and the A/B is the demonstration. A synthetic log built from 513 run 1's capture by
rewriting the park's `0x7d0` timeouts to `0x28` and appending ` xnu_live_capped=0x00002000`:

```
live runner, rung 1b:  A poll that is not the park came back, so rung 1's FAIL is rung 1b's: read that line
repaired,   rung 1b:  UNREAD  and whether it is the park is unread: this log's live channel is TRUNCATED, …
```

**Found by `run-experiment-526`, who reported it rather than editing** — the runner is this lane — and
who then corrected their own account of the parentage: the two cells are **not** siblings, the
rung-1b `else` is inside the outer `if`'s `then` arm and the `$live_trunc` guard is on that `if`'s
`elif`, so the caveat is *structurally unreachable from that cell* rather than omitted from it. That
correction is right, and it changes the repair: the fix is a **third `elif` before the `else`**, which
is what §4's change B is.

## 3. One more measured consequence, recorded and not repaired

`run_and_capture.sh:2557` ends the ambiguity refusal with *"Disconnect the other device, or wait for
it to leave fastboot, then re-run — this is a refusal and not a failed run"*. Read as an invitation to
re-run, that sentence has a consequence it does not state, and the consequence is measured from the
arming scripts, not argued:

* `press-watcher.sh:291` prints `say "gate green; booting"` **unconditionally, before** `:292` runs the
  runner — so an ambiguity refusal, which lives *inside* the runner, happens after that line;
* `press-watcher-relay.sh:82` defines `COMMIT_LINE='^\[…\] gate green; booting$'` and `:147-148`
  `spent()` is a grep for it; `:181-192` then prints *"the press has been spent … the relay stops here
  and will never arm again"* and exits.

So **every catcher-fired attempt ends the automatic cover, whatever exit code the runner returns** —
and the only refusal that leaves the cover intact is the **gate**'s own, at `:287-290`, which fires
*before* the commitment line. For an operator: after an ambiguity refusal the phone is fine, nothing
was sent, and the correct next action is a **hand-fired** `run_and_capture.sh --allow-xnu-entry` once
the neighbour is off the bus — "wait for it to leave fastboot" and let the catcher retry is the one
action that cannot work, because there is no catcher any more.

This makes *unplug `33e80afe` first* stronger than a precaution: with the neighbour in fastboot when
our phone appears, the press is refused **and** the cover that would have caught a later attempt is
gone. `run-experiment-526` found this; the measurement above reproduces it from the two scripts.
**Not repaired here** — it is a message on a fired file, so the same 620 hold applies, and the
sentence's device-side claim is correct as written; it is recorded so the consequence travels.

## 4. The staged patch

| | |
| --- | --- |
| patch | `$CLAUDE_JOB_DIR/tmp/r650/650-runner-repair-p0.patch`, also copied to `/mnt/data/mi4-ios6-export/post-press-repairs/` |
| base | `stages/stage90/run_and_capture.sh` sha256 **`9b2b5ba0…`** — the live file |
| product | sha256 **`5318afda877988abb6c61c73d9ce230f057f85a449c9f4bc294bbdf8be8a32cb`**, `bash -n` clean, re-derived in a scratch tree and hashed |
| with 646 | applying 646 **and** 650 in **either order** gives `bc2918930c3ae98a77f360b4ae120e86df521e6c9e0e38e64309ef0b5b4609a0`, `bash -n` clean — the two are independent |
| landed | **no** — `press-watcher.sh:292`/`:296` fire the runner by path, so landing it while the catch is armed changes what the owed press's read says (620) |

Two hunks, both at line ≥ **1078**, both inside `summarise_log()` (`:498`–`:2305`) — so the change
**cannot alter what the press sends**, only what its read says. Same argument, same measurement, as
646's.

* **Change A — the rung-1 PASS narration is qualified on the sleeper arm.** Where the text read
  *"…because the boot dies inside the park's own poll. This is the death point passed"*, it now
  branches: on `idle_no_sleep_arm == 1` it says a return here is **not** that death point passed, that
  this arm's switch makes the window unreachable by construction, that the pop which kills the other
  arm never runs, and that a returning park on this arm is 513's outcome — evidence about the door and
  not about the pop. On any other log the sentence is byte-identical. **It is deliberately a second
  statement of something the ladder already says**: `:985-988` prints *"the window whose pop {fp, pc}
  this phase measures was never entered"* once, in the ladder's preamble, and this is the same fact at
  the point where a reader is most likely to draw the opposite conclusion — the PASS line for the
  witness itself. Two places is the right number here; one of them is the one Step 6 was read from.
* **Change B — a `live_trunc` branch before the rung-1b `else`**, so a capped channel prints UNREAD and
  says why (the seq and its timeout are separate writes, so a cap between them publishes one without
  the other) instead of asserting a park-side conclusion the channel cannot support.

**Verified by running the reader, old vs new, on five logs:**

| log | shape | old vs new |
| --- | --- | --- |
| 513 run 1 | sleeper | **+5 / −1** — the new sentence; on the *synthetic* capped variant of it, change B flips the cell from the `not the park` FAIL to `UNREAD … TRUNCATED` |
| 513 run 2 | sleeper | **+5 / −1**, the same |
| 520 | baseline | **byte-identical** |
| 533 | baseline | **byte-identical** |
| 581-rehearsal synthetic (seam keys **invented**, name says so) | baseline, seam-carrying | **byte-identical** — the seam block does not move, so 535's and 572's arms still read exactly as before |

The two baselines and the seam-carrying log being byte-identical is the part that matters: the change
is invisible on every shape that is not the armed arm, and the arm-specific sentence fires on the
shape the owed press will produce (513's).

**And `tools/rehearse_live_path.sh` with `RUNNER_OVERRIDE` = the mutant: 39 ok, 0 failed** — the
live-path section (20 states, all `ok`) runs the mutant as the live path, the reading section (15)
runs `bash "$RUNNER" --summarise`, so the mutated analyser ran fifteen times, and the path-argument
section (4) resolves a caller's path. The mutant lived at `stages/stage90/.r650-mut.sh`, a name the
catcher never fires (`press-watcher.sh:292`/`:296` run `./run_and_capture.sh`), and it has been
**removed** — `git status` clean, live runner back to `9b2b5ba0…` — which is 620's mechanism and 632's
leftover-mutant hazard both. (The first attempt at this rehearsal was run under a 600 s bound and
killed after its 19th live-path state; the numbers above are from the run that finished, and the
killed run's partial output was not used for any claim here.)

**And the correction landed in the pre-registration the same hour**, by its owner and by
re-derivation rather than on report: Step 6's first bullet now reads *"The idle exit did not survive
its `pop` — on this arm it cannot, because the window that holds that `pop` is never entered"*, cites
`run_and_capture.sh:985-988` (**the reader's own words in the branch this log takes**: *"the window
whose pop {fp, pc} this phase measures was never entered"* — a sharper citation than this section's),
re-counts the four captures itself, and records that the superseded sentence named the other arm.
Their pass also caught a second defect in the same bullet: it wrote `poll_tmo_max_ms`, **which is not
a key in any log** — the published key is `xnu_live_poll_timeout_ms`, and `poll_tmo_max` is step 2's
derived maximum of it. That is *establish that the extractor could have seen it* applied to a name.

## 5. What this does not do

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The OS is still not observed
  booting. It makes the one press's *read* say the right thing about the arm it sent, and it stops a
  satisfied witness from being recorded as something the armed arm cannot produce.
* **It does not land anything.** One patch, staged in a job tmp directory and mirrored to the durable
  export directory with its base hash and the apply line.
* **It does not change the arm, the park, the gate, or any byte under `out/`**, and it does not decide
  which arm the press should send — that is still a choice of question.
* **It does not fault Step 6's structure.** Both of its bullets — the witness and the falsifier — are
  the right two branches; what is wrong is one parenthetical under the first, and only for the arm the
  press sends.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet.

## 6. Safety

Host-side and read-only apart from two files written outside the tree. The device-adjacent reads are
five `--summarise` runs (reads of files on disk), one rehearsal with `sudo`/`adb`/`fastboot` stubbed
first on `PATH`, and greps of two shell scripts. No `fastboot`, no `adb`, no `sudo` against a device,
nothing written to storage; `fastboot boot` only, never `flash`, so no outcome of this step can write
to storage and a brick is impossible by construction. The staged patch is not applied, and the
transient mutant was removed (`git status --porcelain` empty before the commit that adds this file).
Both catchers were alive throughout (relay pid 4067419; catch 2 of 16, pid 1680547, process start
2026-09-24 01:55:07, its own bound 07:58:49).
