# 614: the arm the press sends was protected by nothing, and the record could only ever refuse

613 ended with a verifier, a rehearsal and a partition table — all of it pointed at **one** arm, the frozen
574 park, and all of it aimed at the moment *after* a run goes wrong. This step points the same machinery at
the arm the next press **sends**, and finds two things: that arm existed in exactly one copy in the world,
and that the record had no way to say "this directory is right" about the arm that is actually there.

Host-side only: one record extended, one tool extended, one rehearsal extended, one reference paragraph, one
document, one index row, one park on disk. No boot, no build, no device, no `fastboot`, no `adb`, **nothing
written to storage**. **TWRP stays withheld.**

## 1. The gap: the unrun arm had one copy and no record

`out/stage90/` holds the arm the next press sends — qcdt `60063c47…`, entry `696a0f39…`, the **sleepless**
arm (`STAGE90_XNU_IDLE_NO_SLEEP=1`). It is built, it passes the gate, and **it has never been run.**

It was also protected by nothing. `out/` is gitignored, no park of it existed, and 611's record describes
only the *frozen 574* arm — the one to fall back to. Running `build.sh` or `build_entry.sh` would overwrite
it, and the reason that is worse than inconvenient is 408's finding, which the record's own header for the
574 set had already written down: **the payload build is not byte-reproducible.** The entry image is
(`build_entry.sh` does), but `stage90.bin` / `stage90.elf` / `stage90-qcdt.img` / `stage90.img` are not. So
`out/stage90/` was the **only copy that has ever existed** of the one arm this project's next device action
depends on. Losing it costs more than a rebuild — it costs the press.

So the same protection 611 gave the 574 arm was applied where the risk actually was:

* **parked** at `out/stage90/frozen/armed-sleepless-696a0f39/`, the eleven files this record defines,
  copied from the live tree in the same run that measured them, `sha256sum`-compared file by file against
  the originals before anything else happened;
* **recorded** as a second `set=` in `stages/stage90/revert-set.txt`, with the manifest's member list pinned
  the way 612 established (`manifest_members=`) and the byte count as the second, independent field.

The two sets differ in **9** of their 11 files — measured, not counted by eye: the tool run against the live
tree with only `frozen-574` selected prints **9 FAIL lines**. `stage90-build-config.txt` and
`stage90_fixture.macho` are byte-identical between the arms, and that is worth stating for exactly the reason
612 gives: "it happens not to matter for those two" is the reasoning a record exists to replace.

*(The first version of the header sentence said **8**. The tool said 9. The tool was right — this is the
fourth step in a row where a number written before the measurement was the thing that was wrong.)*

## 2. The second gap: a record that could only ever refuse

Adding the second set turned a rehearsal cell inside out. Until now the strongest cell in the battery was
*"point the real record at the real live tree — it must refuse"*, and the header said so: the record
describes the parked 574 bytes, the live tree holds the sleeper, and a verifier that accepted it would be
the defect `revert-set.txt` describes.

That cell still refuses, but for a different reason, and the reason exposes something the battery had no
way to test before: **nothing checked that the record could be right.** A verifier whose only demonstrated
behaviour is refusal cannot be told from one that is wrong about every directory it is shown. With the
armed set recorded, the same directory now gets **both verdicts**:

```
cell "live-tree-matches-armed-set" 0 "VERIFIED: 11 file(s) of armed-sleepless-696a0f39"  ...
  ok    live-tree-matches-armed-set exit=0  VERIFIED: 11 file(s) of armed-sleepless-696a0f39
  ok    live-tree-vs-the-other-set exit=1  stage90-qcdt.img hashes to
```

Same directory, same invocation shape, opposite verdict — the only thing that changes is which set is under
test. That pair is the property in full: **a verdict about a directory that does not name its set is not a
verdict about a set**, which is what `sha256sum -c` does to every park, and which is now the thing the
battery demonstrates instead of asserts.

## 3. And the refusal a park now produces needs to say which is which

The change has a cost, and it landed on a documented command. `docs/reference/recovery-and-rollback.md`
told a reader to run:

```bash
tools/verify_revert_set.sh /tmp/r594/frozen-payload
```

That command now exits **1** — the park is the 574 set, the record also carries the armed set, and the armed
comparison fails on 9 files. **The output is a wall of hash failures that is indistinguishable from rot**,
and a reader who reaches for the backup because the park "failed to verify" has been sent the wrong way by a
tool whose whole purpose is to tell them where they stand.

So the tool now keeps **per-set** failure counts and, on a refusal that has one clean set and one dirty one,
prints the distinction it has the evidence for:

```
  note  this directory matches frozen-574 exactly, and fails armed-sleepless-696a0f39. If it is the park of one of those
        sets, name it and the verdict is a pass:  --set=frozen-574
        The failures above are then not rot - they are this directory being a different arm.
```

The reference now names the set on both park commands, and says why in one line. This is a defect this step
**introduced** and then found by running the documented command rather than by re-reading it — the `--help`
lesson from 613, one step later, in a different command.

## 4. The cells, and the fixture that does not depend on which arm is in `out/`

The battery grew **35 → 38 ok / 0 failed**:

| cell | what it asserts |
| --- | --- |
| `live-tree-matches-armed-set` | the live tree passes against the set that describes it |
| `live-tree-vs-the-other-set` | the same directory refuses against the other set |
| `one-set-of-two-unnamed` | a two-set record over one directory refuses **and says which set matched** |
| `the-dirty-set-named` | naming the **dirty** set does not turn the run green, and prints no note |

The last two are built from fixtures rather than from `out/`, because a cell that depends on which arm
happens to be in the tree stops testing anything the moment the tree changes — 611's first defect, exactly.
They use a two-set record synthesised in the rehearsal, with a guard that refuses to run the cells at all if
the second set or its doctored line did not get written.

`one-set-of-two-unnamed` asserts the **note**, and the note is load-bearing: deleting it from the tool (and
nothing else) makes that cell the battery's single failure, `exit=1 but said nothing about: matches fixture
exactly, and fails other`. The tool was restored byte-identically afterwards. The rehearsal also gained a
guard on the armed set's name in the real record, so the pass cell cannot report a refusal instead of the
pass it is about.

## 5. What this does not do

* **It does not boot anything and changes no arm.** The parked set IS the live arm, byte-for-byte, so the
  press still sends `60063c47…`; a park is a copy and never the thing itself. The prediction
  (`xnu_live_poll_seq=3` with `poll_timeout_ms >= 1000`), the falsifier (`poll_seq` still 2) and the gate
  verdict are unchanged.
* **It does not re-run the gate against the park.** The authoritative test is still *revert, then run the
  gate*, and the park has been hashed against the record, not booted — the gate checks `out/`, which already
  holds the same bytes.
* **It does not record the 526/533 parks** in `out/stage90/frozen/`, which still use the older flat naming
  convention and are still unrecorded by design. The record now names them as uncovered rather than leaving
  a reader to assume the directory is complete.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus — last
  `usb 3-10:` disconnect with serial `4a2fe00b` at **2026-09-23 01:18:06**, `adb devices` and
  `fastboot devices` both empty, and the only phone-class device on the host is the neighbouring
  `05c6:9008` QDL on bus 003. The one event that can move the goal is still the user's **Vol-Down + Power**,
  and what this step adds is that the arm that press sends can now be lost only if someone also loses the
  park.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One record
extended (`stages/stage90/revert-set.txt` — a second 11-line set, the park list, and the reason that set
exists), one tool extended (`tools/verify_revert_set.sh` — per-set failure tally and the which-set note on
the refusal path), one rehearsal extended (`tools/rehearse_revert_set.sh` — two real-record cells, two
fixture cells, two guards), one reference paragraph (`docs/reference/recovery-and-rollback.md` — the park
commands now name their set), and **one park on disk** at
`out/stage90/frozen/armed-sleepless-696a0f39/` (11 files, 38 MB, copied and hash-compared in the same run).
Both parks verify: `--set=frozen-574` → **exit 0 / 11 files / 6 manifest checks**,
`--set=armed-sleepless-696a0f39` → **exit 0 / 11 files / 6 manifest checks**. The boot gate was re-run → **EXIT=0
/ 549 lines / 0 stderr**, unchanged; the rehearsal ran green → **38 ok / 0 failed**. The payload, the live
arm and the arm the next press sends are unmodified. `fastboot boot` only — never `flash` — so no outcome of
any of this can write to storage.
