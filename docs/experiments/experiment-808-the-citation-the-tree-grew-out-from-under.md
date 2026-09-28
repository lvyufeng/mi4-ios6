# 808: the citation the tree grew out from under — the baseline is git, so a moved line is a refusal

**A CHECK, AND IT FINDS THIRTY-THREE STALE CITATIONS IN THE PRESS PATH WHERE 802 §7 HAD FOUND THREE.**
No press, no device action, no runner, no firer, and **no armed byte moves** — `out/` was not written
at all, proved by hash. `tools/check_line_citations.py` is new, `Makefile` wires it into `make check`.

## 1. What 802 §7 owed, and why the obvious check is not it

802 §7 named this and left it: *"A check that every site in `docs/experiments/**` citing a line number
still resolves — not owed by 801 or by this step, but the citation drift above is the second instance in
three steps and it is now named rather than noticed."*

The drift it named: the ladder's CMD2 gate line had moved from `:4785` to `:4964` and the single CMD1
call from `:4702` to `:4808`, and **798 §5, 799 §3 and 800 §10 still named the old numbers.**

**The obvious check is "the citation resolves", and it is wrong twice over**, both measured before a
line was written:

| what the obvious check would do | measured |
| --- | --- |
| refuse when the cited file does not resolve | **2050 of 2950** citations in `docs/experiments/**` name vendor sources (`sdhci.c`, `sdhci-msm.c`, `mmc_ops.c`, `caches.c`, `arm_init.c` …) that are not in this repository at all. A check that refuses 2000 correct citations is turned off within a step (m775) |
| resolve a bare basename to the first file that matches | `build.sh` is `scripts/build.sh` **and** six archived snapshots' copy — an older prototype resolved `build.sh:389` to `archive/stages/stage85/build.sh` (165 lines) and reported it past the end. **That is a `one value, two definitions` defect committed by the checker itself** |

## 2. The baseline is git, because a self-written record is not a constraint

Recording the expected line texts in a file this tool maintains would make it a self-written record —
`mi4-self-written-record-is-not-a-constraint`. **The ground truth already exists: the commit that wrote
the citation.** For a citing file and a token, `git log -p --reverse -U0` walks the history once and the
added lines are exactly the lines a citation is introduced by, so every token in a file is dated at the
price of one call. Then the cited file at that commit, at that line, is what the citation *meant*.

The verdicts, and the asymmetry between them is the design:

| verdict | meaning | what happens |
| --- | --- | --- |
| `SAME` | the cited line reads the same now as when it was written | nothing |
| **`MOVED`** | the text it named is now at **exactly one** other line | **refused** in the live scope, **and the new line is printed** |
| `MOVED-AMBIGUOUS` | the text it named now appears at **two or more** lines | reported — **guessing which one it moved to is the defect this tool exists to catch** |
| `RE-WRITTEN` | the text it named is gone from the file | reported |
| `UNDATED` | no commit changed the token's count in the citing file | reported |
| `SITE-ABSENT` | the cited path did not exist, or the line was past its end, at that commit | reported |
| `PAST-END` | the cited line is past the end of the live file | reported |
| `NOT-IN-REPO` | the cited basename is not tracked here | passed over **by name** |
| `AMBIGUOUS-PATH` | the cited basename is tracked in more than one live root | refused — resolving it is a guess |

`MOVED-AMBIGUOUS` is not a dodge: **147 of `entry_storage.c`'s 4068 distinct line texts occur more than
once**, so it is a real and frequent case — the tool's first cut reported `entry_storage.c:3341` as having
"moved to line 1" on exactly that artifact.

## 3. What refuses, and what only reports

**The refusing scope is the prose that PRINTS FOR THE LIVE ARM** — the `rung_para N` line and the
`entry_conseq+=` lines inside `[[ $wst == N ]]`, for the rung read out of
`out/stage90/xnu_arm_entry-config.txt`. That is what the operator reads at press time, and a stale number
there is a false claim about the tree in front of them. The attribution is structural and is itself
tested: a `rung_para` line belongs to its rung, a commented citation belongs to **no** rung because a
comment never prints.

**Everything else is reported**: the paragraphs for other rungs (which do not print on this arm but would
print wrong if an arm returned to them), the comments, and `records/revert-set.txt` and
`docs/experiments/**` — **historical records, deliberately not rewritten.** That is the split
`check_count_citations.py` already makes, for the same reason.

## 4. The reading, and it is a population rather than three instances

```
check_line_citations: 213 citation(s) over 2 citing file(s)
  the prose that prints for rung 33 refuses (read from out/stage90/xnu_arm_entry-config.txt)
    SAME               22
    MOVED-AMBIGUOUS    12
    RE-WRITTEN          4
    NOT-IN-REPO       142
    MOVED              33
check_line_citations: ok - no citation in the live arm's own prose has moved.
```

**Thirty-three moved citations in the press path**, where 802 §7 had found three by hand — and the
rung-33 paragraph that prints today is clean, which is why the default run exits 0.

**And the refusal path is demonstrated rather than asserted.** `--live-rung N` changes *which paragraph
is examined* and nothing else, so the same tree can be asked about another rung:

```
$ tools/check_line_citations.py --live-rung 31
REFUSED - these citations are in the prose that prints for the live arm, and the
  text each one named is not where it says it is:
  tools/verify_press_ready.sh:1120 cites entry_storage.c:4702 - that line now reads
  something else and the text it named is at :4808
```

**`:4702 → :4808` is 802 §7's own finding, reached independently** — the check reproduces the
hand-measured drift and prints the repair.

## 5. The selftest, and the two bugs it did not catch (both found by running it)

A check whose baseline is git cannot be tested against its own repository without pinning that
repository's history, so `--selftest` **builds one**: a temp repo, two files, three commits, and a cited
line that moves, that is rewritten, and that is duplicated. **15 fixtures**, and both of the tool's own
defects were found by running it rather than by reading it:

- **A fixture was wrong and the tool was right, twice.** The moved line was at `:4` and the fixture
  expected `:5`; and a citation added in a later commit correctly read `SAME` because nothing had moved
  since. **The fixtures were corrected, not the tool** — and the second was rebuilt as a third commit so
  it tests what it claims: a citation added at commit 2, with the line moving at commit 3, must be dated
  to commit 2 or the verdict would be `SITE-ABSENT` instead of `MOVED`.
- **`@@` was the wrong commit marker.** `git log --format=@@%H -p` puts `@@` in front of each commit
  **and a unified diff's own hunk header also starts with `@@`** (`@@ -12,7 +12,8 @@`). The reader took
  hunk headers as commit ids and dated every citation to garbage: the press path classified **920
  `SITE-ABSENT` and zero `SAME`, zero `MOVED`.** The marker is `@@@` now and the collision is named in
  the code, because **zero `SAME` beside 920 `SITE-ABSENT` is the shape of a broken baseline** and a
  reader should recognise it.

## 6. Cost, and why the default is bounded

The first version asked `git log -S <token>` per citation: **one full-history diff per citation**, which
did not finish in ten minutes over the press path. One `git log -p --reverse -U0` walk per *citing file*
answers every token in it, and the press path now takes **2.2 seconds**.

**The default scope is the press path and the bound is a measurement**: the whole tree is 3687 citations
and about eighty seconds, and **a `make check` that takes a minute and a half is a `make check` that gets
skipped.** `--all` runs the sweep. With this check, `make check` is **10 seconds, exit 0**.

## 7. What it does not do

- **It is not a claim that a `SAME` citation is correct.** It says the cited line has not changed since
  the citation was written. **A citation that was wrong the day it was written is invisible to this**,
  and only a reader catches that one.
- **It says nothing about vendor files.** No line of `sdhci.c` is checked, or could be.
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made:
  **TWRP-to-storage stays withheld**, and the next rung is still CMD9's `MMC_RSP_R2`.
- **It does not touch the armed arm.** `out/` was not written: nothing under it is newer than this
  session's start, the park verifies, and **all 11 live members' hashes agree with the record.**

## 8. Owed, and named

- **The 33 moved citations the report names** — they are in the historical record and in paragraphs for
  other rungs, which is why they are reported and not refused. They are paid by the step that returns to
  those rungs, or by a step that decides the record's citations should be annotated rather than left.
- **804's debt is unchanged**: the next rung is **CMD9's `MMC_RSP_R2`**; the rung-33 press's unexplained
  `_post_end_calls` 7→8 / `6.005 s`→`8.009 s` pair; the stale citations in **798 §5, 799 §3 and 803 §8**
  (which the check reports but does not refuse, because `docs/experiments/**` is the historical record —
  **this step gives them a machine-checked number rather than a hand-found one**); `entry_storage.c`'s
  `>= 30` guard; 796 §1/§3/§8's `0x0209` sentences; 797 §1's six 1.200 s attributions;
  `TIMEOUT_CONTROL`'s real scope; and the effective bit rate, 171.5 kHz against rung 6's configured
  400 kHz.
