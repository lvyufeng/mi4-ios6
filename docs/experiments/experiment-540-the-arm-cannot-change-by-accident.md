# 540: the arm cannot change by accident, and the log says where the pass died

**Two things had to be true before 533's run could be trusted after it happens, and neither was.** The
build had no way to notice that it was about to produce a *different arm* than the one on disk - every
artifact that describes an arm (the switch record, the source manifest, `SHA256SUMS.txt`) is written
*by* the build, so a wrong build rewrites them all in agreement with itself. And the file that reads the
run's result still carried 522's criteria, under which 533's correct image prints a failure.

Neither is a defect in the arm. 533 is still built, frozen, gated and unrun, and **its two artifacts are
byte-identical to what they were before this step**: the boot image `1daaf44e624563694e…` was never
rebuilt, and the entry image hashes `f202f2465886aba6…` on every rebuild today. This step changes what
the *build* and the *reader* do, not what the run will execute.

This is one step with five commits. `536`, `537` and `539` are the gate half and their messages carry
their own measurements (the content clause and the field defect in it; the freshness sweep refusing when
its scan could not look; the refusal naming a test instead of prescribing a rebuild). This doc is the
index for all five, and §1-§3 are the two halves of the repair.

## 1. The gate refused a correct tree, because it asked an mtime instead of the content

533's own commit **broke the gate on the tree it committed**. Measured at 4787622, `git status` empty:

```
xnu_arm_boot/build_entry.sh   2026-09-22 17:53:35.476837508   <- the commit second
out/stage90/stage90-qcdt.img  2026-09-22 17:53:15.533031244   <- the image that file had produced
REFUSING: the image is stale - rebuild it, then re-run this gate.
```

19.94 s apart, content identical, exit 1. The clause listed `xnu_arm_boot/*.{c,h,S,ld,sh}` against the
**payload image's** mtime - and `build.sh` never opens any of them. It consumes exactly one thing from
that directory, `out/stage90/xnu_arm_entry.bin`, byte-embedded at `build.sh:93`, and mentions
`build_entry.sh` only in prose (`build.sh:70`, `:85`, `:309`). So the refusal was about a file nothing in
the payload's build reads.

**Why this could not simply be obeyed.** The refusal prescribes a rebuild, and the payload rebuild is the
one thing this phase cannot spend: 408, the payload link does not reproduce, so a new image is a
different artifact and the frozen `1daaf44e…` - the thing 533's run is defined as - is gone. A false
stale with a remedy that costs the freeze is not a safe-direction failure; it is a trap with a green
light.

The repair is to ask the question the refusal stood in for: **is this entry image the build of the tree
in front of it?** `build_entry.sh` writes `out/xnu_arm_entry-sources.txt` at every build - every regular
file in `xnu_arm_boot/`, hashed, 20 of them, with the image's own sha256 on its second line so a manifest
from an earlier build cannot be read as this one's. The gate recomputes the same list by the same rule
and compares it **in both directions**: a file whose content differs, a file the manifest names and the
directory no longer has, and a file the directory has that the manifest never saw.

Three things fall out of that, and the third is the one that shows the old check was not merely imprecise:

| case | mtime sweep | content clause |
| --- | --- | --- |
| a source edited, image not rebuilt | refused | refused |
| a source **added** since the build | refused only if its mtime happened to be newer | refused |
| a source **removed** since the build | **never seen** | refused |
| a checkout or a commit that rewrote an *unchanged* file | **refused** (the false positive above) | no difference, no refusal |
| `entry_vectors.s`, `entry_ramdisk.s`, `entry_macho.s`, `entry_arm_rtabi.s`, `assym.s` | **never seen** | hashed like everything else |

That last row is not a property of the fix being tighter; it is the fix being the first check that ever
looked at those files. `-name '*.S'` is case-sensitive and **every assembly source in that directory is
lowercase `.s`** - five inputs to this image, compared with nothing, for as long as the sweep existed.

**Two defects in the new clause, both found by running it rather than reading it** (recorded in 536's
message). The manifest is written `hash` + two spaces + `name`, and awk's default splitting collapses
the run, so that line has two fields and the gate's `$3` printed a leading space plus the hash and
**dropped the filename** - 40 `comm` lines for 20 files, every file both "changed" and "added", against
0 with `$2`. And the label sed used a replacing substitution where a prefixing one was meant, printing
`ntry_arm_rtabi.s`, one letter short of the file it was naming. Both are the class this project keeps
paying for: a reader that recognises a shape and then consumes the wrong subject.

## 2. The build could silently produce a different arm, and now cannot

The record, the manifest and `SHA256SUMS.txt` are all *written by the build*. None of them constrains the
next build: a build that arrives with the wrong switches rewrites all three in agreement with itself, and
the gate - which reads them - is satisfied by an arm nobody chose. The arm the run is for then exists
only in a command line and in the 533 doc.

And the wrong invocation is the *documented* one. `build_entry.sh`'s own defaults are not this arm's:

| switch | default | the frozen arm |
| --- | --- | --- |
| `STAGE90_ENTRY_TRACE` | 0 | **1** |
| `STAGE90_ENTRY_REAL_ARM_INIT` | 0 | **1** |
| `STAGE90_XNU_SLOT_NULL` | 0 | **1** |
| `STAGE90_XNU_ISTACK_SEPARATE` | 1 | **0** |
| `STAGE90_XNU_EXIT_POC_FLUSH` | 0 | 0 |
| `STAGE90_XNU_IDLE_CACHE_ENABLE` | 0 | 0 |

Four of six differ, and a blind `./build_entry.sh` is what this file's own header and `build.sh`'s
missing-entry-bin message both tell an operator to run. `SLOT_NULL=0` puts 526's capture sites back where
533's null instrument is: a different arm, produced by following the instructions, with every downstream
check agreeing. That is *a resume script's retained default is a decision nobody re-made* - already this
project's named defect from 522's first image - with the arrow reversed.

So the build now refuses a switch-set change **before anything in `$OUT` is written**, naming both sets
and the one variable that makes the change deliberate (`STAGE90_ENTRY_ARM_CHANGE=1`). Verified in six
states rather than argued, the two that would otherwise build through an isolated harness holding the
block verbatim:

| state | result |
| --- | --- |
| no record (a first build) | proceeds - the record is read, not required |
| full record, same arm | proceeds |
| full record, `SLOT_NULL` 1 → 0 | **refuses**, both lists in full |
| same + `ARM_CHANGE=1` | proceeds, and says the arm is being changed on purpose |
| record with only the artifact keys | proceeds **with a note** (see below) |
| the real script, invoked blind | **refuses**, naming the four differing switches |

Two findings from running it that reading would not have produced. **`say` is defined 100 lines below the
block**, so the deliberate-change path had to use `printf` - a `say` there is a command-not-found under
`set -e` on the one path where the operator has already decided. And **a partial record is not a previous
arm**: a read-back that is `KEY=<value> ` per key whether or not the key is present turns a record
carrying only `SHA256`/`BYTES` into "the previous arm was seven blanks" and refuses *every* build with no
way forward but the override - so all ten must read back, and a partial record is reported as *no
previous arm* rather than silently treated as one.

**The ten is the other half of that finding, and it came from enumerating rather than from reading the
seven.** `grep -o '${STAGE90_[A-Z0-9_]*:-'` over the whole script turns up three more switches that shape
the linked image and were in no record anywhere: `STAGE90_ENTRY_CHECKPOINT` (`:218`, `--wrap=<symbol>` at
`:235-236`, plus `xnu_arm_entry_checkpoint.o` in the link at `:27218`),
`STAGE90_ENTRY_CHECKPOINT_SKIP` and `STAGE90_ENTRY_CHECKPOINT_AFTER` (`:224`/`:228`, compiled into the
wrapper at `:693-710`). A build with a checkpoint set keeps all seven of the other switches identical and
would have passed the refusal **silently** - a guard total for the dimensions someone listed and blind to
three others. They are recorded as `(unset)` when empty, because `X=` is not `X=(unset)` for the same
reason `#define X 0` is not "off" to `#ifdef X`, and the gate prints them (its key list is twelve now).
The rest of the environment is `*_OBJ` link-path knobs, which move where an object is read from rather
than what the image is.

**One build where the guard cannot fire, stated rather than discovered later:** the first rebuild after
this change meets a record that has only the seven keys, so `ARM_WAS_KNOWN=0` and the comparison does not
run - the note says so and names the ten. That build is the one that writes the ten-key record. The
backstops hold in the meantime, and their order matters: the gate refuses **blob clause (bin vs the bytes
embedded in the frozen image) → record-key clause → sources clause**, so a wrong-switch rebuild produces a
different bin and is caught by the *first* of the three. `build_entry.sh` never writes the boot image at
all.

## 3. The reader still had 522's criteria, and would have failed 533's correct log

`run_and_capture.sh`'s verdict block asserted 522's arm's shape: `_win` with `C` clear and `_set` with `C`
**set**, the second being the re-enable. 533 is the arm whose whole content is *removing* that
re-enable, and `entry_window_note` is kept on purpose because the pair is the reading that says which arm
ran - so 533's correct image prints `_set` with `C` clear. Measured against the copy at HEAD, on a log
with 533's expected pair:

```
FAIL  slot_cwe_set=0x00c50830: C is not set on the far side of the call,
      so the re-enable did not happen
```

which names an instruction that is not in the image, for the reading that arm exists to produce. This is
533's own doc §3 defect - *a check whose shape was copied from the previous arm* - in the one file that
reads the run's result, and it had been sitting in the path of this run since 533 was built.

The pair is now reported for what it is, with each arm's shape named beside it (`ARM 522's arm: … C set` /
`ARM 533's arm: … C clear`), and FAIL is reserved for the one shape neither arm can produce: `_win` with
`C` set, which would say the window never opened where the image claims to read it. Verified on three
synthetic logs - 533-shaped, 522-shaped, and the impossible one - each run against the state it is meant
to refuse.

**And item (3) became the run's actual prediction.** The exit wrapper calls three bracket publishers -
`entry_slot_null_note(&g_slot_pre)`, `entry_slot_rtc_note(&g_slot_rtcpre)`, and after the real exit
returns `entry_slot_null_note(&g_slot_post)` - and it ends in a tail branch rather than returning, so all
three are reached in the same pass or the pass died at the one before. They share one publish schedule
(`entry_stubs.c:6236`, `n <= 4 || power of two`) and one `entry_live_ready()` gate, so **if one published,
the others would have published had they been reached** - which turns "`post_calls` is absent" from
UNREAD into a localization:

| in the log | reading |
| --- | --- |
| `post_calls` ≥ 1 | the exit returned through the wrapper - the pass survived |
| `pre_calls` and `rtcpre_calls` published, `post_calls` absent | **the death is inside `platform_cache_idle_exit`** - 520's `pop {fp, pc}` |
| `pre_calls` only | died between the two notes, earlier than 520's death |
| none of the three | the log cannot say; a wrapped ring buffer or a pass that never reached the exit |

> **Correction (558): every `rtcab` in this section was `rtcpre`.** The second of the wrapper's three
> publishers is `g_slot_rtcpre` (`entry_trace.c:1973`); `g_slot_rtcab` is the **abort** path's, published
> from `entry_note_sleh` (`entry_stubs.c:1792`), so it is a storm counter and not a per-pass one - 520's
> log has 5 `rtcab` records (`1,2,3,4,8`) against 9 abort episodes. The table above is corrected in
> place, and `run_and_capture.sh`'s clause (3) - which was keyed on `rtcab` and so would have printed
> `DIED BEFORE THE EXIT` for a pass that reached the wrapper with no abort, or `DIED IN THE EXIT` for one
> that had aborts and never reached the wrapper - is repaired in 558.

Two things a reader must not take from the counts. They are published on a schedule, so a printed `4`
means *at least* four and possibly 5-7 - the number is the largest published count, not a total
(measurement defect 406), and nothing here uses it as one. And with `IDLE_CACHE_ENABLE=0` the enter
wrapper's two `SCTLR` reads are back to back (`0x8047c924`, `0x8047c928`, nothing between them), so
"they agree" is structural and cannot fail - the informative half is `C` clear, which is also what still
separates this arm from 522's.

## 4. What is unchanged, and the state of the run

- `out/stage90/stage90-qcdt.img` = `1daaf44e624563694e…`, untouched: **`./build.sh` was not run in this
  step**, and the payload's blob clause passes against it on every gate run.
- `out/stage90/xnu_arm_entry.bin` = `f202f2465886aba6…`, reproduced by **five** rebuilds today (17:45 at
  the current sources; 18:05, 18:13, 18:30, 18:39) across four revisions of `build_entry.sh`, every one
  of which changed only shell that feeds no compiler input. The last of them was run alone, on a quiet
  tree, for the reason below.
- `preflight_boot_check.sh --allow-xnu-entry` exits **0**, with the record printing twelve keys and the
  sources clause printing `20 file(s), every one matching the manifest, and none the manifest does not
  name`.
- **The run is still owed.** The device is off the bus - the last `usb 3-10` event is 526's own
  `18d1:d00d` device 117 disconnect at 16:35:47, `sudo fastboot devices` and `sudo adb devices` are both
  empty - so 533 waits on a power press before it can be read at all.

**One measurement in this step looked like a defect and is a property of the build's length, measured
rather than argued away.** Mid-step, the entry bin's mtime read *later* than the record carrying its
hash, which the script's order makes impossible - the link is at `:27274` and the record at `:30309`. The
gap between them is the 25-command verification gauntlet, and on a quiet tree it is **7 minutes**: one
rebuild alone in the tree puts `.elf`/`.bin` at 18:39:02 and the record at 18:46:12, and the whole run at
7 m 16 s. So for most of every build, `out/` legitimately holds a *new* bin beside the *previous* build's
record, and an observer reading it in that window compares build N's artifact with build N−1's
description of it - which is what happened here, with two sessions and repeated rebuilds in one tree. The
content never disagreed: the bin reproduces, `objcopy` of the current `.elf` is identical to it, and the
record's hash and the frozen image's embedded bytes both equal it. What the episode is worth keeping is
the discrimination, not the alarm: a stale description and a wrong artifact have the same shape, and the
one test that separates them is whether the bytes agree - not whether the timestamps do.

## 5. Safety

No device action in this step: entry rebuilds, gate runs and synthetic log reads, all on the host. Nothing
was written to storage and `flash` was not used. The arm frozen for the run was not rebuilt once.
