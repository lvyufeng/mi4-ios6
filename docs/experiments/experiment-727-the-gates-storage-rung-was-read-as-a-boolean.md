# 727: the gate's only narration of the storage rung read it as a boolean — and the key had been a ladder for eleven rungs

Host-side, in `stages/stage90/preflight_boot_check.sh` (this lane's file). **No device action, no build,
no byte of `out/` changed** — `out/stage90/stage90-qcdt.img` is still `c9d7ba6c…`, the rung-13 arm
`armed-storage-instant-e8dc64f7`, and its mtimes are untouched (verified at the end of the step).

## 1. The defect, and why nothing caught it

The gate PRINTS `STAGE90_XNU_STORAGE_PROBE` (`:619`, in `ENTRY_CFG_KEYS`) and, until this step, never read
its value. The only prose about it was the 692 note at `:590-598`, and it read the key as a **boolean**:

> What this key buys a reader is the one thing the arm's other keys cannot say: **with it at 1** the run
> dereferences a device block the image has never touched … and **with it at 0** the arm is 690's clock arm
> with an object in the link whose body compiles to nothing.

That was true when 692 wrote it — the key was a flag then. It is not true now. The armed record says:

    out/stage90/xnu_arm_entry-config.txt:22: STAGE90_XNU_STORAGE_PROBE=12

and the ladder's own guard refuses anything above 12 (`xnu_arm_boot/entry_storage.c:115`). So the one
narration an operator reads to learn **what the storage arm will do to the device** described the
read-only probe about an arm that issues the driver's own commands and makes 32-bit stores to a
controller's interrupt-status register.

**The reason it survived is the whole point, and it is this project's oldest shape.** Nothing ever
contradicted it: a comment has no check behind it, the build does not read the gate, and every arm that
raised the rung raised the *record's number* while the prose stayed where it was. Eleven rungs went by.
`[[a claim in a comment is not a check]]` — and the class it is an instance of, one quantity with two
readings, the prose taking the boolean while the record means the rung, is
`[[one value, two definitions]]`.

**It is not only a stale sentence.** Two of the keys are of this kind — `STAGE90_XNU_STORAGE_PROBE` is a
rung and `STAGE90_XNU_PWR_WAIT_TICKS` is a budget — and neither was in the `0|1` value test either, because
that loop (`:667-704`) is written for the eight **switches** and neither key is one. So both were printed
and neither was bounded: a record carrying `STORAGE_PROBE=99` passed every clause of this gate.

## 2. The repair, and what it deliberately does not do

**The bound is READ, not restated.** Both numbers have exactly one definition — the
`#if <KEY> < lo || <KEY> > hi` guard in `xnu_arm_boot/entry_storage.c`, the same line the compiler refuses
on — so the new clause parses that line:

| key | its own guard | bound the clause reads |
| --- | --- | --- |
| `STAGE90_XNU_STORAGE_PROBE` | `entry_storage.c:115` | `[0, 12]` |
| `STAGE90_XNU_PWR_WAIT_TICKS` | `entry_storage.c:140` | `[1, 19200000]` |

**That is the repair rather than a style choice.** A rung list restated in the gate is a list that goes
stale in the gate, silently, exactly as this one did — and this file has that defect recorded four times
over already (683, 686, 690, 692, each `+1` line to a hand-kept list). After this step the gate picks up a
rung the ladder grows without an edit here, and a bound it **cannot** read is a **refusal** rather than a
pass, which is the file's own rule for a missing dependency (the `tools/check_storage_refs.py` clause).

**The value is also printed, with its bound and with the one place its meaning lives** — so the operator
takes the gate's own reading to the log instead of a rung number whose meaning they have to remember, and
the gate does not restate the rungs:

    == how far up the storage line this arm goes, read against the ladder's own guard ==
      STAGE90_XNU_STORAGE_PROBE=12  (within [0, 12], the bound its own `#if` guard declares in xnu_arm_boot/entry_storage.c;
          what that number DOES is spelled out at that guard, in the `#error` the build refuses on -
          this gate prints the value and the bound and deliberately does not restate the rungs)

## 3. Measured, both directions, through the gate itself

A sandbox was built at `/mnt/data/mi4-ios6-gate-ab-727/` — a hard-linked copy of `out/stage90`, `tools/`
and `stages/stage90/` (same filesystem, so the links are real), with the gate under test copied in and the
link to the live gate removed first. **Hard links, never writes through them; the live tree was verified
unchanged afterwards.** Every row below is a whole-gate run.

| # | the record in the sandbox | exit | what refused |
| --- | --- | --- | --- |
| A | unmutated (`STORAGE_PROBE=12`) | **0** | nothing — 568 stdout lines, 0 stderr, and the `== how far up … ==` block above is printed *by the new clause*, so the success direction is a read and not an assumption |
| B | `STORAGE_PROBE=13` | 1 | the new clause: `=13 (outside [0, 12], the bound its own guard declares)` |
| C | `STORAGE_PROBE=abc` | 1 | the new clause: `=abc (not a whole number)` |
| D | `PWR_WAIT_TICKS=0` | 1 | the new clause: `=0 (outside [1, 19200000], …)` — the ladder's own floor, which refuses the rung-8 no-wait arm's value on purpose |
| H | a second `STORAGE_PROBE=` line | 1 | the new clause: `defines … more than once` |
| I | the guard line deleted from `entry_storage.c` | 1 | the new clause: `could not read the ladder's own bound for …` |
| F | the `STORAGE_PROBE=` line deleted | 1 | the **`ENTRY_CFG_KEYS` loop above** (`:627`), *not* this clause |
| A′ | A again, after every mutation was reverted | **0** | nothing — the sandbox is self-consistent, so the mutations are the only cause of B–I |

**Row F is the step's second finding, and it is about this step's own first draft.** The draft had a
`_rungmiss` clause for an absent key. F would not let it fire: the `ENTRY_CFG_KEYS` loop above already
reads every key with `awk` and fails on an empty value, so a missing line and an empty one are both gone
before the new clause runs. A second refusal for the same state is a branch no record can reach —
`[[a cell that cannot reach the branch its label names]]` — and it was **removed** rather than left looking
like coverage. Duplication is a different state and this clause does own it, because that loop tests
`grep -c` only for the eight switches (row H).

**Two integrity properties were re-derived rather than assumed**, because a gate edit can quietly break
them:

* **The revert-set derivation is unmoved.** `revert-set.txt` derivation A greps the gate for
  `$OUT/…` / `out/stage90/…` literals. The extraction on this file and on `HEAD`'s is **byte-identical**,
  so no recorded set changed meaning. The new clause names its source as `$STAGE_DIR/xnu_arm_boot/…`,
  which is not a path either pattern matches, and that was checked rather than hoped.
* **The refusal census moved by exactly the count of new refusals**: 70 `fail "` sites at `HEAD`, **73**
  now (four added, one dead one removed). Nothing in the tree asserts that count; the check was made
  because a census is the kind of thing this project records in prose and then outgrows.

**And the operator's own tool agrees.** `tools/verify_press_ready.sh` — which runs the edited gate as its
third check — returns **5 of 5, exit 0**, the gate row reading *"exit 0 under `--allow-xnu-entry` … the
entry sources match the manifest by content, the image carries the arm the entry bin holds, and the record
binds that bin"*. The gate's own output grew 561 → **568 lines** on the same tree (HEAD's gate run in the
same sandbox).

## 4. What this does not do

* **It does not advance 「把基础驱动跑起来」.** It makes the gate tell the truth about a rung it had been
  describing as a boolean; it boots nothing and builds nothing.
* **It touches no byte the press sends.** `out/` is unchanged (`stage90-qcdt.img` `c9d7ba6c…`, the entry
  record `fdcdc15a…`, mtimes 02:53/02:54 as before), no arm is re-armed, and the payload is untouched.
* **It does not decide anything about rung 14.** 726 §3's three-part next rung is still the next rung; it
  is the peer lane's file that carries it, and it is not built here.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet.

## 5. Safety

Host-side and read-only apart from one file inside the repository. The sandbox is a hard-linked copy on
the same filesystem; the gate was exercised against it and never against the live `out/`, and the live
`out/` was hash- and mtime-checked afterwards. No `fastboot`, no `adb`, no `sudo` against a device,
nothing written to storage; `fastboot boot` only, never `flash`. **No firer is armed** (723's press ended
`done. gate=0 runner=0`), which is why the gate was editable at all — 660's closure forbids moving the
gate while one is live. The sandbox directory is removed at the end of the step.

## 6. One thing this step found and did not repair

`tools/check_experiment_index.py` was **red before this step and is red after it, with the same seven
violations** (only the line numbers of the last one move, because the new row is inserted above it):
lines 504-513 report *"stage90 is not ascending before its peak"* (568 → 567, then 578 → 577 → 576 → 574 →
572 → 569) and line 581 reports *"not descending after its peak"* (698 → 705). Those rows are other steps'
records, in a file two lanes write to, so they are **not** moved here — but the count is quoted so the next
reader knows the checker's red is pre-existing and not this row's, and so that a step which does move them
can say it took the number to zero.
