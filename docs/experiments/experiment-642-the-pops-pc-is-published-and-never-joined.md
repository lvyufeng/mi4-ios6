# 642: the `pop`'s own pc is published in this log and joined to nothing

Clause (5) of `run_and_capture.sh`'s `summarise_log()` reads the seam pair, and the file's own text
says what one of those two words is: **`b1` is the word the `pop {fp, pc}` takes as `pc`** (`:264-267`,
restated at `:1984-1986`). Clause (1) of the same function reads `xnu_live_sleh_pc`, *the address the
CPU actually jumped to* — which the file's text also identifies as the popped word (547 section 1,
`:1503`: "547 section 1 reads it as the popped word itself, `pc = r11 & ~1`"). Both are locals of one
shell function. **No line of that function compares them**, and both places where the comparison is
wanted ask for it in prose: clause (1) says "Corroborate the shape **by eye**" (`:1501`) and the FAIL
branch says "**read it against** this death's registers (`$sleh_lr`/`$sleh_pc`/`$sleh_sp`)"
(`:1527-1528`). So the two independently published records of the one quantity — what the `pop` loaded —
are printed side by side on every run and never joined.

**And on the arm this step is about, the join that *does* exist is in the branch that arm cannot
take.** The file's only comparison of the pair against a value the pop would load is
`$seam_a1 == "$rtcpre_pop"` at `:2033`, inside `elif [[ $seam_op == "0x00000001" ]]` (`:2026`) — 535's
arm. The frozen 574 park publishes `seam_op = 0x00000000`, so it selects 572's branch (`:2011`), where
`$rtcpre_pop` is read at `:1766` and consumed by nothing. The header's argument at `:264-267` cites
that very comparison as its evidence — "it compares `a1` against `xnu_live_slot_rtcpre_pop` — which is
the second, independent statement of the same fact" — and **on the arm the pair was pre-registered for,
that second statement is not made.**

Nothing was built, booted, edited, re-armed or run. Every number below is a read of the runner, of two
arms' sources and records, and of two archived logs on this host.

## 1. Which word the pop takes, in the file's own words

`run_and_capture.sh:258-267`:

> `b0` is `[seam_sp]`, where the push wrote `fp`; `b1` is `[seam_sp+4]`, where it wrote `lr` … `pop
> {fp, pc}` at `0x8004633c` loads `fp <- [seam_sp]` and `pc <- [seam_sp+4]`, so **`b1` is the word the
> pop takes as pc**, and `b0` is not.

and `:1984-1986`:

> **b1 is the pop's own pc word** (b0=[seam_sp] is the pushed fp, b1=[seam_sp+4] the pushed lr, and
> pop {fp, pc} loads pc from [seam_sp+4]) — so a wrong b1 refutes the frame, and this PASS does not
> clear the pop: all four words are read with SCTLR.C=0, before Apple re-enables caching at
> 0x8004631c, and the pop runs after that with C=1.

The `pop_lr` that clause (1) keys on is the exit's own `bl FlushPoU_Dcache` + 4, read from this tree's
entry ELF at run time, and `pop_death=1` is set by `[[ $sleh_lr == "$pop_lr" ]]` at `:1368`. So the two
clauses are already known to be talking about the same instruction; what is missing is the last step,
from "the same instruction" to "the same value".

## 2. Measured: no line joins them

    awk 'NR>=1200 && NR<=2100 && /sleh_pc/ && /seam_(b|a)[01]/ {print NR": "$0}' run_and_capture.sh
    (no output)

`$sleh_lr`/`$sleh_pc`/`$sleh_sp` are assigned at `:1345-1348` and `$seam_b0`/`$seam_b1`/`$seam_a0`/
`$seam_a1` at `:1762-1765`, both inside `summarise_log()` (which opens at `:498`), so the four values
are in scope together for the whole of clause (5) — the clause already uses `$sleh_sp` at `:1848` for
its own `seam_sp + 8 == sleh_sp` check. The comparison is available and absent.

The whole of `$rtcpre_pop`'s use, for the same reason:

    :1766   rtcpre_pop=$(keyval slot_rtcpre_pop)          # read on every run
    :2033   ... $seam_a1 == "$rtcpre_pop"                 # inside seam_op == 0x1 (535's arm)
    :2034-2043                                            # its three say lines, same branch

## 3. Which branch the window-entering arm takes, from three files

| source | says |
|---|---|
| the park's record (`arm-574-idle-no-sleep-0/xnu_arm_entry-config.txt`) | `STAGE90_XNU_SEAM_POC=0`, `STAGE90_XNU_SEAM_MEASURE=1` |
| the arm's source (`xnu_arm_boot/entry_trace.c:2307`) | `entry_live_write("xnu_live_seam_op", (uint32_t)(STAGE90_XNU_SEAM_POC));` — unconditional inside `#if STAGE90_XNU_SEAM_POC \|\| STAGE90_XNU_SEAM_MEASURE` (`:2219`) |
| the gate's narration for that arm (`out/stage90/captures/574-gate-green.txt:90`) | "`xnu_live_seam_op` publishes SEAM_POC, so this run and a no-seam run both show it at 0" |

So the park publishes `xnu_live_seam_op=0x00000000`, the reader takes 572's branch at `:2011`, and the
pair is scored as "Apple's own `FlushPoU_Dcache` did not write the slot's line back" (equal pair) or
"what changed those two words in memory is Apple's own `FlushPoU_Dcache`" (changed pair) — which is
`experiment-638` section 3's `a1`/`b1` split, correctly printed. **The (A)/(B) call is reached. The
death is not.**

## 4. What is not printed, and what it would decide

On the park's run the log can carry, from one pass through the exit:

| key | what it is |
|---|---|
| `xnu_live_seam_b1` | `[slot+4]` before Apple's flush, read with `SCTLR.C=0` |
| `xnu_live_seam_a1` | `[slot+4]` after it, same `C=0` |
| `xnu_live_slot_rtcpre_pop` | `cpu_data->rtcPop`, the deadline the idle loop computed |
| `xnu_live_sleh_pc` | the address the `pop` actually jumped to |
| `caller_lr` | derived from this tree's entry ELF: where the real exit returns |

Three of those are words that could be `[slot+4]`; the fourth is the value that was. The join prints
which:

| `sleh_pc` | reading |
|---|---|
| `== (b1 & ~1)` | the pop took the word the arm read out of DRAM *before* the flush |
| `== (a1 & ~1)` | the pop took the word as Apple's flush left it — the clean half's write-back is what the pop read |
| `== (rtcpre_pop & ~1)` | the pop took the deadline: 546 section 3's mechanism and 597's spilled word, on the run rather than by inference |
| none of the three | the pop took a fourth value, and all three candidates above are refuted at once |

**Why this is the reading 638 section 3 asks for and cannot get from the pair alone.** Both of 638's
leaves are statements about *memory*: `a1 != b1` says the flush wrote DRAM, `a1 == b1` says it did not.
Neither says anything about *the pop*, which runs with `C` back at 1 (`:1806-1812`), after the L1 has
been invalidated, with the write-back cache under it untouched — which is the whole of 546 section 3's
mechanism. `sleh_pc` is the only published word that speaks to that second half, and it is already in
the log being read. A run that dies at the `pop` prints it.

**One caveat on the relation's form, and it is a two-definitions instance of its own.** The runner's
text states it one way at `:1503` (`pc = r11 & ~1`, 547 section 1) and another at `:2039-2040`
(585: "the pop read `(rtcpre_pop, rtcpre_pop-1)` into `(fp, pc)`"). Both give the same number whenever
`rtcpre_pop` is odd, which is the case in both logs measured below — so neither statement has been
falsified and neither decides the other. Which form the comparison should use is `run_and_capture.sh`'s
call and not this step's.

## 5. The join is retro-validated on both archived fatal logs

Measured on `out/stage90/captures/`, read with `keyval`'s own rule (the **last** publication of each
key — `:1336`):

| log | `xnu_live_slot_rtcpre_pop` | fatal `xnu_live_sleh_pc` | `rtcpre_pop & ~1` |
|---|---|---|---|
| `520-2026-09-22-last_kmsg.txt` | `0x04b79075` | `0x04b79074` (3rd of 3 publications) | `0x04b79074` ✓ |
| `533-2026-09-23-last_kmsg.txt` | `0x33f1c1b5` | `0x33f1c1b4` (2nd of 2 publications) | `0x33f1c1b4` ✓ |

Both hold. Neither log carries a single `xnu_live_seam_*` key (`grep -c xnu_live_seam` returns **0** for
each), which is the other half of why this join has never been made: **no run has ever carried both
sides of it.** The `xnu_live_seam` hits in `574-gate-green.txt` are the *gate's narration* quoting key
names (`:90-92`), not a log.

And the `sleh_pc` these tables take is not a lucky last line: 520's log carries three publications
(`0x000011a4`, `0x800176e0`, `0x04b79074`) and 533's two (`0x80017840`, `0x33f1c1b4`), the abort storm's
own records, and `keyval`'s last-wins rule is what `:1336` says it is.

## 6. What this does not say

- **Not that clause (5) is wrong.** Its 572 branch prints the (A)/(B) reading correctly, and `:258-267`
  is the strongest statement in the tree of which word the pop takes. The gap is one absent
  comparison, not a wrong one — so the cost is a reading left to the eye, not a reading inverted.
- **Not that the pair is unreadable on the coming run.** It is read; only the death-side join is missing.
- **Not a demonstration on a real pair.** No log carries both sides, so this cannot be shown end to end
  until a run does. What is shown is that both sides are published in one log by one function, that the
  relation holds on the two logs that carry one side each, and that the join that does exist lives in
  the branch this arm cannot take.
- **Not mine to edit.** `stages/stage90/run_and_capture.sh` is another session's lane. This is a
  measurement and a message, not a patch: the only files this step writes are this log and its row in
  `docs/experiments/README.md`.

## 7. What would make it structural

1. **One comparison in 572's branch**, beside the pair test at `:2011`: `sleh_pc` against `b1`/`a1`
   (and against `rtcpre_pop`), with the three-way reading of section 4 as its three `say` arms and an
   explicit fourth for "none of them". Without it the file asks a person to do arithmetic on six hex
   numbers from two clauses that are forty lines apart.
2. **The `rtcpre_pop` join moved out of 535's branch, or duplicated into 572's.** The deadline is
   published by the *enter* wrapper's spill (`__wrap_platform_cache_idle_enter`'s `str r4,[sp,#-12]!`),
   not by 535's cleaning operation — so it is a fact about the window and not about the operation, and
   gating it on `seam_op == 1` makes a window fact unreadable on the one arm whose window opens.

This step therefore adds no reader; it records the measurement, so that whichever session edits the
runner next has the two logs, the two branch lines and the three-way table in one place.

## 8. Anchor correction, same day — five numbers, and each landed on a nearby line about the same subject

Every line number in sections 1 to 7 was re-measured after this doc was written, by **printing the cited
line** rather than quoting the number — the check the operator sheet's own correction section records
for two earlier anchors. Five were wrong, and each landed on a nearby, self-consistent line about the
same subject, which is why none of them looked wrong from inside the doc:

| cited | actually | what sits at the cited line |
|---|---|---|
| `:1507` | `:1501` | "Corroborate the shape by eye" is at `:1501`; `:1507` is a clause (1) note about 520 |
| `:1508` | `:1503` | "`pc = r11 & ~1`" is at `:1503`; `:1508` is that same note's second line |
| `:1528` | `:1527-1528` | the sentence is a two-line `say`; `:1528` is its second half, not its start |
| `:2027` | `:2026` | `:2026` is the `elif`; `:2027` is that branch's own comment, one line below |
| `:2012` | `:2011` | `:2011` is the `if`; `:2012` is that branch's own comment, one line below |

The last two are the instructive pair: the `if`/`elif` and the comment naming the arm sit on consecutive
lines, so a citation taken from a listing that dropped the predicate line still points at the right
*arm* and the wrong *line* — one quantity with two readings, the one used silently not the one meant. A
doc that cites a line number is making a claim about that line, and this doc made five wrong ones.

Nothing else changes: the argument, the measured `awk` (still no output), the `rtcpre_pop` enumeration,
the three-file agreement on `seam_op=0`, and both retro-validated log pairs all re-check clean. The
anchors that were re-measured and **hold**: `:258-267`, `:498`, `:826-828`, `:1336`, `:1345-1347`,
`:1368`, `:1466`, `:1523`, `:1565`, `:1698-1705`, `:1752-1753`, `:1762-1766`, `:1806-1812`, `:1848`,
`:1984-1986`, `:2033`, `:2043`, `:2071`, `entry_trace.c:2219`, `entry_trace.c:2307`,
`574-gate-green.txt:90`.
