# 644: the arm the press sends had no copy outside the tree, and the two records a reader would check cannot tell the arms apart

The press sends exactly one command — `fastboot boot out/stage90/stage90-qcdt.img` — and the payload
build is not byte-reproducible (408), so `out/stage90/` holds the only copy of the armed arm that has
ever existed. 636 turned that into a gate row for **what the tree accepts**. The 574 park was rescued
for the arm that *enters* the idle window. **This step is about the arm that is live**: measured
2026-09-24, it had the same exposure, no copy outside `out/`, and no rescue site at all.

Nothing was built, booted, edited, re-armed or run. Every number below is a read of two arms' bytes,
of their records, and of one host-side instrument. No device action.

## 1. Measured: where the live arm's bytes were, before this step

`find /tmp /home/lvyufeng /mnt/data -maxdepth 8 -type f -name xnu_arm_entry.bin`, each hit hashed.
The 5519996-byte entry bin `696a0f39…` — the arm `out/` holds — was at exactly five places:

| location | survives a `build_entry.sh` run? | survives the job's deletion? |
|---|---|---|
| `out/stage90/xnu_arm_entry.bin` — **the live file** | no: it is what the build overwrites | yes, but as the new arm |
| `out/stage90/frozen/armed-sleepless-696a0f39/` — the park | **no: the park is itself inside `out/`** | yes |
| `/tmp/r594/boot-594/`, `/tmp/r594/on/` | yes | yes, but `/tmp` is not durable |
| `$CLAUDE_JOB_DIR/tmp/parked-696a0f39/` | yes | **no** |
| `/mnt/data/mi4-ios6-export/armed-sleepless-696a0f39/` | yes | yes — **added by this step** |

So before this step the arm had **no copy outside `out/` and the job temp directory**, and the one
site that looked independent — the park — is inside the directory the build rewrites. The 574 arm, by
contrast, has had a durable copy in the export directory since 2026-09-24 01:00: the repair had been
done for the *fallback* arm and not for the one the press would actually send.

**A second measurement fell out of the same census, and it is the "right size, wrong value" class in
a new form.** Six of the nineteen hits are **not files** but symlinks:

    $CLAUDE_JOB_DIR/tmp/gate3/out/stage90/xnu_arm_entry.bin        -> out/stage90/xnu_arm_entry.bin
    …/mirror594/…, …/mirror594b/…                                  -> out/stage90/xnu_arm_entry.bin
    …/reh574/cases/{A,B,C,D,E,F}/xnu_arm_entry.bin                 -> out/stage90/xnu_arm_entry.bin
    $CLAUDE_JOB_DIR/tmp/shadow-526/xnu_arm_entry.bin               -> out/stage90/frozen/526-xnu_arm_entry.bin

`sha256sum` and `stat` follow a symlink, so each of these **hashes to the live arm and reports its
size** while the tree it sits in holds none of those bytes. A hash taken in such a tree is a
statement about the target and not about the tree; and for `reh574/cases/*` — rehearsal trees whose
subject was the 574 arming — the entry bin they carry is the **sleepless** arm, i.e. the one thing the
two arms differ in is the one thing that is not local to them. This is a finding for the session that
owns those scripts, sent as a message and not edited here.

## 2. The repair, and the method that verifies it

The park inside `out/` was copied outward with an ordinary `cp -r` (fresh mtimes, 2026-09-24 01:21;
**never `cp -a`** — the gate's freshness sweep refuses an entry whose sources are older than the build
session), and verified the way `stages/stage90/revert-set.txt` records:

    tools/verify_revert_set.sh /mnt/data/mi4-ios6-export/armed-sleepless-696a0f39 \
        --set=armed-sleepless-696a0f39
    → exit 0, "VERIFIED: 11 file(s) … 6 manifest-member check(s) agree"

**Never `sha256sum -c SHA256SUMS.txt` in that directory**: the copied manifest carries
`/mnt/data/mi4-ios6/out/stage90` absolute paths, so run in a park it hashes the live tree and reports
`FAILED` for everything that has changed — a manifest of absolute paths records a location, not a
directory. The record in `revert-set.txt` is the authority; the copy was checked against it.

**And the record's own claim about the two sets is now measured rather than quoted.** It says the two
arms' sets differ in 9 of their 11 files. Measured between the two parks: **9 differ, 2 agree**, and
the two that agree are the two the record names as byte-identical — the payload's own
`stage90-build-config.txt` (`6c2b6038…`, 682 B) and `stage90_fixture.macho` (`52bc9c35…`, 1744 B).

## 3. Two readers that cannot tell the arms apart, and the one that can

The question a reader arrives with is "which arm is in `out/`?". Three candidates, measured:

| reader | what it says about the two arms |
|---|---|
| `strings -n 6 \| sort -u` on the entry bin | **13234 unique strings each, and `comm` is empty in both directions**: no text in the image distinguishes them |
| the payload's own switch record, `stage90-build-config.txt` | **byte-identical** (`6c2b6038…`): the switch that separates the arms is an *entry* build switch, so the payload's record cannot express the difference |
| `cmp -l` on the two entry bins | 531553 differing bytes in 69717 runs, first at offset 8645, last at 5487365 — real and diffuse |

So the entry record is the only *document* that differs, and it differs in exactly two lines:

    STAGE90_XNU_ENTRY_SHA256           696a0f39…   vs   151425c4…     (the hash that must differ)
    STAGE90_XNU_IDLE_NO_SLEEP          1           vs   0

**The reader that can is `tools/check_idle_window_unreachable.py`, and it is not a record — it reads
the code.** Run on both ELFs (2026-09-24 01:24), plus the new copy as a control:

| ELF | verdict |
|---|---|
| `out/stage90/xnu_arm_entry.elf` (`7f80c2cb…`) | `VERDICT: the window is UNREACHABLE in this image.` |
| the export copy (`7f80c2cb…`) | identical — same bytes, same verdict |
| the 574 park (`3bc72605…`) | `VERDICT: the window is reachable EXACTLY ONCE in this image.` |

Two outcomes, one per arm, from an instrument that reads the artifact rather than the build's own
record — which is what makes it usable as a check rather than a quotation.

## 4. The check that would *not* have been a check

The first idea for a pre-press arm row was to assert that the arm being sent can publish the owed
run's witness. That is wrong, and the reason is four printed lines away from the claim:

    entry_stubs.c:4537   if (g_poll_calls < 4u) {
    entry_stubs.c:4538       entry_live_write("xnu_live_poll_seq", g_poll_calls + 1u);
    entry_stubs.c:4542       entry_live_write("xnu_live_poll_timeout_ms", timeout_ms);
    entry_trace.c:1303   entry_note_poll(caller, fds, nfds, timeout, (uint32_t)error,
    entry_trace.c:1304                   (retval != 0) ? (uint32_t)*retval : 0xFFFFFFFFu, before, after);

`entry_note_poll` is called from `__wrap_poll` **outside both** `#if !STAGE90_XNU_IDLE_NO_SLEEP`
guards — the first opens at `:1253` and closes at `:1264`, the second opens at `:1283` and closes at
`:1296`, and the call sits at `:1303`. Those guards hold only 514's repair. So the witness keys are
published by **both** arms: `poll_seq=3` with `poll_timeout_ms >= 1000` is a green row whichever arm
is sent, and a check written on it **could not fail in the direction it was written for** — the class
this project keeps re-finding (`mi4-a-claim-in-a-comment-is-not-a-check`,
`mi4-silence-is-a-reading-only-if-success-is-silent`).

What the two arms actually differ in is **which question the press's log can answer**:

| arm | `IDLE_NO_SLEEP` | the window | the log answers |
|---|---|---|---|
| live in `out/` (`696a0f39…`) | `1` | UNREACHABLE | the witness: a third `poll` and its timeout |
| `arm-574-idle-no-sleep-0/` (`151425c4…`) | `0` | reachable exactly once | 638 §3's `a1`/`b1` pair table, and 642's `sleh_pc` join |

Neither is broken; a press answers the question the arm it sends was built for, and one press answers
one of them.

## 5. Where the fix belongs

`tools/verify_press_ready.sh` and `tools/check_idle_window_unreachable.py` are `mi4-ios6-1a`'s files.
This session's lane is `stages/stage90/preflight_boot_check.sh` alone, so the proposal went to that
session as a `SendMessage` and **nothing in either tool was edited here**: one more row, asserting the
reachability verdict and the record's switch together (`UNREACHABLE` ⟺ `STAGE90_XNU_IDLE_NO_SLEEP=1`),
so the verdict names the arm it is about instead of leaving the choice to the operator's memory. It is
the row 636's tool wants and cannot get from the park bytes, since the park is the arm for only one of
the two ways the press is spent.

## 6. What this does not say

- **Not that a copy is a park.** This directory is a copy *of* the recorded park, verified against
  `revert-set.txt`. The record's authority is its hashes; the copy is a copy, and
  `verify_revert_set.sh` says only that these are the recorded bytes.
- **Not that the arm choice is settled.** This step measures which arm is where; it does not recommend
  one. The live arm's log carries no `xnu_live_seam_*` key (640) and must not be scored on 638 §3's
  pair table — and the park's log cannot carry the witness that a first press is usually spent on.
- **Not that the symlink trees are wrong.** They were built for other subjects; what is measured is
  that a hash taken inside them describes the live file, which is worth knowing before one of them is
  read as an independent copy.
- **Not a device action.** No boot, no `fastboot`, no `adb` that acts on anything, no build, no edit
  under `xnu_arm_boot/`, nothing written to `out/` or to storage. `fastboot boot` only — never
  `flash`. The device was not touched; the two enumerations this step read were taken earlier and
  unchanged.
- **Not an assertion that a press would work.** `fastboot devices` still lists the neighbouring
  handset `33e80afe` alone, so a run fired now is refused at the ambiguity guard and boots nothing.
