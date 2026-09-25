# 668: the arm the press sends is pinned by the runner, and three instruments' flag sets come from one derivation

667 repaired `tools/verify_press_ready.sh` and recorded the reason: the gate's `--allow-*` flags are a
function of the arm's own switches, so a caller holding a hard-coded set is a caller that goes red the
moment the arm changes. This step is that sentence taken seriously — the same defect was measured in
**two more instruments**, one of which is the file that spends the press, and the fix is to stop each of
them holding its own answer.

Nothing was fired and nothing was sent: every command in this step is host-side, `out/` was read and never
written, and the two falsification trees live outside the repository.

## 1. The defect, in three instruments, measured

| instrument | what it held | what it did on the arm 666 parked (`armed-selftest-wdog-ef0361a2`) |
| --- | --- | --- |
| `tools/verify_press_ready.sh` | `"$GATE" --allow-xnu-entry` | `the gate accepts this tree` **FAIL**, quoting a *passing* line as the reason (667 §1b) — repaired in 667 |
| `tools/rehearse_live_path.sh` | `bash "$RUNNER" --allow-xnu-entry` | **0 ok, 20 failed** — measured this step on the unmodified file, and `REFUSING: the hardware-watchdog SELFTEST skips the normal boot path and spins until a net reboots it; needs --allow-hw-watchdog-selftest` in every cell's `err.txt`. The battery had been red since the arm was built, and nothing read it |
| the armed press launcher (`/tmp/r654/press-on-clear.sh`, scratch) | `./preflight_boot_check.sh --allow-xnu-entry` and `./run_and_capture.sh --allow-xnu-entry` | a press fired by it is **spent on a gate refusal**. Its readiness call is also pinned to the *superseded* acting arm (`--set armed-seam-poc-a43304f2`), which is why it currently cannot fire at all — the readiness row blocks first |

The battery's red is the finding that changes the shape of the step. A battery whose every cell fails is
not measuring the runner, and it is exactly the kind of red this project has learned to read as "the tool
is stale" — which is why the third row, one file over, would have cost a press. One missing definition,
three symptoms, and the two that matter most are the two nobody was looking at.

## 2. R4: the runner pins the arm around its gate call

654 §6 named this and left it open: *"the gate call is not atomic against a build … the sharpening that
would — pinning the arm's identity around the gate call and re-checking it after the boot send — is named
here as an open item, not done in this step."* 660 sharpened the reason: a **rebuild is caught by
nothing**, because image, config and manifest move together, so every gate clause is satisfied by
construction, and **nothing compared `out/` against the arm the press is owed for**.

`stages/stage90/run_and_capture.sh` now has a step of its own, `== the arm this press is for ==`, before
the gate, and three readings in it:

1. **Resolve what the bytes ARE** — `tools/resolve_arm_set.sh "$OUT"` hashes `stage90-qcdt.img` (the one
   file `fastboot boot` sends) and returns the recorded set, or refuses naming the hash it looked for and
   the sets the record does have.
2. **Require the caller to declare the arm** — `--expect-arm=<set>`, in both spellings
   (`--expect-arm SET` too, because a launcher that builds its command as a string is the caller this
   exists for). **No default**, for the same reason 667 removed the readiness tool's default park: a
   default is a name nobody typed, and a name nobody typed is how the wrong arm gets pressed. A live run
   without it refuses before the gate with nothing sent; `--dry-run` prints what it would have checked, so
   the flag can be exercised without a phone. A mismatch between the declared name and the resolved one is
   the same refusal, naming both.
3. **Pin the hash and re-check it** — once immediately before the send (a refusal, because up to
   `FB_AMBIG_WAIT` seconds of device checks have elapsed since the gate ran, and nothing has been sent
   yet), and once after the send (a **note**, because the press is spent either way by then; its job is to
   stop the log being read as a reading of the arm it names — 662's shape, "a state whose pinned inputs
   moved gets no verdict").

The pin is the image and nothing else, deliberately: any rebuild that could change what this press does
changes that file's hash, and a rebuild that does not change it did not change what this press does.
Widening the pin to the other four files the gate reads would be a check that cannot fail for the reason
that matters.

**The consequence is the point, and it is a behaviour change on purpose.** The armed launcher's
`./run_and_capture.sh --allow-xnu-entry` no longer reaches the gate: it is refused before anything is
sent. A stale caller can no longer spend a press — the worst outcome available to this file was a press
booted on a superseded arm and read as the arm it was pre-registered for, and that is now unreachable
rather than merely unlikely.

Two silences were found and fixed while editing the argument loop, and the second is pre-existing:
`set -euo pipefail` is on, so `--summarise` **with no value** failed its `shift 2` and ended the script
with **status 1 and no output at all** (`EXIT=1 lines=0`, measured). A refusal that prints nothing cannot
be told from a crash. `--summarise` and the new flag both now refuse by name, and an empty value
(`--expect-arm=`) is refused separately from an absent one — "the caller did not say" and "the caller said
nothing" are two different records.

## 3. One question, one definition — twice

Two tools, because there are two questions, and each was being answered in more than one place:

* **`tools/resolve_arm_set.sh DIR`** — *which recorded set are these bytes?* Resolved **by hash** because
  the caller does not know the name (that is why it is asking), and because the record's own `role=`
  sentences cannot answer it: three sets claim to be "the arm the next press sends" (667 §2a). One
  tab-separated line out — set, sha256, bytes — or a refusal on **stderr** that names what it found
  (no such file / no such set / two sets / unreadable record). Called by the runner, by
  `tools/verify_press_ready.sh`, and by the battery.
* **`tools/gate_flags_for_arm.sh DIR [--gate PATH]`** — *which `--allow-*` flags does this arm's record
  demand?* The table is still a copy of the gate's conditions, and it carries both checks that bound the
  copy: every switch it reads must be named exactly once **with a value**, and every flag the gate's own
  usage line and refusals name must be producible — an eleventh flag becomes a refusal that names it. One
  flag per line out, so a caller is sure where the boundaries are; zero lines is a legitimate answer.
  Called by `tools/verify_press_ready.sh` and by the battery.

**What deliberately did not merge, and why.** `verify_press_ready.sh` keeps its own reader for the
payload's `#define NAME VALUE` record, because it asks a different question — the *values* of two switches,
to name the arm in row 4 — where the tool asks for the whole table, to derive flags. The same line of
reasoning keeps the readiness tool's `kv_of` (enumerate the members of a **named** set) separate from the
resolver (identify an **unnamed** set by hash). Two questions, two readers, one record; and the drift
between them is loud rather than quiet, because a parse that stops seeing a value refuses both.

## 4. Falsifications

Every new branch was run, including the two that needed a mutation rather than an invocation.

| what was falsified | how | what it printed |
| --- | --- | --- |
| the resolver, four ways | the truncated-copy tree, the park, the acting arm's park, an absent member | correct set for the first three; the truncated copy names its hash and **all four** recorded sets |
| the flags tool, four ways | the live arm, the acting arm's park, a duplicate+bare switch record, a record absent, a fake gate naming `--allow-newthing` | `--allow-xnu-entry --allow-hw-watchdog-selftest` / `--allow-xnu-entry` / the two switch defects by name / the unreadable record / the unknown flag |
| `--expect-arm` absent on a live run | `./run_and_capture.sh --allow-…` | `exit 1` **before the gate**, naming the set the bytes are and the exact argument to pass |
| `--expect-arm` naming the superseded arm | `--expect-arm=armed-seam-poc-a43304f2` | `REFUSING: this press was declared for … and the bytes in out/ are 'armed-selftest-wdog-ef0361a2'` |
| every `--expect*` spelling | `--expect_arm=`, `--expectArm=`, `--expect-arm` bare | refused by the argument loop, naming the flag; the emitter no longer falls through to the gate's `unknown argument` |
| the empty-value silences | `--summarise` alone, `--expect-arm=` | both refuse by name instead of `EXIT=1 lines=0` |
| **the pre-send refusal's reachability set** | a mutant forcing `_sha_now` to a mismatch, through the whole battery | `7 ok, 13 failed` — and the seven that pass are exactly the states that refuse **before** the send: `host-log-unreadable`, `no-fastboot-after-reboot`, `two-devices-in-fastboot`, `one-device-wrong-serial`, `adb-unauthorized`, `adb-offline`, `fastboot-other-after-wait`. All 13 that reach the send exit 1. The damage pattern *is* the branch's reachability set |
| **the post-send note** | a mutant forcing only `_sha_after`, through the whole battery | section A green, and `happy-adb.out` carries the whole `**NOTE, and it is about this log's provenance…**` paragraph with both hashes (`ef0361a2…` and the forced one) |
| the whole readiness verdict | the seven-from-667 suite re-run against the refactor | all seven still fire; the gate-flag reasons now come from the tool, so 667's T4 (`--allow-newthing`) reports the vocabulary refusal instead of the gate's exit |
| **the battery** | `tools/rehearse_live_path.sh` on the refactor | see §5 |

## 5. The battery, before and after

* before, on the unmodified files: **0 ok, 20 failed**, every cell quoting the gate's flag refusal.
* after: **20 ok, 0 failed** — and the other two sections of the same file, which the runner's argument loop feeds, stayed green as well (**15 ok, 0 failed** reader states, **4 ok, 0 failed** path states). `tools/rehearse_live_path.sh` exits 0.

That number is the check on R4 itself as much as on the extraction: the battery's live-path cells go
through the runner's new pin on every state, so a pin that refused a correct arm would take the tally back
to zero.

**And the two send-time branches were measured rather than argued.** Both are unreachable by choosing a
state — the pre-send refusal needs the tree to move between the gate and the call, and the post-send note
needs it to move during the call — so each was forced by a mutant of the runner under
`RUNNER_OVERRIDE` (a copy at `stages/stage90/.r668-mut-*.sh`, which the gate's freshness scan does not see
and the catcher never fires; both copies were deleted before the commit). The pre-send mutant's tally is the
sharper result: **7 ok, 13 failed**, where the seven are exactly the states whose path refuses *before* the
send, and the thirteen are exactly those that reach it. A mutant that damaged a state it cannot reach would
have been a mutant in the harness, not in the runner — the reachability set is what makes the number mean
something. The post-send mutant leaves every exit code alone, as a note must, and its evidence is the
paragraph in `happy-adb.out`, not a tally.

Both runs used a scratch copy of `tools/rehearse_live_path.sh` whose `cleanup()` keeps the per-state
`out.txt`/`err.txt` instead of `rm -rf`ing them (`diff` against the real file: **one line**), because the
harness otherwise deletes the only artifact a passing state produces. That copy lived at
`tools/.r668-keep-rehearse.sh` and was deleted with the mutants.

**And the **third** run was the unmutated runner through that same copy, because a branch is not measured
by its exceptional arm alone.** It re-read `20 ok, 0 failed` / `15 ok` / `4 ok`, exit 0, and left the
ordinary path on disk beside the forced one: `happy-adb.out:571` reads `the bytes sent were the bytes the
gate read: ef0361a2…, unchanged across the send`, with `:11` and `:13` above it naming the resolved set and
the declaration that matched it. So both arms of the note and both arms of the pin are artifacts now
rather than a reading of the source — which is the same standard §4's readiness table is held to, and the
reason the harness's own `rm -rf` had to be worked around instead of trusted.

## 6. Findings recorded and not fixed

* **The armed launcher is stale in three ways** (`/tmp/r654/press-on-clear.sh`, a scratch file outside the
  tree, so it is not a lane's repository file): the readiness call is pinned to `armed-seam-poc-a43304f2`
  (the superseded acting arm), and both invocations carry `--allow-xnu-entry` alone — neither the flag set
  nor the new `--expect-arm` that the runner now requires. It cannot fire the owed arm at all, and the
  readiness row is what stops it: the repair is to re-point it at the arm resolved from the bytes, and to
  take the flags from `tools/gate_flags_for_arm.sh`. Reported rather than edited here: the catcher is the
  peer lane's work, and the file is not in the tree — the message to `run-experiment-526` carries the
  three stale things, the **three-line recipe** for the arm currently in `out/`
  (`verify_press_ready.sh` with no `--set`, then the gate, then the runner **with `--expect-arm`**), and
  the warning that the launcher's runner line no longer reaches the gate on purpose.
* **`stages/stage90/xnu_arm_boot/build_entry.sh` records the old invocation in eight comments**, and it
  must **not** be edited now. It is a *generator*: editing it moves the entry image's sources, and the
  gate's entry-source clause compares content — so an edit here is the gate refusing the arm that is
  currently owed a press. The stale text is recorded in this doc instead, which is the same trade 667's
  record makes for the acting arm's `role=` sentence: a later step supersedes, it does not tidy.
* **The `build.sh` skip clause and 666's own §7 list** are unaffected by this step.

## 7. Safety, and what this does not do

Host-side only: no `fastboot`, no `adb` beyond the two *list* reads `verify_press_ready.sh` row 5 has
always made, no boot, and **no byte under `out/` written** — the falsification trees are under
`/mnt/data/scratch-verify-press-ready/`, and the truncated payload there was produced by *reading* the live
qcdt through `head -c` into a new file. `fastboot boot` only, never `flash`; nothing written to storage;
the neighbour's serial `33e80afe` was not touched. The battery asserts its three device binaries resolve
into its own stub directory before any state runs.

**Nothing is left behind in the tree but the tools.** The two mutated copies
(`stages/stage90/.r668-mut-{pre,post}.sh`) and the keep-the-work-dir copy of the harness
(`tools/.r668-keep-rehearse.sh`) are deleted before the commit — the mutants because a `.sh` beside the
runner is a file the gate's freshness scan does not see and the catcher never fires, and the harness copy
because two copies of one harness is the defect this step is about. The two background batteries
(`b19kev8gz`, `bxaj5ck9b`) and the third that re-ran the live runner for the equal arm of the note
(`bq9il1c3x`) all ran with no device attached to the host: their `sudo`, `adb` and `fastboot` are stubs
made by `mktemp`, asserted first on `PATH`.

**It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The frontier is where 652 left it — XNU
reaches pid 1, runs the userland phase, dies at the idle exit's `pop {fp, pc}` — and this step produces no
boot and no reading: it makes the next press attributable and makes a press on the wrong arm impossible.
**TWRP-to-storage stays withheld**, because 「如果os已经能进去了的话」 is still unmet.
