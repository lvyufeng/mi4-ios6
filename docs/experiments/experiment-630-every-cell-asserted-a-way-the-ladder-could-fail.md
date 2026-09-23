# 630: every cell asserted a way the ladder could fail, and none asserted the way it passes

`run_and_capture.sh`'s sleeper clause prints a ladder whose rungs are **the reading the owed run is
pre-registered against**: the witness is `xnu_live_poll_seq` past the second with the largest recorded
ask at or above this tree's park threshold, and that is rung 1's PASS line and rung 1b's PASS line.
627 gave the ladder's *premises* a record. This step is about the ladder's *verdicts*.

**Every rehearsal row that touched the ladder asserted a way it can FAIL.**

| row | what it asserts | direction |
| --- | --- | --- |
| `door-max-0x8000` | "stops at or below 0x8000" | rung 0's **FAIL** |
| `poll-seq-2` | "no poll record past the second" | rung 1's **FAIL** |
| `small-timeout` | "the largest recorded timeout is 40 ms" | rung 1b's **FAIL** |
| `poll-seq-2-no-arm`, `*--capped` | the UNREAD forms of the same | rung 0/1 **UNREAD** |

and the two rows that *could* have caught a PASS changing - `predicted` and `no-poll-over` - assert the
**arm clause**, `=> this arm did what it was built to do at the point that matters`. That paragraph is
gated on `verdict_ok`, and `verdict_ok` is set inside rung 1's and rung 1b's own branches (and reset
unconditionally to 0 just before them) - so the arm clause is a **proxy that spans exactly two of the
five rungs**. Rungs 0, 2, 3 and 4 are read by no cell at all. This is 613's shape one level up from
where 628 found it: there, a branch nothing *asserted*; here, a whole *direction* of a ladder's verdict.

Measured, not argued: **a rung-0 and rung-3 flip is invisible to all fifteen reading rows.**

Host-side only: one harness edited, one document, one index row, plus transient mutants in the shared
checkout that the run's own trap removes (named below). No boot, no build, no device, no `fastboot`, no
`adb`, **nothing written to storage**. The arm and both parks were hashed and never modified. **TWRP
stays withheld.**

## 1. Which rungs the proxy covers, measured with two mutants

Both mutants are the live runner (`8aad5d12…`) with **one comparison changed**, and both were read on
the harness's own witness fixture log. The reading:

| mutant | change | rung 0 PASS | rung 1 PASS | rung 1b PASS | rung 3 PASS | the arm clause |
| --- | --- | --- | --- | --- | --- | --- |
| rung 1 (`> 2` → `> 99`) | `poll_seq_max` test | - | **gone** | **gone** | - | **gone** |
| rung 1b (`>= park_min` → `>= 999999999`) | the park test | - | kept | **gone** | - | **gone** |
| ladder (rung 0, 2, 3) | `door_max > 32768` → `> 999999999`, `park_group > 0` → `> 999999999`, `[[ -n $park_over ]]` → `&& == 0xDEADBEEF` | **gone** | kept | kept | **gone** | **kept** |

So the proxy is real and it is narrow: rungs 1 and 1b are covered *indirectly*, rungs 0 and 3 are
covered by nothing. (Rung 2 prints its NOTE rather than a PASS on this fixture; rung 4 does not print.)

## 2. The gap, measured across every reading row

The harness's own fixture logs from a completed run were kept, and each of the fifteen
`reader_state` rows' expectation lists re-evaluated against the ladder mutant's output - the same two
loops `reader_state` runs (every positive present, every `forbid:` absent):

**15 of 15 green under the mutant.** The row that should have caught it, `predicted`, is green: its
three expectations all survive a mutation that deletes half the ladder's reading of the very log it
runs on.

The re-implementation is stated as such rather than presented as the harness's own verdict, and the
counted rows from the two full runs are below - because a re-implementation of a check is a claim about
the check (612's rule), which is exactly why the counted runs were still run.

## 3. The repair, and the pair that shows it is the repair

Five expectations were added to the `predicted` row - the witness's own lines, on the same fixture:
rung 0's PASS, rung 1's PASS, rung 1b's PASS, **rung 2's NOTE** (the branch `verdict_ok == 1 && park_group == 0`
that no row reached) and rung 3's PASS. The row now carries eight expectations instead of three, and
the table prints `(+7 more)` - a pass that hides what it checked is the shape this harness exists to
catch.

| run | harness | runner | section A | section B |
| --- | --- | --- | --- | --- |
| the defect | before 630 | ladder mutant | - (not run: section B is where the assertion is) | **15 ok / 0 failed**, cell by cell on the harness's own fixtures |
| the repair | after 630 | same mutant | **20 ok / 0 failed** | **14 ok / 1 failed** - `FAIL  predicted  the reader did not say: went past the last record every previous boot published`, then `REFUSING: the reader has a state it cannot read.`, exit 1 |
| the anchor | after 630 | the real `8aad5d12…` | 20 ok / 0 failed | 15 ok / 0 failed, exit 0 |

The second row is the whole point, and it carries three facts at once: the `predicted` row **is** the
row that goes red; it goes red on the *first* line the mutant removed (the rung 0 PASS - the row reports
the first miss, so rung 3's is not printed); and **section A stays 20/0 under the same mutant**, which is
the counted form of the claim that the run-path cells cannot see the ladder either.

Section C also ran in both: 4 ok / 0 failed, and the four path cells are unaffected by any of this.

The first two rows are the pair that matters: the same mutant, one harness apart, and the difference is
the cell.

## 4. Two of my own measurements were wrong first

**(a) The audit's first run reported two false gaps, and the cause was its own file set.** A dynamic
version of 628's stated audit ("every `forbid:` phrase must be printed by some state, or the absence can
never be falsified") was run over a completed rehearsal's kept artifacts, and it reported that
`adb-unauthorized` and `adb-offline` each forbid a phrase **no cell prints**. Both are false: the phrase
is `device did not appear in fastboot`, and it is printed on **stderr** by
`no-fastboot-after-reboot/err.txt`. The audit's glob was `$K/*/*.err` while the files are
`$K/<cell>/err.txt`, so it searched **35 artifacts instead of 55** and missed every `die` - and `die` is
where this harness's refusals live, so the blind spot was aimed at exactly the states the two rows are
about. Corrected, over **70 artifacts** (35 `.out`, 20 `err.txt`, 15 `reader-*.err`): **zero gaps** - and
the near-miss is `[[mi4-measurement-defects]]`'s own rule arriving in its own audit: before concluding a
value is absent, establish that the extractor could have seen it. The corrected audit's result is
recorded here as a negative finding: the forbid/partner rule is clean today, on all ten forbids.

**(b) The first candidate was a proxy, and it took a measurement to see it.** The hypothesis was "the
ladder's PASS lines are asserted by no cell", which the static grep supported - the five PASS phrases
appear in the runner once each and in the harness **zero** times. But the arm clause is a downstream
*proxy* for two of them, so the hypothesis was wrong as stated for rungs 1 and 1b. It is the two
mutants that separate the two cases, and the step's actual finding is narrower and sharper than the
grep suggested: not "the ladder is unasserted" but "**the ladder is asserted by a proxy, and the proxy
covers two of five rungs**".

## 5. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still the sleeper arm (`60063c47…`), still **UNRUN**, and the press is still the user's. The park at
  `/tmp/r594/frozen-payload` still verifies against `--set=frozen-574`.
* **It does not change the runner.** `8aad5d12…` before and after, asserted at every launch and checked
  after.
* **It does not assert the ladder's *arithmetic*.** The rows assert the *lines the odds run's reading is
  printed on*; the comparison that decides PASS from FAIL is still exercised only by whichever fixture
  the row runs on. A ladder that computed the right verdicts from the wrong numbers would pass - which is
  627's division of labour, and 627's record is what bounds the numbers.
* **It does not mechanise the forbid/partner audit.** That audit was run by hand in section 4(a) and is
  clean; nothing in the tree enforces it, so this remains a rule written down and not a check (602).
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus; the only
  event that can move the goal is the user's **Vol-Down + Power**.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One file
edited (`tools/rehearse_live_path.sh`). Every run went through the harness, which asserts that `sudo`,
`adb` and `fastboot` resolve to its own stubs before any state runs. The mutants were transient files in
the shared checkout - `stages/stage90/.630-mut-ladder.sh`, `.630-mut-rung1.sh`, `.630-mut-rung1b.sh`,
`tools/.630-keep.sh` - named here so a peer who sees one mid-run knows whose it is; the batch removes
them and `git status` afterwards shows only the one intended modification. An aborted run's output is
kept at `/tmp/630-mut-harness.ABORTED.out` rather than discarded silently. `fastboot boot` only - never
`flash` - so no outcome of any of this can write to storage.
