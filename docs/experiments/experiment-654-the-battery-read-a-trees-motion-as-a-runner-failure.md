# 654: the battery read a tree's motion as a runner failure, and the pins that stop it

`tools/rehearse_live_path.sh` came back **18 ok, 2 failed** on 2026-09-24, refusing with "at least one
live-path state does not behave as its own contract says". Both failures are the **runner being right**
about a tree that was being rewritten under it: an entry rebuild in this session was writing
`out/stage90/` while the battery was firing the live path. The two cells that straddled the write are the
only two that went red, and this step establishes that from the timestamps, quotes both refusals as the
gate's own correct behaviour, and closes the door in the harness - the live path's tree is now pinned per
state, and a state whose inputs moved gets **no verdict** rather than a FAIL.

Nothing was built and nothing was run against a device. The change is one file, `tools/rehearse_live_path.sh`.

## 1. What the two reds actually were

The failure line was the whole of the evidence the table gave:

```
  FAIL  adb-plan-line                exit 1, promised 0
    did not say (either stream): sudo adb -s 4a2fe00b reboot bootloader
  FAIL  fastboot-plan-line           exit 1, promised 0
    did not say (either stream): the device is already in fastboot, so this run issues no adb
```

Both states are the plan-line pair, both expect exit 0, and both are the last two cells of section A.
`exit 1` there is not the runner's `die` about the plan - it is the runner's **gate refusing**, and the
per-state `err.txt` says so in the gate's own words. Kept under the battery's work directory
(`/tmp/rehearse-live.4g6ZzT`), quoted in full:

```
REFUSING: STAGE90_XNU_SEAM_MEASURE=1 in /mnt/data/mi4-ios6/out/stage90/xnu_arm_entry-config.txt and
entry_seam_flush in /mnt/data/mi4-ios6/out/stage90/xnu_arm_entry.elf calls FlushPoC_DcacheRegion 1
time(s): the record says 535's operation is NOT behind this seam and the image says it is. ...
run_and_capture: the gate refused
```

```
/mnt/data/mi4-ios6/out/stage90/stage90.bin does not contain the bytes of
/mnt/data/mi4-ios6/out/stage90/xnu_arm_entry.bin, while its own compiled-in blob is 5519996 bytes and
this entry image is 5519996 - the lengths agree and the bytes do not ...
run_and_capture: the gate refused
```

Read as statements about the tree, both are true and both are the **torn state**: a record that says
`SEAM_MEASURE=1` beside an entry image whose `entry_seam_flush` calls the POC operation, and a payload
that embeds an entry blob of the same length and different bytes. Neither pair ever existed as a whole
on disk. The gate is the reader that noticed, and refusing a self-contradictory arm is exactly what it
was built for ([[mi4-a-claim-in-a-comment-is-not-a-check]] inverted: this is a check that *did* fire).

## 2. The timestamps, which is what turns "both reds are suspicious" into a measured cause

From the five files the gate reads and the battery's own per-state directories:

| time (UTC) | event |
| --- | --- |
| 17:30:04 | the battery's first state finishes |
| 17:39:34 | `boot-call-fails-after-send` finishes (its cell reads the **old**, whole tree - the sleeper arm's record with the sleeper arm's image) |
| **17:39:34 → 17:39:59** | **`adb-plan-line` runs** |
| **17:39:41** | the rebuild writes `out/stage90/xnu_arm_entry.elf` **and** `.bin` (the POC entry, `a43304f2…`) |
| 17:39:59 | `adb-plan-line` ends - **exit 1**, the record/image tear above |
| 17:39:59 → | `fastboot-plan-line` runs and ends in the same instant - **exit 1**, the payload/entry tear |
| 17:45:44 | `xnu_arm_entry-config.txt` is written (record finally agrees with the image) |
| 17:47:21 | `stage90-qcdt.img` is written (payload finally embeds the new entry) |

The second red is the gate's **first** refusal, not its only one: the payload-embeds-entry byte check is at
`preflight_boot_check.sh:431` and the record/image seam check at `:2605`, so with the record still saying
`SEAM_MEASURE=1` at 17:39:59 both tears were live and the earlier clause is the one it printed. The two
cells read as two different failures for that reason and not because the tree was consistent for one of
them.

So the tree was **not self-consistent between 17:39:41 and 17:47:21** - seven minutes and forty seconds -
and the battery's last two states are the only two that fall inside it. The states before it read the old
arm end to end and passed; the sections after section A did not run at all, because section A's verdict
exits before them. That is a second cost of the same defect and it is worth stating plainly: **one build
during a battery silently costs the reader section and the path-argument section their run as well**,
under a message that blames the runner.

The mechanism is not in doubt in the failure text itself. Every gate clause quoted above is a statement
about *two files disagreeing*, and the identity of one of them had changed mid-state. The gate reads them
in sequence, so it can see a pair that never existed.

## 3. The harness defect this is: it ran the live path against an unpinned live tree

`run_state` fires the **live** `run_and_capture.sh` (`tools/rehearse_live_path.sh:320`), and the live
runner's first act is its gate, which reads `out/stage90/`. Nothing in the battery asserted that the tree
was the same before and after a state, so a state could not tell "the runner misbehaved" from "the
runner's inputs moved under it" - and it printed the second as the first.

The file's own comments already name this class twice, which is what makes it a defect rather than an
oversight. It cites **595** in the `RUNNER_OVERRIDE` note - *"a harness that reported the tree's motion
instead of the edit under test"* - and applies that lesson to the *runner file* (a mutant must be a copy,
because a live process fires it by path) while leaving the *tree the runner reads* unpinned one section
down. It is the same shape as [[mi4-a-claim-in-a-comment-is-not-a-check]]'s most obstructive form: a rule
written down and enforced in one place, with a reader left with a reason not to look in the other.

A green table was equally unsound in the other direction. A battery that passed while the tree moved
would have been read as "the runner is fine", when some of its cells had read a tree that was only
briefly whole.

## 4. The repair: a pin set, a third verdict, and a second exit code

**The pins are measurements, not the build's own record.** `out/stage90/SHA256SUMS.txt` is *written by the
build that produced the files it lists*, so it agrees with whatever is on disk and cannot witness a swap
([[mi4-self-written-record-is-not-a-constraint]], 629). The identity is `sha256sum` and `stat -c %Y` on
the five files the gate reads:

| pinned path | why it is in the set |
| --- | --- |
| `xnu_arm_entry.elf` | the gate reads addresses and the seam's body out of it; the tear above is this file against the record |
| `xnu_arm_entry.bin` | the blob the payload embeds; the second tear is this file against `stage90.bin` |
| `stage90.bin` | what the payload compiled in, so a payload rebuilt on an older entry moves it |
| `stage90-qcdt.img` | what `fastboot boot` would send - the artifact a press consumes |
| `xnu_arm_entry-config.txt` | the record, i.e. the other half of both tears |

**mtime is part of the identity, on purpose.** The gate's freshness sweep *compares mtimes* ("no source
file is newer than the image"), so a rebuild that only rewrote a source file - or a plain `touch` - moves
the tree the runner reads without moving a byte of these five.

**The identity is read immediately before and immediately after each state**, not once per battery: a tree
that moved between state 3 and state 4 invalidates neither of them, since each saw a constant tree, while
the state that straddled the change is the one that gets the third verdict. Cost is ~27 MB of hashing per
read (~0.1 s) against a state that takes ~25 s.

**A moved pin withholds the verdict rather than inventing one.** The row prints `TREE` and the changed
pin's before/after line, and the state is counted in neither `ok` nor `failed`:

* a state that went red across a write may be the runner correctly refusing a torn tree - as both cells
  here were - so `FAIL` would be a claim the reading cannot support;
* a state that *passed* across a write is no better evidence, because it may have read a tree that was
  briefly whole.

**And the exit codes are separate**, because "the runner is wrong" and "this run proves nothing about the
runner" are different claims and must not be spelled the same way: **exit 1** for a real failure (which
outranks), **exit 2** for a run that ended with TREE cells and no FAIL. The header's usage block and its
exit contract carry all three.

**The pin is applied in `run_state` and deliberately not in `reader_state` or in section C.** Both of those
run the runner with `--summarise`, which returns from `summarise_log` and never reaches the gate or the
plan (`run_and_capture.sh:2383`), so the tree cannot touch their readings - and a TREE verdict there would
invalidate a cell on the strength of a file it never read. Stated in the file as a reason, not left as an
omission.

**And no "is a build running?" check was added, on purpose.** A `pgrep` for `build_entry.sh`/`build.sh`
before the first state would refuse *some* of the collisions and, worse, would read as a guarantee: a build
started by another session a second later is invisible to it, and a battery that printed "no build is
running" at the top would be the reassuring answer to the question it cannot answer (the shape of *"`dmesg`
without sudo is a silent zero"* in [[mi4-hardware-run-safety-gate]]). The pin measures the thing that
matters - whether the tree changed while a state read it - and reports it after the state rather than
pretending to have prevented it.

## 5. The detector was shown to fire, which is the only way to know it works

A guard whose firing has never been seen is the 613 shape - and a detector over `out/` can only be shown
to fire by moving something it reads, which means writing into the live tree. So the pin set takes an
**additive** override: `REH_TREE_EXTRA` appends paths to the identity, printing a loud line when it is
set. It can only *add* pins (the live five are always pinned and printed first), so unlike a
`TREE_DIR`-style override it cannot be used to disarm the guard.

Measured, with nothing written into `out/`:

```
p=$(mktemp); ( sleep 5; printf 'moved\n' >> "$p" ) & REH_TREE_EXTRA=$p tools/rehearse_live_path.sh
```

The write lands inside the first state (~25 s), so that state must print a `TREE` row naming `$p`'s
before/after line, every other state must be unaffected, and the run must end at **exit 2**. Measured
(2026-09-24 18:1x, `/tmp/r653/rehearse-extended.log`):

```
rehearse: identity extended with /tmp/r653/tree-probe.txt - the FALSIFICATION knob, not the live pin set.
          The five files in /mnt/data/mi4-ios6/out/stage90 are pinned as well; this can only add.
          6 pin(s) in the identity, as asked.
...
  TREE  happy-adb                    the pinned tree changed while this state ran - NO VERDICT
        | < /tmp/r653/tree-probe.txt sha=e3b0c442…  mtime=1790273499
        | > /tmp/r653/tree-probe.txt sha=2c85ab07…  mtime=1790273511
  ok    host-log-return-no-adb       exit=3  REFUSING to call this a hang
```

(the probe written 12 s into `happy-adb`, whose window was 18:09:25-18:09:49; `e3b0c442…` is the empty
file's sha, so the two lines are the same file before and after its first byte). Every other state reads
as before, and the run ends **`19 ok, 0 failed, 1 with no verdict (the tree moved)`** at **exit 2** -
sections B and C not run, which is the price of the verdict being withheld at the end of section A and
the right trade: on a moved tree there is no reading to hand to them.

### 5.1 The knob's first version added nothing at all, and printed as if it had

This was found by running the falsification and reading the *identity* rather than its exit code, and it
is the second defect in this block in one sitting:

```
< <(printf '%s' "$REH_TREE_EXTRA" | tr ':' '\n')
```

`printf '%s'` writes **no trailing newline**, and `while IFS= read -r` **drops an unterminated last
line**. With a single path - the exact form the usage block documents - the loop body never ran,
`tree_identity` returned the five live pins alone, and the banner still printed *"identity extended
with …"*. So the knob's own proof was inert **while reporting itself armed**, and the falsification run
would have come back as "the TREE detector does not fire" - a conclusion about the guard drawn from a
defect in the guard's test ([[mi4-silence-is-a-reading-only-if-success-is-silent]] one level down). With
two or more colon-separated paths only the *last* was dropped, which is the shape that would have hidden
it for good: the measured failure at 18:09 is a state printing `ok` although its pinned file was written
twelve seconds into its window.

**The repair is the newline *and* a check that the pins landed**, because "the fix is correct" is not the
property worth having - a knob whose effect is not counted goes inert again under any later edit to the
loop. The harness now computes how many pins were asked for (`${#REH_PINS[@]}` plus the non-empty extras)
and how many the identity carries, **prints the count when they agree** (so the success direction is
visible, not silent) and **refuses at exit 1 when they disagree**, with a message that says the run would
otherwise read as "the detector does not fire". Exercised in three directions against the real function:
one extra path → **6** pins, two (one of them non-existent, still a line) → **7**, none → **5**.

### 5.2 The same battery on the quiescent tree

Measured the same evening, `/tmp/r653/rehearse-plain.log`, with no build running in either lane:

| section | result | exit |
| --- | --- | --- |
| A - the live path's states | **20 ok, 0 failed** | 0 |
| B - the reading, on every state the press can produce | **15 ok, 0 failed** | — |
| C - the path argument, resolved against the caller | **4 ok, 0 failed** | — |

**and `EXIT=0`.** The two cells that were red in §1 are in that 20 and green, which is the direct reading
of the question the false red had muddied: **the plan-line pair is present, and the runner has no
regression there.** The 653/646/650 landings did not drop a plan line - and this run is the first of the
battery's whose reading is pinned to a tree, so it is also the first that is a statement about the runner
rather than about the tree and the runner at one wall-clock.

## 6. What this does not do

* **It does not say the runner has or has not a regression.** What it does is make the last red
  *unreadable* rather than misleading: that run is not evidence about the plan-line pair in either
  direction. The direct reading of that question is in §5's second measurement.
* **It does not make the battery safe to run while anything builds - it makes it honest.** The correct
  operating rule is still to run it against a quiescent tree; exit 2 is what tells the reader that rule
  was broken, not a licence to ignore it.
* **It does not close the runner-side exposure, which is the same hazard at press time.** 636 already
  records that the gate **accepts** a tree whose armed image has been replaced, and this step measures
  that a gate call is not atomic against a build: the gate reads five files in sequence, so a rebuild can
  put a *different* consistent pair in front of it as easily as it can put a torn one. Nothing here
  changes the runner, and the sharpening that would - pinning the arm's identity around the gate call and
  re-checking it after the boot send - is named here as an open item, not done in this step.
* **It does not coordinate the two sessions' use of `out/`.** `out/` is one directory, and a build in
  either lane rewrites it. The pins make the collision visible in this harness; the rule that a battery
  and a build must not overlap is a working agreement.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The owed press is unaffected: the acting
  arm's park, the runner and the gate are untouched, and the park still verifies (11 files, exit 0).

## 7. Safety

**No device action**: `sudo`, `adb` and `fastboot` are stubs first on `PATH` and the harness asserts all
three resolve into its own directory before any state runs (`tools/rehearse_live_path.sh:301-309`). No
build, no `fastboot`, no `adb`, and **nothing written to storage**. The only file changed is
`tools/rehearse_live_path.sh`; nothing under `out/`, `stages/` or `docs/` was modified by the change, and
the pin set reads the tree and never writes it. The only writes the step made anywhere are to
`/tmp/r653/` - the two logs, the launcher, and the falsification probe. The gate refusals quoted in §1
were produced by the runner, host-side, with no image sent anywhere.

**As built, and one honest difference from what was measured.** `tools/rehearse_live_path.sh` is
`4b1f374fb034e2ea5e88813a4d497b130080722d427d2f455c12ee077c6a8b65`. The two batteries above ran on the
revision *before* the last one-character edit: the exit-2 message read `the runners gate`, because the
sentence was written as a single-quoted `printf` with `''` for the apostrophe, which bash concatenates to
nothing - the trap the file already avoids two other places with `'\''`. The fix changes one message's
text and no behaviour, and it is recorded rather than smoothed over because "measured on this file" is a
claim about a hash.

**The arm is untouched and still verified.** All five pinned files in `out/stage90/` hash to the
`armed-seam-poc-a43304f2` park (`xnu_arm_entry.elf` `f4ef57fc…`, `.bin` `a43304f2…`, `stage90.bin`
`a47bc89f…`, `stage90-qcdt.img` `a5997bae…`, the record `e3af19b4…`), so the press that is owed still
sends the acting arm. **No catcher is alive** (`ps -eo pid,lstart,cmd | grep press-watcher` is empty,
re-checked immediately before this file was edited - 653's rule, since a catcher fires files by path and
reads its own script incrementally), so the edit was inside a permitted window; that check is about the
gate, the runner and `press-watcher.sh`, and this file is none of the three.
