# 591: the second premise with no guard, and the `ok` a doctored image earns

The gate's seam-body section reads `entry_seam_flush` out of the frozen entry ELF and compares what it
finds there against what the entry record claims the arm is. 579 gave each arm a guard on **the other
arm's operation**: the measurement arm refuses if the body calls `FlushPoC_DcacheRegion` (`_POC_N`), and
535's arm refuses if it does not. That pair is symmetric and it is complete *as a pair* — which is
exactly why the fourth one was missing, and why its absence is silent rather than loud.

## 1. The measurement

The section's comment has said, since 579, that **four** record-versus-body disagreements stop the gate:
two are the arms' mutual guard above, and two are the UNREAD properties of this shell. The count was
right and the enumeration was not. Only **two** `fail` calls guard the arm pair — both of them read
`FlushPoC_DcacheRegion` — and the measurement arm's branch carried a **second premise with no guard on
it**: that the body calls `FlushPoU_Dcache` at all, i.e. `_POU_N >= 1`.

That premise is not decoration. It is the *whole meaning* of the arm's two readings. `entry_seam_flush`
reads `b0`/`b1` from the frame before the call and `a0`/`a1` after it, and the value of the pair comes
from what sits **between** the two reads: Apple's own L1 flush, the seam's own `bl FlushPoU_Dcache` — the
call the `ok` line names and the call the arm's one reading rule is about ("with nothing behind the
interception, an **unequal** `b`/`a` pair is that flush writing the line back, and an **equal** pair is
the arm working as designed"). A body in which the interception **passes the call through by a tail
branch** instead of calling it leaves `_POC_N == 0`, so the branch's only test passes, the gate printed
`ok`, and the narration was printed over a run in which **nothing ran between the two reads** — the pair
carries nothing at all, and a *return* would be read as the arm's own reading.

This is 584 §2's shape (a property asserted of an artifact by a comparison that never ran) and 587's (a
clause reading what it could not open), one branch over from the guard the other arm has carried since
579.

## 2. The rehearsal, before the change was trusted

A gate edit that only *adds* a refusal is proved by exactly two things: that it is inert when it should
be, and that it fires when it should. Both were measured, in a mirror of the repository built under the
job's `tmp/` — `repo/` holds symlinks to every top-level entry, real directories for `stages/`, and
**pinned copies** of the three files that matter, so the live tree cannot move under the experiment:
`preflight_boot_check.sh` = HEAD's gate (`38c9c39a…`), `gate591.sh` = this change
(`1ad26147…`), and `run_and_capture.sh` pinned at `a45f6cf1…` — pinned because the gate prints that
file's exit-site **line numbers**, which the peer was moving live (three different values inside three
minutes, `9c1af60a…` → `48b3e0d0…`), and a byte-identity claim needs them fixed.

The doctored image is the real disassembler's own output with **one line deleted** from
`entry_seam_flush`'s body — the shim passes everything through and drops the line

```
8047ca58:	bl	80045874 <FlushPoU_Dcache>
```

Measured: the real stream is **1,253,983** lines, the doctored one **1,253,982**, and the only deleted
line is that one — a pure deletion, exactly the state the new clause is written for ("a body that never
calls the routine"), reached without writing a byte under `out/` and without rebuilding anything.

Four cells, each `--allow-xnu-entry`:

| cell | gate | image | exit | stdout | stderr |
| --- | --- | --- | --- | --- | --- |
| A | HEAD `38c9c39a…` | real | 0 | 483 lines / 36,285 B | 0 |
| B | HEAD `38c9c39a…` | doctored | **0** | 483 lines / 36,285 B | 0 |
| C | this change `1ad26147…` | real | 0 | 483 lines / 36,285 B | 0 |
| D | this change `1ad26147…` | doctored | **1** | 475 lines / 35,679 B | 1,190 B |

- **A vs C: byte-identical.** `cmp` clean over the whole 36,285 bytes. The change is inert on the frozen
  arm — the arm the coming boot is staged on — because `_POU_N` is 1 there and the new branch is not
  entered.
- **B is the defect, reproduced.** HEAD's gate stays **`EXIT=0`** on the doctored image and prints
  `ok: the measurement arm's record and its body agree`, with the sentence "Its **0** bl into
  FlushPoU_Dcache is the wrapper's own call into the real routine" — asserting, in the same breath, the
  call its own number refutes. Four lines differ from A and all four are the count changing value
  (`115`→`114`, `FlushPoU_Dcache=1`→`=0`, `5 bl site(s)`→`4`, `1 spelling`→`0`); every changed token is
  one digit, which is why B is the same **length** as A (36,285 B) and yet differs — byte 34,429. A green
  gate over a narration its own reading contradicts, and the run it clears is one whose pair means
  nothing.
- **D is the refusal.** One `REFUSING:` line on stderr (1,190 B), `EXIT=1`, and the message names the
  number: `does not call FlushPoU_Dcache at all (FlushPoU_Dcache=0)`, states what that costs (the pair
  "carries nothing at all"), says what the clause can and cannot tell ("that the record and the body
  disagree, not which is stale"), and repeats the two standing instructions: disassemble by hand, or
  rebuild with the switch this arm really needs — **and that nothing here rebuilds anything**.

D's stdout is 8 lines shorter than C's for a reason worth stating rather than leaving to be discovered:
the seam-body section is the gate's **last**, and `fail` prints and exits (`preflight_boot_check.sh:50`),
so the refusal drops the `ok` echo, the blank, and the trailing `image:` / `booted, never flashed` lines.
That is the convention all 65 `fail` calls in this gate already follow, and the section list is
unaffected — **all 23 section headers are present in D**, and the diff is inside the last one.

## 3. The repair

`git diff --numstat` is **`18 1`**: fourteen lines of comment above the section (the count corrected to
five, and the paragraph recording why the fourth was missing), and one guarded `if` in
`V_SEAM_MEASURE`'s branch. **No new read**: `_POU_N` was already computed three lines above the branch
(`:2124`) and is already printed. The guard was chosen over rewording the `ok` echo into self-consistent
silence, because the clause's job is to **stop the boot** when the premise it narrates is false, and a
silent reword would leave the next author a claim with no check
([[mi4-a-claim-in-a-comment-is-not-a-check]]).

## 4. What did not move

- `EXIT=0`, **23 sections**, 0 real `UNREAD`, 0 stderr bytes on the frozen 574 arm; the seam-body
  section's own output is **byte-identical** to the pre-edit run, because a guard that is not entered
  prints nothing.
- The whole output diff against the pre-edit baseline is the runner's live line numbers, which is why the
  runner is a pinned input in the rehearsal above and not a variable.
- Frozen artifacts untouched: `xnu_arm_entry.bin` `151425c4…`, `xnu_arm_entry-config.txt` `bcacf065…`,
  `SHA256SUMS.txt` `aeb7862a…`. No rebuild, no `./build.sh`, no edit to any arm.

## 5. Scope

One file, this one gate. The arms, `run_and_capture.sh` and everything under `out/` were read and not
written. The defect is a property of the gate's own comparison, so it is a defect in *this* lane: an arm
whose record and body disagree now stops the boot on **both** arms rather than on one, which is the
minimum this section can owe the run it is gating.

## 6. Safety

No device action, no build, no write under `out/`. The device is off the bus waiting for the power press
and nothing here changes which bytes the next `fastboot boot` would carry. TWRP stays withheld:
「如果os已经能进去了的话」 is unmet.
