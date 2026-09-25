# 680: the runner could not read the arm that ends the run — a key it never looked for, and three sentences about a `pop` that does not run

678's arm publishes the seam pair and then ends the run on purpose. **The runner could not read that.** Its
seam block turns the pair into a cell and every one of those cells is a sentence about a `pop {fp, pc}` that
ran with the word the pair describes — and on this arm the pop never runs, because the seam never returns to
the exit that owns it. So the run 678 is waiting to fire would have produced a log the reader would have read
with the wrong arm's rules: a `CLEAN LINE` read as *"a pop that still died on a stale value got it from
somewhere this operation does not reach"*, when nothing here died at the pop at all.

That is this project's most expensive shape — a designed absence read as a finding — and it is the same defect
677 was spent on one step earlier, one file over. **No device was touched and no press is armed.**

## 1. What was missing, measured rather than argued

`grep -c 'seam_end_run' run_and_capture.sh` was **0** both before and after 678. The key the entry image
publishes (`xnu_live_seam_end_run`, written beside the pair at `entry_trace.c:2442`) had no reader anywhere in
the runner, so on the arm built to end the run:

* the pair was read with the 535 cells' consequences, none of which hold when no pop runs;
* the arm's **own** reading — *the pair was published and then the machine was reset on purpose* — was
  invisible, because the log's shape is the same as any other seam log's;
* and the one cell that this arm can produce and no earlier arm could — **`seam_calls >= 1` with no pair at
  all** — printed `CHANGED … a dirty line, holding something this block does not name`, asserting a dirty line
  from **two absent keys**. On the arms that return, an absent pair and a hang are the same silence; on this
  arm they are the reading.

**And the `seam_op == 0x00000001` branch has never been exercised by a real log.** Measured over every capture
in `out/stage90/captures/`: **no archived log carries `seam_op=0x00000001` at all** — the 650 capture is the
574 park (`op=0`), and 653's acting arm is the one that ended `EXIT 2` with no capture. So this is not a
sentence that used to be right; it is a branch the project has never read a log through, and 678 is where it
first matters.

## 2. The change, and one classification rather than two

The key is read, printed, and used to select which reading a cell gets:

```
if   seam_end_run == 0x00000001   ARM 678: publishes the pair, then ends the run; the pop never runs
elif seam_end_run == 0x00000000   the seam RETURNS: the pop runs, and the pair is read against its death
else                              UNREAD, with its two causes named (see below)
```

The three cells keep 535's **facts** and replace only the **consequence**, because the consequence is exactly
what a pop makes true or false. `STALE LINE, WRITTEN OUT` on this arm is not "the pop read the wrong word" —
it is *the operation put the loop's own datum where the frame's word belongs, the pair was published, and the
run then ended where this project told it to, so the reading survives a machine that would have died at the
pop*. That is 663 §2's whole purpose, stated by the reader instead of left to the operator.

**The cell conditions are written once.** They live in one `if/elif` that assigns `pair_cell`, and the two arms
`case` off that value. A second copy under the ending arm would be one value with two definitions — this
project's most expensive defect class — and the two copies would drift on the first edit.

**A fourth cell was added, and only the ending arm reads it.** `pair_cell=absent` (all four words missing) is
reachable on both arms; on the ending arm it gets its own sentence, and on the returning arm it falls through
to today's `CHANGED` text **unchanged**. That asymmetry is deliberate and is commented as such: every capture
already in `out/stage90/captures/` must read exactly as it did, and the returning arm's own absent-pair
sentence is **owed** rather than invented at the end of a step about a different arm.

## 3. The absence clause, and why it is not a zero

`seam_end_run` absent has **two** causes and this log cannot tell them apart: the image predates 678 (653's arm
and every earlier one), or the machine did not survive to the publish. The clause names both and then gives the
discriminator, which is a fact about the instrument rather than about the log:

> the first call always publishes when the live channel is up — `entry_seam_publish`'s schedule is
> `n <= 4 || power-of-two`, and `entry_seam_flush` ends its body on the same call — so **a pair with no ending
> key is the first cause** (a fact about which image ran) while **`seam_calls>=1` with no pair at all is the
> second** (the arm's own reading).

Measured, not assumed: `STAGE90_SEAM_LIVE_MAX` is `4u` (`entry_trace.c:2307`) and `entry_seam_publish(1)`
returns `1`. It is reported the way 677's `selftest_tick_us` clause reports an absent key — as a fact about the
image, **never** as a zero.

## 4. What was verified, and the strongest form of it

* **All 22 archived captures, old runner vs new, byte for byte**: **18 identical**, and the 4 that differ
  differ by **exactly the seven-line absence clause** — and they are exactly the four logs carrying a seam
  block (`576-rehearsal-OK`, `-OLDEQ`, `-BADWRAP`, and 650's real 574-park capture). Nothing else moved.
  Baseline taken by stashing the change, so the comparison is against `HEAD`'s own runner and not a copy.
* **The four end-run cells exercised, all with exit 0**: clean, stale (`a1 == rtcpre_pop`), changed, absent —
  plus the same four with `end_run=0x0`, and the keyless variants. The `0x0` variants print the *returning*
  arm's own 535 sentence, unchanged.
* **The live-path battery, old vs new**: **20 cells, identical cell for cell** (`0 ok, 20 failed` both ways).
  The battery's red is pre-existing and is **the same single blocker** — every cell stops at the gate, whose
  last printed line is the entry record's `STAGE90_XNU_IDLE_NO_SLEEP=0` before the converse clause refuses.
* **The gate, with the peer lane's one line added to a `.sh` copy**: **exit 0 / 550 lines**, and its census
  still reads **5 exit site(s) / codes `1 2 3`** — this change adds no `exit` and no `die`, so the exit
  contract and the gate's reading of it are untouched. The copy was deleted.
* `bash -n` clean, and no `say` string in the file carries an unescaped backtick (the convention is 18
  escaped ones) — 679's own slip, checked here because a printed string with a live backtick is bash running
  the prose.

**Every hunk of this change is inside `summarise_log`** (`git diff -U0` hunk headers, all seven). That
function is reached only by `--summarise` or after a capture has already been taken, so the live run's control
flow is not touched at all.

## 5. What this does and does not change about the press

It does not unblock it. The press is still blocked by exactly one thing and it is still not in this lane: the
single `STAGE90_XNU_SEAM_END_RUN` name the peer lane must add to `ENTRY_CFG_KEYS`, which 679 measured as one
line taking the gate from exit 1 / 69 lines to exit 0 / 550.

What it changes is what the press would be **worth**. Before this step, 678's arm would have run, ended the run
on purpose, returned, and produced a log whose reader would have described the death of a pop that never ran —
the reading the arm exists to make, reported as its opposite. Now the log's own keys select the arm, and the
one cell that is new to this arm (`seam_calls>=1`, no pair) is named as *the operation did not return* rather
than as *a dirty line*.

## 6. State

**The arm in `out/` is unmoved** (`armed-seam-endrun-88972ba9`, `94c95342...`), the park and the record are
unmoved, **no device was touched, nothing was flashed, nothing was written to storage, and no press is
armed.** **This does not advance 「起码要能进入操作系统，把基础驱动跑起来」** — no boot, no device, no
`fastboot` — so **TWRP to storage stays withheld**.
