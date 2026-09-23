# 598: the baseline had no lane, and the window's SCTLR had two publishers

594 §5 and 595 §9(b) both found that the idle-window clause block could not read **520** — half of this
project's own baseline — and both said the repair was to route a baseline-signature log into clauses
(1)–(5), and both left it as its own step "because it changes the condition under which five clauses run
and the validation it owes is against 520". This is that step, and the validation found a second thing
worth the whole change: **the cell those clauses read has two publishers in the image, and the reader
was keyed on the newer one**, so 520 was readable all along.

Host-side only: one file (`stages/stage90/run_and_capture.sh`, +150/−43), no build, no device, no
`fastboot`, no `adb`, nothing written to storage. **TWRP stays withheld** — 「如果os已经能进去了的话」
is unmet until a boot says otherwise, and this step does not boot anything.

## 1. The state that had no lane

The clause block is a three-way, and until this step the control flow kept two:

```sh
if (( idle_no_sleep_arm == 1 )); then      # 594's arm, the sleeper
elif grep -a -q 'xnu_live_slot_cwe_' "$log"; then   # <- the old selector
else                                        # everything else, named by one paragraph
```

`xnu_live_slot_cwe_` is published by `entry_window_note`, which **is 533's instrument and not the
arm's signature** — a fact about the *image*, not about the boot. 520's image predates it: measured on
the archived captures, `xnu_live_slot_cwe_*` has **3** occurrences in 533 and **0** in 520, while both
carry `door_seq` = 16 records with `sip_seq`/`pce_seq`/`wfi_seq`/`repair_seq` = 1. So 520 satisfied every
criterion the five clauses are written for and could not enter the block — and the `else` it landed in
described *three* states in prose ("the two signatures are complements … the deciding test is whether
`xnu_live_door_seq` is present at all: its absence means `cpu_idle` was never entered") while the
control flow kept two. The paragraph was right about the test and the code did not take it.

**Measured before and after, on 520's own capture:** `--summarise` printed 112 lines, of which the
clause section was an 11-line `UNREAD` and the census; it now prints **151** lines, and the death is
read — clause (1) `PREDICTED` with `xnu_live_sleh_lr=0x800462dc` and `pc=0x04b79074`, clause (3)
`DIED IN THE EXIT` from the three bracket publishers, and the banner "the arm's prediction arrived".
That is the reading 594 and 595 said the project did not have for half its baseline.

## 2. The widening, and why it is a widening

The selector moves from the instrument to the signature:

```sh
elif grep -a -q 'xnu_live_door_seq=' "$log"; then
```

`xnu_live_door_seq` is published by `__wrap_Idle_load_context` on the earliest passes of every boot
that reaches `cpu_idle`, so the three branches are now the three states the old paragraph named:

| state | test | clause |
| --- | --- | --- |
| the sleeper arm (594) | `door_seq` present, and `repair_seq`/`sip_seq`/`pce_seq`/`wfi_seq` all absent | the ladder |
| either baseline arm | `door_seq` present, the window family present *or not* | clauses (1)–(5) |
| no arm at all | no `door_seq` | the named `UNREAD`, whose census is kept |

**It is a widening and not a re-keying, and that is checkable rather than argued:** every log that took
the branch before still does (`slot_cwe_` present ⇒ the window ran ⇒ `door_seq` was published — the
sleeping arm is the only one that can lack the window family, and it fails the sleeper conjunction
instead), and the gain is exactly the logs that satisfy the *baseline* signature without the pair. The
sleeper branch is byte-identical: the three synthetics that exercise it, plus `syn-G`, `syn-E`, `syn-F`
and the empty log, all produce output `cmp`-identical to the pre-598 runner's, measured by stashing the
file and running both (see §5).

## 3. The second publisher, and why it is not a stand-in

Clause (2) reads the *cell* — which arm's `SCTLR.C` state the window's near end left — from
`slot_cwe_set`. **The same register is read, at the same point in the same wrapper, by a publisher that
predates the pair**, and the source says so in two adjacent statements:

```c
        entry_window_note(win, entry_sctlr());          /* entry_trace.c:1973 -> xnu_live_slot_cwe_set   */
    }
    entry_note_pce_after(entry_tpidrprw(), entry_cpu_datap(), up_style_idle_exit, real_ncpus,
                         entry_sctlr());                /* entry_trace.c:1976 -> xnu_live_pce_after_sctlr */
```

`entry_note_pce_after` publishes through `entry_stubs.c:5610`, and it is old enough to be in 520's
image. Between the two `entry_sctlr()` reads there is `entry_window_note`'s own body — a `.bss` store,
`entry_slot_publish`, `entry_live_ready` and three `entry_live_write`s — and **no writer of `SCTLR`
and no cache operation**, so the two must read the same value. Measured: 533 carries both and they are
**`0x30c57879` both**; 520 carries only the older one, also `0x30c57879`.

So the fallback is used, named, and **checked**: wherever an image carries both keys the block compares
them and prints the outcome in the log — on agreement *and* on disagreement, because a check that
succeeds by printing nothing cannot be told from one that never ran. A disagreement is a `FAIL`: it
would mean one of the two keys is not the register read the source note says it is, and every cell
reading in the block rests on that. This is what keeps the second name from being a second *definition*
of one value — the defect class this project has paid for most often, and the reason the fix is a
comparison rather than a substitution.

Two things this does **not** buy on a 520-shaped log, and the block says so in as many words: the
window's own proof (`slot_cwe_win`, C clear before the near-end write) and the note's pass count exist
only under the pair. So clause (2) reports the *cell* and names the reading it could not take, rather
than a whole pair.

## 4. The three defects the change forced out, and how each was found

1. **The `UNREAD` text described a log that is not this one.** "slot_cwe_calls is not a count >= 1: the
   pair's keys are in this log but the note's own count is not readable" was true while the branch was
   reachable *only* through the pair, and 598's widening is exactly what makes a log with **none** of it
   land there — a reader told its keys are present would go looking for keys the log does not have. The
   branch now says what it is: some key of the pair or the older publisher is present and the count did
   not publish.
2. **The first draft of the new by-construction branch named 520 as its example, and 520 never reaches
   it.** 520 predates the pair but carries the older publisher, so it takes the branch *above*. Found by
   running the draft against 520 — the printed sentence now says "**This is not 520's shape**" in as many
   words, because the next reader will make the same guess. (The branch itself is not dead: `syn-H`, a
   log with `door_seq`, the window family and neither SCTLR publisher, fires it — see §5.)
3. **Clause (1) printed this log's registers as 520's.** "the aborted pc should be a value that is NOT a
   valid address - **520's** is pc=$sleh_pc sp=$sleh_sp at storm=$storm" interpolates the log in hand
   and compares nothing, so on 533 it read "520's is pc=0x33f1c1b4", a value 520's log does not contain
   anywhere (520's abort is `pc=0x04b79074`, as the same file's clause (1) note says). It was written
   when 520 was the only baseline that carried the line, and it became false the moment a second one
   did — the same "a claim about another artifact, written from this one" shape as §1's. Now "this
   log's is".

## 5. Verified on eleven states, and what the oracle is

The two archived captures are **artifacts**; the rest are **shapes** built from them and labelled as
such. For the sleeper branch the oracle is the pre-598 runner itself, run by stashing the file
(`git stash push -- stages/stage90/run_and_capture.sh`, both versions run on the same logs, then
`git stash pop` verified by sha):

| state | shape | before | after |
| --- | --- | --- | --- |
| **520** (artifact) | baseline signature, no pair, older publisher present | 112 lines, clause section = 11-line `UNREAD`, **no clauses** | **151 lines**: (1) `PREDICTED`, (2) cell read from `xnu_live_pce_after_sctlr=0x30c57879` with the window proof named absent, (3) `DIED IN THE EXIT`, banner "the arm's prediction arrived" |
| **533** (artifact) | both publishers present and equal | 154 lines | 157 lines, and the diff is **exactly four hunks**: the reworded clause (1) sentence, the ARM line naming `xnu_live_slot_cwe_set`, the three agreement lines, the rewritten cell sentence in the banner |
| `syn-A/B/C/E/F/G`, empty (594's) | sleeper / empty | — | **`cmp`-identical** to the pre-598 runner |
| `syn-D` (594's) | no `door_seq` | — | differs **only** in the `else` paragraph |
| `syn-H` | `door_seq` + window family, **neither** publisher | — | the by-construction branch fires |
| `syn-I` | 533's shape with the two publishers **disagreeing** | — | the agreement `FAIL` fires |
| `syn-J` | payload output and syscalls, no `door_seq` | — | the `else`, with the census |

So both directions of the new check are exercised (533 agrees, `syn-I` disagrees), both new branches
have a state that reaches them, and the one branch that was *not* supposed to change is proven not to
have changed by byte comparison rather than by reading the diff.

**And the gate, which reads this file:** `preflight_boot_check.sh --allow-xnu-entry` → **EXIT=0**, 518
lines, 0 stderr, and the only difference from the 597 run of the gate is the exit census's three line
numbers (`:1737/:1769/:1779` → `:1844/:1876/:1886`) — same codes, same count, recomputed at gate time,
which is what 584 built it to do. No `exit N` and no `die` was added.

The enumeration of stale claims is by **predicate, not provenance** (560): `grep -rn 'gate on that
block is\|gated on the .xnu_live_slot_cwe_\|gated on the pair' docs/ stages/stage90/` returns five
sites — 554 §2, 555 §2, 564 §6 and the 564 README row in the present tense, and 594 §5 / 595 §9(b) in
the past tense, which stay true as the record of how the gap was found. The three present-tense sites
are annotated rather than left, because each is a statement about *this file's* future.

## 6. What this does not do

* **It does not boot anything and does not change any arm, payload, prediction or the gate's verdict.**
  `out/stage90/stage90-qcdt.img` = `60063c47…` is still the image the next power press sends, and rung 0
  and rung 1 still decide it. This is a reader for a log that *might* arrive, not a step toward one.
* **It does not make the two publishers one publisher.** The image still publishes one register twice
  under two names; the block now checks they agree instead of removing the duplication. Removing it
  means editing `entry_trace.c`, which is a build input (see 597 §7) — the same reason 597's two
  candidate repairs are named and not built.
* **It does not read the window's own proof on a 520-shaped log.** `slot_cwe_win` existed from 522 and
  has no older counterpart, so a log that predates the pair gets the cell and not the proof, and clause
  (2) says which of the two it is reporting.
* **It does not decide 「能进入操作系统」.** The goal block is untouched (596) and its floor/ceiling
  sentences are unchanged; the ceiling is still that the machine stays up across the idle pass.

## 7. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One
host-side file edited (`stages/stage90/run_and_capture.sh`, which is not a manifest *entry* in `xnu_arm_entry-sources.txt`
— that manifest covers `xnu_arm_boot/` by content, and the gate's freshness scan deliberately does not
match `*.sh`); reads of two archived captures, of `entry_trace.c` / `entry_stubs.c`, and of the two
entry ELFs at run time by the clauses' own derivation. Five synthetic logs under `/tmp/r598/`, all
derived from the two archived captures and never written into the repository. The payload and the
parked frozen set are untouched, and the gate was re-run afterwards to confirm the file it reads still
passes. `fastboot boot` only — never `flash` — so no outcome of any of this can write to storage.
