# 648: the reader's clauses (1)-(5) are unreachable on the sleeper arm, and the arm table is where a keyless seam gets read

640 §5 found that `run_and_capture.sh`'s clause (5) says one thing about a keyless seam block that has
two causes, and derived the repair from the file: *the arm the predicate needs is already derived in
the same file (`idle_no_sleep_arm`)*. This step re-read that against the **reader's actual control
flow**, on four archived logs, and the repair moved. The predicate belongs in the **arm table** — the
row, not the clause — because on a sleeper-shaped log the clause is not reached at all.

Everything here is host-side: four `--summarise` runs over archived captures, one patch staged and not
landed, one synthetic log that says so in its own name. **No device action, no build, no byte of `out/`
changed** — `out/stage90/stage90-qcdt.img` is still `60063c47…`, entry `696a0f39…`, still **UNRUN**.

## 1. Two conditions that are one chain, and which log takes which

```
run_and_capture.sh:876    if (( idle_no_sleep_arm == 1 )); then                 <- the sleeper ladder
run_and_capture.sh:1291   elif grep -a -q 'xnu_live_door_seq=' "$log"; then     <- clauses (1)-(5)
```

**These are one `if`/`elif` chain, not two independent tests.** So a log that takes the ladder never
evaluates clauses (1) through (5) — and a log from the sleeper arm *is* such a log by definition, since
the arm table's own test (`:826-829`) is `door_seq` present and `repair_seq` absent.

Measured, old reader, `--summarise` on four archived captures. The markers are: the ladder's verdict
line (`did what it was built to do`), a line that only exists inside the clause chain
(`episodes_seen` | `xnu_live_sleh_storm`), and the arm table's own `door_seq` row:

| log | shape | ladder verdict | clause (1)-(5) region | arm table |
| --- | --- | --- | --- | --- |
| 513 run 1 | sleeper | **1** | **0** | 1 |
| 513 run 2 | sleeper | **1** | **0** | 1 |
| 520 | baseline | 0 | **2** | 1 |
| 533 | baseline | 0 | **2** | 1 |

Two directions, and the arm table is the pivot: it prints on **all four**, because it sits at `:832-841`
*before* the chain's head. **513's two captures are the shape the owed press can produce** — 513 is the
image that predates 514's repair and is the record's own worked model of the no-repair regime, which is
what 594's switch reinstates.

## 2. What that does to 640 §5's conclusion

640 §5's sentence about the file is true and stays true: clause (5)'s preamble comment is the only
thing *the file* says about a keyless seam block, and it named one cause where there are two. What
this step corrects is the **operational** half — the inference that the owed log would therefore be
misled *by that sentence*. On a sleeper-shaped log the operator never reaches it. Two mechanisms
produce the same silence about the seam pair, and they are different mechanisms:

* **the `seam_calls >= 1` gate** (clause (5)'s own, `:1752`) — what 646's note measured, and what a
  **baseline**-shaped keyless log hits (520, 533);
* **the arm branch at `:876`** — what the **owed** log hits, one level up, and which skips the whole
  death-shape block rather than one clause's body.

And the reader is not silent about the second one: the ladder already prints it in its own words
(`:989`, *"the whole family clauses (1)-(5) score is absent, and this clause reads what is left"*),
with 513 named as the measured counterexample. What the ladder does **not** say is the thing 640 §4
established: that on such a log the seam pair cannot exist, so 638 §3's `a1`/`b1` table and 642's
`sleh_pc` join are **UNREAD** there and must not be scored as a failed arm.

**So the reachable place to say it is the arm table**, and that is where the repair went.

## 3. A repair that could not reach its own branch, caught by running it

The first draft of the patch added clause (5)'s missing `else` with an inner test:

```
else
  if (( idle_no_sleep_arm == 1 )); then   # <- narrate the sleeper case
```

**That branch can never be taken.** Reaching that `else` means the chain's head was false, i.e.
`idle_no_sleep_arm == 0` — the arm is re-tested inside a branch whose entry *is* the test. It was
found by running the reader over the four logs rather than by reading the patch, and the tell was
that the arm-shaped narration printed on **no input at all**: which is the "cell that cannot reach the
branch its label names" shape this project ranks first to suspect, and the same shape a reader would
have found only if they had thought to ask which log reaches it. The staged version names both causes
in **one sentence** and says which route each takes.

Which is the argument for the predicate living in the arm row rather than in a new condition: a row is
read by every log that reaches the table, and the table is reached by all four shapes measured above.

## 4. The staged patch, and what it is verified against

| | |
| --- | --- |
| patch | `$CLAUDE_JOB_DIR/tmp/646-runner-repair/646-runner-repair-p0.patch` (102 lines, 4 hunks) |
| base | `stages/stage90/run_and_capture.sh` sha256 **`9b2b5ba0…`** — the live file |
| product | sha256 **`e393ac6f…`**, `bash -n` clean; re-derived from scratch in `/tmp` and hashed, so the number is the product and not a claim |
| landed | **no.** It waits for the owed log for the same reason the gate's half does: **`press-watcher.sh:292`/`:296` fire `./run_and_capture.sh` by path**, so landing it while the catch is armed changes what the owed press's read says (620) |

Four hunks: a seventh arm-table count `arm_seam` (reported, and deliberately **not** folded into
`arm_behav`, so the row changes no existing decision); the table preamble's sentence that a key can be
absent by construction *and* inside the window at once; the row itself; and the clause (5) preamble
comment plus its missing `else`.

**Verified four ways, and the fourth is the one that matters:**

* `bash -n` clean; `patch -p0 --dry-run` clean against the live file.
* `--summarise` over the four logs, old vs new: 513 r1/r2 **+5 lines** (the preamble sentence and the
  row, and **no clause (5) line** — it is unreachable, §1); 520 and 533 **+8** (the same, plus the new
  clause (5) narration, which is exactly the case that gate can carry).
* **The seam-carrying case is byte-identical where it counts**: on
  `out/stage90/captures/581-rehearsal/581-SYNTHETIC-574-shaped-log-INVENTED-SEAM-KEYS.txt` — a
  *synthetic* log whose invented keys the name declares, per 562/563 — the seam block's own output does
  not move by a byte; only the preamble and the row are added. 535's and 572's arms are read exactly as
  before.
* **`tools/rehearse_live_path.sh` with `RUNNER_OVERRIDE` = the mutant: 39 ok, 0 failed** — 20 live-path
  states, 15 reading states (that section runs `bash "$RUNNER" --summarise`, so the mutated analyser
  ran 15 times), 4 path-argument states. The mutant lived at `stages/stage90/.r646-mut.sh`, a name the
  catcher never fires, and it has been **removed** — `git status` empty, live runner back to `9b2b5ba0…`
  — which is 620's mechanism and 632's leftover-mutant hazard both.

**And it cannot change what the press sends, only what its read says**, verified independently by
`run-experiment-526`: six hunks in the diff, every one at line ≥ 818, all inside `summarise_log()`
(`:498-2305`).

## 5. Two more things the same measurement produced

* **The runner prints no self-revision anywhere.** `grep -nE '\$0|BASH_SOURCE|revision|self_sha|VERSION'`
  returns only `:79`'s `cd "$(dirname "$0")"` and `:307`'s awk field. `press-watcher.sh:181` logs its own
  sha and `:185` the analyser's, and then `:296` logs a summary produced by whatever
  `run_and_capture.sh` was on disk at that instant — so if the reader changes before the press, the
  watcher's own log cannot say which revision produced its summary, and neither can a re-run. One line
  in the summary header closes it. **Measured by `run-experiment-526`, recorded here, not folded into
  the patch** — it is a different change to the same file.
* **The battery has no state that names the seam at all.** `grep -n 'seam' tools/rehearse_live_path.sh`
  is empty, so 648's verification above is the only coverage the new row and the new `else` have. That
  is a gap in the *battery*, not a defect in the patch, and it is worth stating because "the rehearsal
  is green" would otherwise read as "the new code is exercised".

## 6. What this does not do

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The OS is still not observed
  booting. It makes the one press's *read* say the right thing about the arm it sent.
* **It does not land anything.** One patch staged in a job tmp directory, one `README.md` beside it
  with the apply line and the base hash, and this record.
* **It does not change the arm, the park, the gate, or any byte under `out/`.** The gate is
  `run-experiment-526`'s lane and their half was staged separately; neither patch waits on the other.
* **It does not decide which arm the press should send.** That is a choice of *question* (646 §1): the
  sleeper answers the witness, 574's park answers 638 §3's pair. On the sleeper the pair is not merely
  keyless — it is unreachable, and this step is what makes the reader say so.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet.

## 7. Safety

Host-side only. The only device-adjacent reads are the four `--summarise` runs (a read of a file on
disk), one rehearsal with `sudo`/`adb`/`fastboot` stubbed first on `PATH`, and greps. No `fastboot`,
no `adb`, no `sudo` against a device, nothing written to storage; `fastboot boot` only, never `flash`,
so no outcome of this step can write to storage and a brick is impossible by construction. The staged
patch is not applied. Both catchers were alive throughout (the relay pid 4067419; catch 2 of 16, pid
1680547, armed 2026-09-24 01:55:17, its own bound 07:58:49). `git status --porcelain` empty before the
commit that adds this file.
