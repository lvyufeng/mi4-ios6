# 660: the armed watcher's edit closure — one direction of movement is caught, one is not, and this session walked into it

A press is owed and is armed. The armed watcher fires three files by path and reads the arm's bytes, so the
set of files that must not move is a *closure* rather than a list, and this step measures both directions of
that closure — what it catches when something moves, and what it does not.

The measurement was prompted by something in this session's own history: **`tools/verify_press_ready.sh` was
edited twice after the watcher was armed.** §1 states that plainly and establishes its scope; §2–§3 measure
the closure's two directions; §4 lists the repairs, all of which wait; §5 is the roster so the next session
does not have to re-derive it. Nothing was built, no device was touched, and **no file in the closure was
edited by this step**.

## 1. The lapse: a file the press fires moved after the press was armed

The armed watcher (`/tmp/r654/press-on-clear.sh`, pid 1951993) started at **2026-09-25 00:52:49**. Measured
from the git record, in that window and after it:

| commit | time (UTC) | files touched |
| --- | --- | --- |
| `0f6556e` | 00:56:10 | the 657 document, the 653 corrigendum, the index — **no fired file** |
| `25c01e7` | 01:00:15 | the 657 document **and `tools/verify_press_ready.sh`** |
| `c292607` | 01:05:43 | the 657 document **and `tools/verify_press_ready.sh`** |
| `9c08faf`, `61df4d6` | 01:09, 01:13 | documents and the index only |

and, for the three files that matter most:

```
git log --since='2026-09-24 20:00' --name-only --format='%h %s' -- \
    stages/stage90/preflight_boot_check.sh stages/stage90/run_and_capture.sh out/
    -> (nothing)
```

So the gate, the runner and `out/` did not move; **`tools/verify_press_ready.sh` did, twice, at 01:00:15 and
01:05:43** — and it is one of the three files the watcher invokes:

```
/tmp/r654/press-on-clear.sh:51   tools/verify_press_ready.sh --set armed-seam-poc-a43304f2 \
/tmp/r654/press-on-clear.sh:56   ( cd stages/stage90 && ./preflight_boot_check.sh --allow-xnu-entry ) …
/tmp/r654/press-on-clear.sh:65   ( cd stages/stage90 && ./run_and_capture.sh    --allow-xnu-entry ) …
```

**This is contrary to the rule this session wrote for itself** (`mi4-hardware-run-safety-gate`): *"from
20:06:48 no edit to `run_and_capture.sh`, `preflight_boot_check.sh` or **any file the press fires** is safe,
even though no catcher is alive."* The readiness tool is fired by the watcher, so it is in that set.

**The hazard, bounded and stated as narrowly as the evidence allows.** What the watcher does with it is
`| tail -14` and `say "readiness exit=${PIPESTATUS[0]}"` — an **advisory print into the fire log**. The bytes
the press sends come from `out/`, which did not move. And the version the watcher will execute is the
committed one, `bash -n`-clean before installation and **executed after the edit** (it printed its five
verdict rows: 4 `ok`, row 4 `FAIL` on the neighbour). So the concrete consequence is that the fire log will
carry the *corrected* verdict text, and **no device-side byte differs**. What the rule genuinely protects is
the general case — a fire running bytes nobody rehearsed — and here that case is bounded by the two facts
just given rather than eliminated.

**What is owed for it, and it is not a re-edit.** The invariant while a watcher is armed is discipline, and
the discipline was broken once in a window where the affected file's own edit could not change the press's
bytes. The repair is the one §4 lists and it waits: after the fire (or a disarm), the closure is free again,
and the pre-fire habit is `tools/verify_press_ready.sh` run **as its committed self** before any arming, so
that the edit and the rehearsal are both inside the free window rather than the armed one.

## 2. The closure's caught direction: an entry-source edit is refused by the gate

The gate compares the entry image's claimed sources against the tree, by content. The manifest is
`out/stage90/xnu_arm_entry-sources.txt`, and its own header states the scope: *"Every regular file in
`xnu_arm_boot/`, by content."* Measured: **20 files**, and the two commands that do the comparison are,
verbatim from `preflight_boot_check.sh:1088` and `:1092`:

```
ENTRY_SRC_RECORDED=$(awk '/^[0-9a-f][0-9a-f]*  / { print $2 " " $1 }' "$ENTRY_SRC_MANIFEST" | LC_ALL=C sort)
ENTRY_SRC_NOW=$(cd "$STAGE_DIR/xnu_arm_boot" && find . -maxdepth 1 -type f -printf '%f\n' | LC_ALL=C sort \
                  | while IFS= read -r _f; do printf '%s %s\n' "$_f" "$(sha256sum -- "$_f" | awk '{print $1}')"; done)
comm -3   # the diff; a non-empty diff is the failure
```

**Reproduced exactly** (`/tmp/g660/srcclause.sh`) against the real tree — the **positive control**, which must
come out empty or the check could only ever say "different" — and against three mutated copies of
`xnu_arm_boot/`:

| case | `comm -3` lines | the gate would fail naming |
| --- | --- | --- |
| **the real tree** | **0 — empty** | *(agrees; the success direction is reachable)* |
| one **comment line** appended to `entry_trace.c` | 2 (old hash, new hash) | `entry_trace.c` |
| a new file `entry_660_probe.c` | 1 | `entry_660_probe.c` |
| `entry_gic.h` removed | 1 | `entry_gic.h` |

**A defect in the measurement itself, caught by the control.** The first run of the script reported the
*edit* case as 3 lines and named **`.gitignore`** in all three cases — a file no mutation had touched. The
cause was the copy, not the clause: `cp "$SRC"/*` does not copy dotfiles, so every mutated copy was missing
`.gitignore` and the diff was reporting *two* differences in the edit case and one in the others. **The
control is what caught it** — an all-empty control against a mutant's two- or three-line diff is a
disagreement about a file name the experiment never touched, and `cp -r "$SRC/." "$DST/"` removed it. The
manifest's own scope ("every regular file") is the reason a dotfile counts, and that is now measured rather
than read.

**And the gate is the sole carrier of this check.** `run_and_capture.sh` has no clause of this kind at all
(`grep -n 'xnu_arm_entry-sources\|ENTRY_SRC\|manifest'` → nothing), so an unbuilt edit is caught in exactly
one place, and only if the gate runs before the boot — which is the standing order.

## 3. The closure's uncaught direction: a rebuild is *consistent*, so nothing refuses it

The interesting failure is not the edit. It is the **rebuild**, and the two scripts' own writes say why:

* `build_entry.sh:30624` hashes the new entry image and `:30662` writes `out/xnu_arm_entry-config.txt` **bound
  to that hash**, while `:30692-30709` rewrites `xnu_arm_entry-sources.txt` **with the same hash on its second
  line**. So after a rebuild the image, the config and the manifest all move **together**, and the gate — which
  additionally requires the manifest's hash to equal `xnu_arm_entry.bin`'s actual hash (`:1071`, *"a manifest
  bound to no artifact can be satisfied by any content"*) — is **satisfied**. Its clauses check *internal
  consistency*, and a freshly built tree is internally consistent **by construction**.
* `./build.sh` writes only payload-side artifacts (`stage90.elf/.bin/.map/.symbols/.size`, the fixture macho,
  the final image); it does **not** touch the entry manifest or the config. So a payload-only rebuild does not
  perturb the clause either.

**So nothing in the firing chain compares `out/` against the arm the press is *owed* for.** A rebuild would
make the watcher fire the new arm — the exact scenario `experiment-658` §2 named as *"bytes that no
pre-registration describes"* — and the gate would print a clean pass for it.

**And the one tool that would notice is advisory.** `tools/verify_press_ready.sh`'s row 1 is *"the live arm is
the recorded arm"* (it hashes `out/` against `revert-set.txt` and the park). The watcher invokes it, captures
`${PIPESTATUS[0]}` into a **`say`**, and proceeds:

```
say "readiness exit=${PIPESTATUS[0]}"        # printed, never tested
say "=== GATE: preflight_boot_check.sh --allow-xnu-entry ==="
```

There is no `if` between the two lines. So a red readiness row cannot stop a fire — the verdict is reported
into the log and the flow continues to the gate. That is the shape `mi4-a-claim-in-a-comment-is-not-a-check`
names, in the press's own automation: **a check whose result is read by a human afterwards rather than by the
thing that decides.** Today it is harmless, because the only red row is row 4 (the neighbour) and the watcher
cannot fire until that condition clears — but it means the firing path's only blocker is the gate, and the
gate does not know what the press was pre-registered for.

## 4. The repairs, all waiting for a window with no watcher

Every one of these is an edit to a file in the closure (§5), so none can be made now — and the discipline is
now backed by a measurement rather than an assertion:

1. **Make the firing path test readiness** (the watcher's own file): `exit 1` on a red row instead of `say`.
   This is the one that would have caught the §3 scenario, and it needs the watcher to be re-armed anyway.
2. **Let the gate read the owed arm's identity** (`preflight_boot_check.sh`): the record and the park are
   already read by the readiness tool, and the gate is the file that actually blocks.
3. **Anchor the storage-symbol patterns** before widening their scope to the entry image (658 §3(a)) — the
   same gate file, and the same window.
4. **654 §6**, the runner-side pin of the arm's identity around its gate call — also an edit to a closure
   member.

Until a window opens, the invariant is: **no build, no entry-source edit, and no edit to the readiness tool,
the gate or the runner while a watcher is armed** — and §1 is the record of it being broken once, in the
mildest of the three ways, by this session.

## 5. The closure's roster, for the next session

Measured from the watcher's three invocations and their transitive reads:

| member | how it enters | why it must not move |
| --- | --- | --- |
| `out/stage90/*` (the arm: entry elf/bin, payload, `-config.txt`, `-sources.txt`) | fired as the image | it **is** the bytes; a rebuild also re-binds the manifest to the new image |
| `stages/stage90/xnu_arm_boot/**` (20 regular files, dotfiles included) | hashed by the gate against the manifest | an unbuilt edit is refused, a rebuilt one is not (§2, §3) |
| `stages/stage90/preflight_boot_check.sh` | invoked by path | it is the only blocker in the firing path (§3) |
| `stages/stage90/run_and_capture.sh` | invoked by path | it sends the bytes; 654 §6's pin is owed here |
| `tools/verify_press_ready.sh` | invoked by path | advisory today (§3) — and the file §1 moved |
| `tools/check_storage_refs.py` | invoked by the gate | if it stops running, the gate's `fail` fires (the missing-tool branch) |
| `stages/stage90/revert-set.txt`, `out/stage90/frozen/armed-seam-poc-a43304f2/**` | hashed by the readiness row 1 | they are the *owed arm's identity* — the only comparison that knows what the press was pre-registered for |
| **not** `preflight_storage_write.sh` | not invoked by this watcher | it gates a persistent write, which this press does not make |

## 6. What this does not do

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot was observed; `poll_seq` still
  stops at 2 and `slot_post_calls` is still absent. The press remains the only step that can move it.
* **It does not repair the lapse or make the closure safe.** It measures the closure and states the lapse; the
  invariant remains discipline until a window with no watcher.
* **It does not measure what a rebuild would actually do to the fire**, and deliberately: making the tree
  inconsistent to test the inconsistency is the one experiment 658 §2 forbids while armed.
* **It does not edit any closure member.** This step's writes are one document, one index row and the
  `/tmp/g660` scratch copies.
* **It does not claim the gate is wrong.** Every clause it has is satisfied by a consistent tree; the gap is
  that **no clause asks *which* arm**, and that gap is worth naming rather than papering over.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet.

## 7. Safety

No device action: no `fastboot`, no `adb`, nothing sent anywhere, **nothing written to storage**. Every
reading is host-side — the gate's own two commands re-run against `xnu_arm_boot/` and against three copies
under `/tmp/g660/`, `git log` over the window, the two scripts' sources, and `build_entry.sh`'s and
`build.sh`'s write sites. **No edit to any file in the closure**, and no build: `out/` was read and not
written, and the arm still verifies against the `armed-seam-poc-a43304f2` park. `fastboot boot` only, never
`flash`.
