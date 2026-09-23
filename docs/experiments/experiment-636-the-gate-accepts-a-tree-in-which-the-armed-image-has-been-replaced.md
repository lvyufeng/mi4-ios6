# 636: the gate accepts a tree in which the armed image has been replaced

The next press sends exactly one command - `fastboot boot out/stage90/stage90-qcdt.img` - and the
payload build is **not byte-reproducible** (408), so `out/stage90/` holds the only copy of the armed
arm that has ever existed. Four things have to be true at the moment of the press, and until this step
each was checked by a *different* tool, by hand: the live bytes are the parked bytes, the park is the
set the record describes, the gate accepts the tree, and a press would actually be caught. The first of
those was checked by nothing, and the measurement below is why it matters.

**Measured: the gate accepts a tree whose armed image is a different build.** The gate chains
image ↔ entry bin ↔ config record and the entry-source manifest, and every one of those links still
holds when `stage90-qcdt.img` is replaced by another build of the same entry - so the gate's exit 0 is
not a statement that the bytes the press sends are the recorded bytes.

## 1. The gap, measured

A copy of the armed set with **one byte changed in `stage90-qcdt.img`** (`300` → `Y` at offset 200),
presented as if a rebuild had written it into `out/`:

```
FAIL  live arm is the recorded arm   the LIVE file(s) are not the recorded bytes: stage90-qcdt.img:
                                     live 8bcb5df6…, record 60063c47… - a rebuild, or a tree that
                                     moved under the armed catcher
 ok   the gate accepts this tree     exit 0 - the entry sources match the manifest by content, the
                                     image carries the arm the entry bin holds, and the record binds
                                     that bin
```

**Both rows are true and they are about the same tree.** The gate's four clauses are satisfied because
none of them asks this question: its freshness sweep covers the *payload's* own sources
(`*.c/*.h/*.S/*.ld` one level deep, plus `tools/mkmacho_fixture.py`), its entry clause compares the
`xnu_arm_boot/` sources against the manifest **by content**, and its record clause binds the entry bin
to its config - and a rebuild that rewrote all of them together is *self-consistent*. Nothing in the
chain is anchored to the parked bytes.

So the two ways the armed window actually breaks are both invisible to the gate alone:

| event | what it does | who sees it |
| --- | --- | --- |
| `build_entry.sh` (+ `build.sh`) | rewrites `xnu_arm_entry.bin`, its config and manifest, then the payload and the qcdt - **the live arm becomes a different arm** | **only this check** |
| an edit anywhere under `xnu_arm_boot/` | the entry-source clause compares **content**, so it refuses | the gate (check 3) - and a press spent then boots **nothing** |

The second is the reason the rule for the armed window is *read the tree, do not edit it*; and a
`git checkout`, which rewrites mtimes but not bytes, is fine - 533 removed the mtime sweep from the
entry sources precisely because a checkout produced a false refusal.

## 2. The tool

`tools/verify_press_ready.sh` - four checks, one verdict, exit 0/1:

| check | what it reads | what it refuses on |
| --- | --- | --- |
| 1. the live arm is the recorded arm | `revert-set.txt` for the set, then every member: hash **live vs record** and `cmp` **live vs park** | an absent member, a live file that is not the recorded bytes, or a park that disagrees with the record |
| 2. the park verifies against the record | delegated to `tools/verify_revert_set.sh --set=…` - the recorded method (`sha256sum -c` is **not**, its manifest is absolute-pathed) | any member the record's own tool refuses |
| 3. the gate accepts this tree | `preflight_boot_check.sh --allow-xnu-entry` | any gate refusal |
| 4. a press would be caught | `fastboot devices` / `adb devices` | a list that is not `$SERIAL` alone, a state the catcher's `usable()` is false for, or **a list it could not read** |

Check 1 carries two independent fields of the same bytes on purpose: the record's hash is the
authority and the `cmp` against the park is a copy of it - two fields agreeing is what makes a typo in
either a reading instead of a silence, which is `verify_revert_set.sh`'s own argument for checking size
*and* hash.

**On this host, right now, it reads: 3 ok, 1 FAIL.** The FAIL is the true blocker and nothing else:
`fastboot devices` lists `33e80afe` alone, so a run fired now is refused at the ambiguity guard and
boots nothing (634/635). **The tree and the arm are press-ready** - live arm = park = record, 11 files
each way - and the one thing standing between the press and a run is physical.

## 3. The falsifications

Each is the real tool with one thing changed, and each names what it measured.

| # | what was changed | result |
| --- | --- | --- |
| A | the **park** doctored (one byte in the qcdt), live tree untouched | check 1 FAIL *"the live file(s) MATCH the record but differ from the parked copy"* - the copy and the record disagree, and which is armed is now a question; check 2 FAIL naming the hash |
| B | `--set` names a set the record does not have | check 1 FAIL *"the record has no line for set=… - either way this check compared nothing"* |
| C | `--live` points at a nonexistent directory | check 1 FAIL *"an absent directory is not a failed read"* |
| D | a stub gate that exits 1 | check 3 FAIL *"exit 1 - REFUSING: stub gate"* - the gate's own message carried, not invented |
| E | `sudo` fails (exit 1) | check 4 FAIL *"the device lists could not be READ … it is not a statement that the phone is absent"* |
| F | `adb` lists the phone as `unauthorized` | check 4 FAIL naming the state and the remedy - **not** an ok, because the catcher's `usable()` is false for every state but `device|recovery` |
| G | `adb` lists the phone as `device` | check 4 **ok**, naming the adb branch the run would take |
| H | `fastboot` names the phone with no state column | check 4 FAIL - *presence passes, the guard refuses*; **634's divergence**, from the live side |
| I | the phone alone in `fastboot`, pinned | **4 of 4 ok** - the state the press needs |
| J | the **live** tree replaced (the rebuild case) | check 1 FAIL naming the live hash; **check 3 still ok** - § 1 |
| anchor | nothing changed | 3 ok / 1 FAIL, the FAIL being the neighbour |

A, B, C, J are the four directions check 1 can be defeated in (the park, the record's name, the live
directory, and the live bytes); D tests that a refusing producer is not read as a pass; E - the most
important of the device rows - separates *could not look* from *looked and found nothing*.

## 4. Two defects of this tool's own, both of them this project's known classes

* **The record's shape was assumed, not read.** The first draft parsed each line positionally
  (`set= sha256= bytes= file= role=`) and `file=` is the **fourth** token, so `file` was given the
  *role* text and all eleven members were reported absent with paths ending in
  `manifest_members=stage90_fixture.macho,…`. The tell was the path, and the shape is not even uniform
  down the file - the `SHA256SUMS.txt` line carries an extra key before `role=`. Repaired by parsing
  each token **by name**. This is "an assumed output shape", which is near the top of the list this
  project's measurement notes say to suspect first.
* **A flag whose value was used in one place and not another.** `--gate PATH` was honoured in the
  executability test and then ignored in the invocation, which ran a hardcoded
  `./preflight_boot_check.sh` inside the flag's directory - so falsification D reported **`exit 127`**
  (command not found) as if it were the gate's own verdict. Repaired by invoking `"$GATE"`; the gate
  resolves what it reads from `$(dirname "$0")` itself, so the `cd` was never needed either. A status
  from a producer that never ran, read as a verdict about the gate - `[[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]`, arrived at through a flag.

Both were found by running the tool rather than by reading it, which is the only way either class ever
gets found.

## 5. What this does not do

* **It does not fire anything.** No gate-by-proxy, no run, no boot. It does not arm, disarm or wait for
  a catcher, and it says nothing about whether one is alive.
* **It does not tell you the press will succeed.** It says the press will not be *wasted*. Whether the
  payload runs is the run's verdict: **exit 0 or 3 came back, 2 did not return and owes another press.**
* **It does not fix the neighbour.** Check 4 is a reading; unplugging `33e80afe` is the user's.
* **It does not edit the live catcher**, which still carries the false header sentence 635 recorded -
  the repair is owed for after the press (620).
* **It does not change the gate, the runner, the arm or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still `60063c47…`, still **UNRUN**. The doctored copies of A and J were made in `/tmp` and never in
  the tree.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** It removes a way for the one press
  that can advance it to be spent on nothing, which is the most this side of the cable can do.

## 6. Safety

No device action, no boot, no build, no `fastboot`, no `adb` that acts on anything, **nothing written to
storage**. The device commands were `fastboot devices` and `adb devices` - enumerations, the same two
reads the armed catcher makes on its own 30 s poll. The gate was run seven times and is host-only,
measured inert in 634 (stubbed and unstubbed runs byte-identical, the stub never called). The doctored
copies were made under `/tmp/636/` and the live tree was never written to: `git status` is clean and
`sha256sum out/stage90/stage90-qcdt.img` is `60063c47…` after every run. Both catchers were confirmed
alive (pid 1344846 watcher, pid 4067419 relay). `fastboot boot` only - never `flash` - so no outcome of
this step can write to storage.
