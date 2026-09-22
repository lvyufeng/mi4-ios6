# 554: the reader called 533's pre-registered prediction a failure

> **Superseded in part by [555](experiment-555-the-death-is-read-from-the-key-and-the-criterion-is-derived.md).**
> The finding and the repair direction stand, and 555 is the same file after the peer session's review
> of the *criteria*: the abort's registers are read from the keys the image publishes rather than from
> the dump's text (520's log holds an earlier, unrelated abort episode whose dump also prints an `lr`),
> the criterion address is derived at run time instead of pinned, the `lr` test's *scope* is asserted
> from `xnu_live_pce_up`/`_ncpu`, and 547 §4's third row is printed as a FALSIFIER rather than left
> inside a PASS sentence. **§1 below - the twenty lines of clause text and the two documents they
> contradict - is unchanged by that**, and is the reason both exist. Kept rather than rewritten
> because this tree already has one pair of same-numbered commits (548) and disambiguating them cost
> more than the duplicate label.

A host-side repair to `run_and_capture.sh`'s verdict block, no device and no build, and it is the same
defect class as 551 - a reading in this phase's own path that was measured against the wrong arm. 551
repaired what *exit 2* measured; this repairs what **clause (1)** and **clause (3)** of
`summarise_log`'s idle-window block measure. Both are in the one file that reads a run's result, and
both were found by reading the block against the prediction instead of against itself.

## 1. The two clauses, and the prediction they contradict

Three writings about the frozen arm disagree, and the disagreement is between a check and two
*pre-registrations*:

| where | what it says about 533's expected log |
| --- | --- |
| `run_and_capture.sh:204-213` (540, `ec95e2f`) | `FAIL  N 'panic ... sleh_abort' record(s) - the death 519 and 520 died is back` … **"The arm's verdict is the panic's ABSENCE"** |
| `run_and_capture.sh:260-277` (540) | clause (3)'s `DIED IN THE EXIT` … **"That is this arm's prediction failed"** |
| `run_and_capture.sh:310-311` (540) | the close: "read the verdict line (1) first, because **the arm's prediction is the panic's absence** and nothing else" |
| [547](experiment-547-the-load-succeeded-and-the-value-is-wrong.md) §4 (`9675e82`, **before the run**) | "the pass reaches the exit, the push runs with `C` = 0, the pop loads the stale words, **the prefetch abort panics** at `pc = the popped value & ~1`, and the device **comes back** on XNU's `MACH Reboot`" |
| [533](experiment-533-the-idle-windows-near-end-is-apples.md) §5.2 (before the run) | `pre` + `rtcpre` present, `post` absent ⇒ "**inside `platform_cache_idle_exit`** - the `pop`" (533 as written said `rtcab`; see its 558 note and [560](experiment-560-the-same-wrong-key-in-four-more-files.md)) |
| [533](experiment-533-the-idle-windows-near-end-is-apples.md) §5.4 | "whether Apple's own panic path runs" - an **item to read**, not a failure |

So on a returning 533 the reader would have printed two `FAIL`s and a "read the verdict line (1) first"
for the exact outcome both documents name as the prediction. **A reader that scores the pre-registered
prediction as the arm's failure cannot test it.**

**And this is clause (2)'s defect one clause over, which was already repaired there.** Clause (2)
asserted 522's pair as the only correct one and would have printed `FAIL ... the re-enable did not
happen` for 533's *correct* image - naming an instruction that is not in it; 540 fixed that and left a
comment calling it "a check whose shape was copied from the previous arm and never re-read against this
one". Clauses (1) and (3) are the same sentence, in the same function, five lines up and forty lines
down.

## 2. What separates the two shapes, and it is a literal off the hardware

`pop_death` - one extraction, next to the block's existing `keyval` set, and the only new mechanism:

```
pop_death=$(grep -a -c 'lr: *0x800462dc' "$log" || true)
```

`lr = 0x800462dc` is read off 520's own register dump, not derived:

```
/tmp/cancro-last_kmsg.txt:4006
r12:  0xde58b701  sp: 0x8054fed0  lr: 0x800462dc  pc: 0x04b79074
```

It is the return address the exit's own `bl FlushPoU_Dcache` pushed at `0x800462d8`, and the two `bl`s
that could overwrite it before the pop - `InvalidatePoU_Icache` at `0x80046304`, `flush_core_tlb` at
`0x80046308` - are **skipped** in this configuration, which 549 read out of this image's own boot-args
rather than assumed. The literal appears **once** in 520's log and once only, at the fatal dump
(`grep -a -c` = 1, against `panic.*sleh_abort` = 1). `lr: *` because the dump's spacing is not uniform
(`r10:`/`r11:` are single-spaced).

**The pin is deliberate, and its failure direction is noisy rather than silent**: an exit that moves in
a later build stops matching, which lands on the FAIL branch and makes a human look. A shape test that
failed *quietly* on a moved address would be this project's most-paid-for class
([[mi4-measurement-defects]]), so the comment in the file says not to "fix" it by loosening the match
to `lr:` alone.

## 3. The change, clause by clause

| clause | was | is |
| --- | --- | --- |
| preamble | "The arm's verdict is the panic's ABSENCE" | "each arm's prediction is a **shape** of death, not the death's absence (547 §4)" |
| (1) | two-way: no panic ⇒ PASS, any panic ⇒ FAIL | **three-way**: no panic ⇒ PASS (naming 547 §4's third row); a panic whose dump carries `lr: 0x800462dc` ⇒ **`PREDICTED`**, `verdict_ok` untouched; any other panic shape ⇒ FAIL |
| (3) `DIED IN THE EXIT` | `verdict_ok=0` unconditionally | `verdict_ok=0` only when clause (1) did **not** attribute the death - otherwise it says the two clauses localize the same instruction |
| close | two-way, "the arm's prediction is the panic's absence" | **three-way**, with the predicted branch first and a `case` that puts 547 §4's prediction beside the arm clause (2) read |

One new variable beyond `pop_death`: `arm_seen`, set where clause (2) already decides the arm
(`522` for `_set` with `C` set, `533` for `C` clear, `unknown` otherwise). It exists for one sentence -
547 §4's prediction is the **enable-off** cell's, and a log whose pair says 522 would be the surprise
rather than the prediction, because 547 §3 has both enable-on rows dying with *no log at all*. The
`case` in the close names that instead of leaving the reader to join the two up.

## 4. Tested against five states before it is spent, as this file's own norm requires

Nothing below touched a device or a build. Each state is 520's real captured log with keys injected
(`/tmp/533-{A,B,C,D,E}.log`, host-side only):

| state | what it is | reader |
| --- | --- | --- |
| A | 520's log + `_cwe_{win,set}` both `C` clear | `PREDICTED` … `ARM 533's arm` … `DIED IN THE EXIT` + agreement … close: "the arm's prediction arrived" + "the two agree" |
| B | A with the dump's `lr` moved to `0x8004c0ff` | `FAIL … the dump does not carry lr: 0x800462dc (matches: 0)` + clause (3)'s disagreement + the failure close |
| C | A with `_cwe_set=0x30c5787d` (`C` set) | `PREDICTED` … `ARM 522's arm` … close: "**522's arm, which 547 section 4 does not predict this for**" |
| D | the fatal panic block removed, `slot_post_calls` injected | `PASS` × 3 (no panic, window open, exit returned) + "547 section 4's third row … 546's mechanism did not fire" |
| E | `_cwe_set` unreadable | `UNREAD` at clause (2) + clause (3) agreement + the `*` arm branch |

Also run: `bash -n`; `--summarise` on 520's **unmodified** log, which prints no idle-window block at all
(the gate on that block is `xnu_live_slot_cwe_`, which 520's image never published); and the gate
read-only, **exit 0**, with its three `exit` sites re-derived at gate time (`run_and_capture.sh:499`,
`:511`, `:521` - the line numbers moved because of this edit, which is exactly why 552 made the gate
compute them rather than pin them).

## 5. What is not changed, and one thing that was already drift

- **The prediction is not changed.** 547 §4 was committed before the run and stays the criterion; the
  reader was the thing stale relative to it (540 predates 547). The edit aligns the check with the
  pre-registration rather than adjusting the pre-registration to the outcome - which is the only order
  in which a prediction means anything.
- **The block's scope is unchanged**: it still prints only for a log that carries
  `xnu_live_slot_cwe_`, so it cannot speak about a run of an earlier image. And clause (1) still only
  greps `panic.*sleh_abort`: a boot that dies of some other panic is out of this block's scope, and the
  close's "three checks pass" is a statement about this seam, not about the boot.
- **The arm caveat in §3 is prose, not a check.** A log whose pair says 522 *and* whose dump carries the
  pop's `lr` is reported as the surprise it is, but nothing *stops* the run being read - which is the
  honest limit, since the reader's job here is to report and the runner's exit code is what decides the
  arm. Recorded rather than papered over.
- **The gate's 526-era narration is drift in the other direction and is not patched from here.** Its
  `--allow-xnu-entry` prose still reads "the arm to run is 526" and "522's block in
  `run_and_capture.sh --summarise` reads this log for it too - `xnu_live_slot_cwe_win` shows C clear,
  `_set` shows it set with `_calls >= 1`". That sentence is *conditionally* right (it is guarded by
  "where the write IS in the image") and 533 is the arm where it does not apply, but the section around
  it describes a spent arm. It is reported to that file's owner with the clause text, the same
  discipline as 551 §4 - not patched across the file boundary.

## 6. Safety

No device action. `bash -n`; five `--summarise` runs over synthetic logs in `/tmp` built from the
already-captured 520 log; one `--summarise` over the real log read-only; one read-only gate run (exit
0). Nothing written outside `docs/`, `stages/stage90/run_and_capture.sh` and the memory files; no build
run; no file under `out/` touched; `flash` not used, nothing written to storage. Frozen pair untouched
(`1daaf44e624563694e…` / `f202f2465886aba6…`), and the device is off the bus awaiting a power press
before 533 can run.
