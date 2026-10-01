# 883 — THE RUNG-58 ARM REBUILT FROM THE TREE, BECAUSE 863'S PARK IS NO LONGER REVERTIBLE (and two defects that blocked the press)

**A build, a park and a hand-written record; NO device, no boot, no press, nothing flashed. THE PRESS
IS THE OPERATOR'S. THE GOAL IS NOT MET.**

In one paragraph: the step's job was to put `out/stage90/` back to the rung-58 storage arm so the
operator's next press would be the one experiment 867 section 3.1 names as the unblocker for the
eMMC-driver clause. The project's canonical mechanism is a **revert — `cp -r` from a park, no build** —
but the parked rung-58 arm, 863's `armed-storage-b00b87bb`, **cannot be reverted onto today's tree**:
its entry image predates edits to three `src/entry/` sources, and `preflight_boot_check.sh`'s sources
clause refuses it, correctly. Rebuilding the entry image with 863's *own* switch set produced a
different bin of the same size, so the arm is not byte-reproducible across those edits. The step
therefore rebuilt the arm from the tree that exists — **the same experiment** (`PROBE=57`, `MOUNT=0`,
`HFS_ROOT_MEDIA=0`), a new artifact `armed-storage-5936b246` — and, in the course of making it
pressable, found and repaired a **second** defect: 882 had made every **pre-882 parked arm unpressable.**

## 1. Why the revert the plan called for did not work

`preflight_boot_check.sh` refuses an entry image whose recorded source manifest no longer matches
`src/entry/`: *"the entry image is not the build of these sources: build_entry.sh entry_storage.c
entry_trace.c."* 863's arm was built 05:24; `git log` shows those three files were edited after it by
**864** (rung 59, 06:48), **866** (rung 60, 09:05), **877** (10:20) and **882** (13:14). The clause's
own remedy is precise and safe — *"rebuild the entry image with the switches this arm needs … if
`sha256sum xnu_arm_entry.bin` still equals the record's `STAGE90_XNU_ENTRY_SHA256`, the changed source
fed no compiler input"* — so the test is empirical, not argued.

**Measured**: rebuilding the entry image with 863's switch set (`PROBE=57`, `MOUNT=0`,
`HFS_ROOT_MEDIA=0`, `ISTACK_SEPARATE=0`, `POST_END_TICKS=115200000`) produced a bin of the **same size**
(5,585,564 bytes) and a **different hash** — `5936b246…`, not `b00b87bb…`. So the later edits **did**
reach the compiler (rung 59's `entry_root_media_mount_disk`, rung 60's `mi_mdev`, 882's HFS switch), and
863's park is a **record of what 863 measured** rather than a revertible target. This is the
[`mi4-not-absent-its-build-output`] distinction read the other way: a park is bytes, and bytes built
from a tree that has moved are not the build of the tree in front of you.

## 2. Defect A — 882 made every pre-882 parked arm unpressable

882 added `STAGE90_XNU_HFS_ROOT_MEDIA` to `ENTRY_CFG_KEYS` in `scripts/preflight_boot_check.sh` as an
**unconditional** required key. Every arm parked before 882's build writes a record that predates the
key, so `awk` returns empty and the gate refused the rung-58 arm on a key its build could not have
written:

```
REFUSING: out/stage90/xnu_arm_entry-config.txt has no STAGE90_XNU_HFS_ROOT_MEDIA line
```

Measured scope: of the parked `armed-storage-*` arms, **all but 882's own** (`65c6424b`) lack the key.
882's section 8 checked only its *live* arm and never re-ran the gate against an older park, so the
regression was invisible in the step that introduced it — the same shape as
[`mi4-a-claim-in-a-comment-is-not-a-check`], a "nothing moved" claim that was never a check.

**The repair is grounded on the artifact, not the calendar.** The entry image carries
`entry_root_media_hfs_root_arm_on` / `..._off` **unconditionally** (882's own section 8), so the gate
now reads the entry ELF and requires the key **only when that image carries the HFS root arm**. An image
that cannot carry the arm has no such value to record, and demanding one from it refuses a **correct**
record. The key stays in `ENTRY_CFG_KEYS`, so the converse check still accepts a post-882 record that
carries it. On the rebuilt rung-58 arm the row now prints:

```
STAGE90_XNU_HFS_ROOT_MEDIA=(absent, and this entry image carries no HFS root arm to name - the key is N/A here)
```

## 3. Defect B — the payload carries the entry image, so the entry image cannot be rebuilt alone

Once the entry image was rebuilt (new bytes, same length), the gate refused again: *"the bytes … are
NOT the bytes of this … while its own compiled-in blob is N … so what it carries at that length is a
DIFFERENT build of the entry image rather than an absent blob."* The payload embeds its entry image
(`build.sh` regenerates `xnu_arm_entry_blob.c` from `xnu_arm_entry.bin`), so a rebuilt entry image
orphans the payload that carries the old one. The remedy is the one the clause names: rebuild the
payload — `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh`. **The payload is not
byte-reproducible (408)**, so the payload-side members are new bytes too; the walk found no other
coupling.

## 4. What the rebuilt arm is, and what it is not

- **It is the same experiment as 863**: `STAGE90_XNU_STORAGE_PROBE=57` (VALUE 57 = ORDINAL RUNG 58),
  `STAGE90_XNU_MOUNT=0` (the storage ladder, not 862's mount track), `HFS_ROOT_MEDIA=0` (882's split is
  absent), and its **payload switch record is `6c2b6038…`, byte-identical to rungs 23..59** — no payload
  switch moved. It carries rung 57's selection *and* 863's repair of rung 57's two-witness defect, so a
  press would read `_dp_next_is_arg = 1` with `_dp_part_is_first = 0`.
- **Its entry image is a new artifact** (`5936b246…`), a record of a *distinct* build. It is **not**
  `b00b87bb`, and 863's prose names `b00b87bb`; the two are the same experiment at two trees.
- **The entry-build clauses validated it**: `xnu_entry_678`'s arm-key set (20 keys + 2 artifact keys),
  `xnu_entry_533`'s config/manifest binding to `5936b246…`, and `xnu_entry_857`'s `st_data_part` body
  check all ran green in the build.

## 5. The record

- Park `out/stage90/frozen/armed-storage-5936b246/` (11 members, plain `cp -p`).
- `records/revert-set.txt`: `set=armed-storage-5936b246 …` (11 member lines), written by hand in this
  step, with the header stating why 863's `b00b87bb` could not be used.
- `tools/check_set_name_rule.sh` exit 0; `tools/verify_revert_set.sh … --set=armed-storage-5936b246`
  **VERIFIED, 11 files, 7 manifest checks**.
- `tools/verify_press_ready.sh … --set armed-storage-5936b246` — **5 of 5**, gate flags
  `--allow-xnu-entry`.
- `make check` exit 0.

## 6. What this is not

A **parked** arm is a record, not a queue. **No press has been made; the press is the operator's.**
`fastboot boot` only, never flash. And even a successful press does not meet the goal: naming the
mountable partition is not a mounted filesystem. **The eMMC driver (867) is still open — this press is
what unblocks building it — and TWRP-to-storage stays done-and-separate (860). THE GOAL IS NOT MET.**