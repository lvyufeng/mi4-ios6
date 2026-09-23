# 576: the arm's sp is the slot, and the reader's test for it was one push out

The evaluation criterion 574's clause (5) compared the arm's `sp` against was **wrong by 8 bytes**, and
its wrong form printed `FAIL` for exactly the run the clause exists to score a `PASS`. It was found
host-side, before any boot, by reading 520's real log against the disassembly of this image's exit
wrapper - which is the only way it *could* have been found, because both numbers it needs are already in
that log and the image is in `out/`.

This is a `run_and_capture.sh` change only. `run_and_capture.sh` is not a build input, so the frozen
574 arm was not touched and the gate is green on it after the change (`EXIT=0`, 0 UNREAD, the binding
clause still deriving `0x800462dc`) - 565 section 1's constraint is respected rather than worked around.

## 1. What the clause said, and why it is one push out

574's clause (5) carried:

```sh
if [[ $seam_sp == "$sleh_sp" ]]; then   # PASS: "the address the arm read is the address the pop reads"
```

and the arm's `sp` and the dump's `sp` are **not** the same address. The arithmetic, from three
independent records:

| record | value | what it is |
| --- | --- | --- |
| `platform_cache_idle_exit` | `800462d4: push {fp, lr}` then `800462d8: bl <__wrap_FlushPoU_Dcache>` | the seam's `sp` is what the `push` wrote - the frame slot - and the trampoline is `naked` and three instructions, so it hands that `sp` on unchanged |
| `pop {fp, pc}` at `8004633c`, and 546 section 1 | the push and the pop are **8/8 with no `sp` change between them** | so the `sp` in the abort's dump is the *post-pop* `sp` = pre-push `sp` = **slot + 8** |
| the entry wrapper's own capture | `8047c964: str r4, [sp, #-8]!` … `mov %0, sp` | the capture is taken **after** the prologue, so `xnu_live_slot_pre_sp` is the `sp` the real exit is *entered* with - also **slot + 8** |

And 520's log carries both of the last two under different names, with the same value:

```
xnu_live_slot_pre_sp=0x8054fed0      (the exit wrapper's capture)
sp: 0x8054fed0                       (the fatal abort's dump, same run)
```

So the slot is `0x8054fec8`, and a correct arm's `seam_sp` is `0x8054fec8` against a `sleh_sp` of
`0x8054fed0`: **the equality test would have printed FAIL for a correct arm**, and its FAIL text ("the
arm's slot and the pop's are different addresses") would have been a false statement about a good run.
The class is the project's most-repeated one - a comparison whose two sides are *different objects* with
a constant offset - and what makes it this time is that the two objects are one `push` apart in the same
frame.

## 2. The repair, and the second publisher it now reads

The test is the relation, not an equality, and it is checked **against both publishers of the slot's
address** rather than against one:

```sh
(( seam_sp + 8 == sleh_sp ))       # the abort's own register dump
(( seam_sp + 8 == slot_pre_sp ))   # the exit wrapper's own capture
```

Two relations and not one because they are two independent records of one value: a check that consumed
both would let one silent key hide the other, and the failure of the two to agree is itself a reading
(the wrapper's capture and the arm's frame disagreeing would mean the arm read a different object than
the wrapper's frame - a fact neither number states alone). Measured in three states, on 568's real log
with only the arm's keys appended:

| variant | `seam_sp` | `sleh_sp` | `slot_pre_sp` | measured |
| --- | --- | --- | --- | --- |
| OK | `0x8054fec8` | `0x8054fed0` | `0x8054fed0` | both PASS |
| OLDEQ (what 574 would have compared) | `0x8054fed0` | `0x8054fed0` | `0x8054fed0` | **both FAIL** - i.e. the old criterion failing a good arm |
| BADWRAP | `0x8054fec8` | `0x8054fed0` | `0x8054fec0` | first PASS, second FAIL - the two publishers disagreeing is caught |

The FAIL texts print hex on both sides (`printf '0x%08x'`), because the first version printed the
expected address as a **decimal** integer beside hex ones - the same readability defect 515's clause has
carried since it was written, and one that makes a misread more likely at exactly the moment someone is
reading a fresh failure.

## 3. And the log already carries the slot's *contents*, under a name nobody had joined up

`xnu_live_slot_pre_m8` and `_m4` are the words at `sp-8` and `sp-4` at the exit wrapper's entry - i.e.
**exactly the two addresses the exit's own `push` is about to write**. On 520's run:

```
xnu_live_slot_pre_m8=0x80553520      the word at the slot
xnu_live_slot_pre_m4=0x8047c974      the word at the slot+4
```

and the same run's pop read `{0x33f1c1b5, 0x33f1c1b5}` (both registers in the dump, and
`xnu_live_slot_rtcpre_pop=0x33f1c1b5` - the deadline the exit wrapper published in that same pass).

> **Correction (585, host-side): that sentence's numbers are 533's, not 520's, and the claim it makes
> about the two registers is false of 520 in both halves.** `0x33f1c1b5` occurs **zero** times in
> `out/stage90/captures/520-2026-09-22-last_kmsg.txt`; it is `533-2026-09-23-last_kmsg.txt` that carries
> it (its abort dump has `r4: 0x33f1c1b5` / `r11: 0x33f1c1b5` / `xnu_live_slot_rtcpre_pop=0x33f1c1b5`).
> 520's own dump reads `r11 (fp): 0x04b79075` and `pc: 0x04b79074` - **not equal to each other**, and the
> `pre_m8`/`pre_m4` values quoted above are the ones that are 520's. The pairing is in the source already
> (`entry_stubs.c:6491` writes `rtcpre_pop = 0x04b79075` against `pc = 0x04b79074`), so the code had the
> right pair while this document had the wrong run's. See
> `experiment-585-one-value-two-runs-and-the-pair-the-pop-actually-read.md` for what the corrected pair
> shows - and it is a stronger reading than the one above, because it reproduces across both runs.

So 520's log holds **both sides of the mechanism measured on one pass**: the frame the push wrote, and
what the pop actually got. The clause now prints the pair beside the arm's own `_b0`/`_b1`, as a
*reading* and not a criterion: they are equal when the frame did not move between two passes through the
same code, and a difference is a fact about the idle loop rather than about the arm.

**One premise of 546 section 1 is not what this pair shows, and it is recorded rather than resolved.**
546 section 1 reads the frame's second word as naming `cpu_idle`; 520's `slot_pre_m4` is `0x8047c974`,
which is inside `__wrap_platform_cache_idle_exit`'s own extent (`0x8047c964..0x8047c9c4`) and not in
`cpu_idle` (`~0x8000cc..`). Two things could explain it and this step does not decide between them: the
capture is taken *before* the push, so those words are the *previous* occupant's, or the value at that
address is not the exit's `lr` at all. It is a question the next log answers directly, because 574's arm
publishes `_b0`/`_b1` - the same two addresses *after* the push.

## 4. Safety

No device action: one reader edit, one read-only gate run (`EXIT=0`, 0 UNREAD), and three rehearsals
against a log in `/tmp`. Nothing was flashed, nothing was written to storage, no build input was touched,
and the frozen 574 arm is unchanged (entry `151425c4…`, `stage90-qcdt.img` `914f45ac…`) - **the boot
still waits only on the user's power press**, and this fix is what makes its `sp` line readable instead
of falsely failing.

## 5. State

- Owed before the next boot: nothing on this side. The gate is green on the frozen arm; the peer session
  registered the second seam switch in `575` (`b3121a9`).
- Parked: `out/stage90/captures/574-*` (the arm) and `576-*` (this step's gate run and rehearsals, under
  `captures/` beside it).
