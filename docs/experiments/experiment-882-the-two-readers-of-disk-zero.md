# 882 — THE TWO READERS OF DISK 0, AND THE ARM THAT SERVES THEM DIFFERENT BYTES (built and parked host-side)

**A build and a park, and nothing else; PARKED, NOT PRESSED, NOT ARMED.** No device, no boot, no
press. The rung 57–60 arms and 862's mount arm stay parked and unpressed. **THE PRESS IS THE
OPERATOR'S. THE GOAL IS NOT MET.**

In one paragraph: 881 built an HFS+ root image and laid out the arm that mounts it. This step builds
that arm. Its whole subject is that **`hfs_mountroot` and `mockfs_mountroot` read the same disk 0
through the same `st_media_strategy`**, and the safety of an HFS-first root depends on those two
readers being handed **different bytes** — the volume to HFS, the Mach-O to mockfs — or a failed HFS
mount would map 524288 bytes of HFS+ as process 1. The split is a build refusal
(`tools/check_hfs_root_arm_split.py`), the image is bounded at build time
(`src/entry/build_entry.sh`), the blob is validated by file and by linked object
(`tools/check_hfs_root_blob.py`), and the arm is recorded as `armed-storage-65c6424b`. Along the way
the step found and repaired a **build-blocking gate defect that had been latent since the HFS+
staging landed**: the external-tree rule refused the state the stager is *supposed* to produce.

## 1. The two readers, and why one strategy cannot serve both

`stage90_root_media.c` registers a device at disk 0 whose `strategy` is `st_media_strategy`. In 866's
arm (`STAGE90_XNU_HFS_ROOT_MEDIA=0`) that strategy serves `g_stage90_ramdisk`, which is exactly what
`DKIOCGETMEMDEVINFO` also reports — both branches read the same array, so `mockfs`'s file node gets
`MH_MAGIC` whether the pager mapped it or the strategy copied it.

An HFS+ root needs the strategy to serve the **volume**. But `hfs_mountroot` reaching it is not the
only thing that happens: `vfs_mountroot` walks `vfstbllist[]`, finds the HFS row (879) before mockfs,
tries it, and on failure **falls through to mockfs**, whose file node's pages come through
`cluster_pagein` → `mockfs_strategy` → `buf_strategy` → `spec_strategy` → the *same* `st_media_strategy`.
If that strategy answered the volume on both paths, a failed HFS mount would feed 524288 bytes of HFS+
to `exec_mach_imgact` and the run would panic at `load_init_program`.

**The two readers are separated by which ioctl they use**, and the split is therefore structural
rather than conditional:

| reader | path | answered from |
| --- | --- | --- |
| the mount path | `st_media_strategy` → `st_medium_disk_base(0)` | `g_stage90_root_hfs` (the volume, when the switch is on) |
| the exec path | `DKIOCGETMEMDEVINFO` → `st_media_memdev_info` | `g_stage90_ramdisk` (the Mach-O, on **every** arm) |
| the exec path | `DKIOCGETBLOCKCOUNT` → `spec_open` → `si_devsize` | the Mach-O's length |

`st_media_memdev_info` is **unchanged from 866's** — it never consults `st_medium_disk_base`. That is
the property the build refuses to lose.

## 2. The refusals, and where each lives

**`tools/check_hfs_root_arm_split.py`** (in `make check`) reads the *body* of `st_media_memdev_info`
with `strip_comments` + brace counting and refuses one that derives from `st_medium_disk_base`, names
`g_stage90_root_hfs`, or drops `g_stage90_ramdisk`; and refuses a `st_medium_disk_base` that does not
name **both** arrays or does not name the switch. A paragraph saying "these must not be the same
reader" is a claim in a comment; this is the check.

**`tools/check_hfs_root_blob.py`** (in `make check`) validates the artifact in both forms — the raw
file and the linked object's `.data.hfsroot`: signature `0x482b`/v4, `blockSize` 4096, the 128-block
total fitting the array, the content-protection attribute bit clear, the `catalogFile` first extent,
and a catalogue `nodeSize` ≠ 512.

**`src/entry/build_entry.sh`** reads the **linked** platform object with bare `nm` and:

- refuses unless it carries `entry_root_media_hfs_root_arm_on` / `..._off` matching
  `STAGE90_XNU_HFS_ROOT_MEDIA` — the marker is unconditional, so a build cannot silently produce the
  other arm;
- on `HFS_ROOT_MEDIA=1`, refuses an image whose `g_stage90_root_hfs` span is not **524288 bytes**, is
  not 4096-aligned, or does not sit below `bss_start`, below the entry image's end and below
  `topOfKernelData`.

## 3. `nm -S` prints no size for a `.set`-ended section, and the check printed a blank number

The first draft of the span check read the size column of `nm -S`. The blob is emitted by
`xnu_arm_entry_root_hfs.s` with `.set g_stage90_root_hfs_end, .`, so the whole section span belongs to
the **END** symbol:

```
80523000 D g_stage90_root_hfs
805a3000 D g_stage90_root_hfs_end
```

`nm -S` therefore prints a size on `_end` and **none on the base**. The check compared `""` against
524288, and `(( "" == 524288 ))` is a **shell arithmetic error** — which printed the refusal message
with an empty number in it: `FAIL: the linked HFS+ root volume is  bytes, not the 524288`. The
message looked like a check that had measured something. It had not. The repair computes
`hfs_end_va - hfs_va` from the two **bare** `nm` addresses, and the misread is recorded in a comment
at the site.

Note the column shift that makes this easy to get wrong a second time: bare `nm` is
`$1=addr $2=type $3=name`; `nm -S` is `$1=addr $2=size $3=type $4=name`.

## 4. The record-writer read the empty environment variable, not the resolved one

The arm read-back `case` in `build_entry.sh` gained `STAGE90_XNU_HFS_ROOT_MEDIA) _v=$HFS_ROOT_MEDIA ;;`,
but the record writer was first written as `echo "STAGE90_XNU_HFS_ROOT_MEDIA=${STAGE90_XNU_HFS_ROOT_MEDIA}"`.
`HFS_ROOT_MEDIA` is the **resolved** local (defaulting to 0); the environment variable is usually
unset, so the record got an empty value, and the gate's `ENTRY_CFG_KEYS` clause then refused with
*"out/xnu_arm_entry-config.txt does not carry key(s) ENTRY_ARM_KEYS requires"*. This is
`mi4-off-option-two-spellings` one layer up: **the switch has two spellings — the environment binding
and the resolved local — and only one of them is the arm.**

## 5. `ISTACK_SEPARATE` defaults to 1, unlike every other switch

`build_entry.sh:391` sets `STAGE90_XNU_ISTACK_SEPARATE` to **1** when unset. Both parked baselines
below this arm carry `=0`, and the build refuses `ISTACK_SEPARATE=1` alongside `STAGE90_XNU_IDLE_STACK=1`
("two state changes in one image"). So unsetting the environment variable is not neutral here — an
`env -u` leaves the switch at 1 and the build refuses, and the arm has to be built with the switch
carried **at 0** explicitly, the same value both baselines carry.

## 6. The external-tree gate refused the state the stager is supposed to produce

`scripts/xnu_compile_graph_scan.py` and `scripts/xnu_link_proof.sh` both refused **any** dirt under
`external/*`. That rule is experiment 155's — a checkout under `external/` is never written — and the
HFS+ port (868–879) changes it *on purpose* through a tracked, re-appliable generator,
`tools/stage_hfs.sh`. The rule had been latent since the staging landed: 879, 880 and 881 never built
the payload, so nothing had run the gate against a staged tree.

The replacement is **not** "the tree may be dirty". It is that the tree must hold **exactly what the
stager wrote**, witnessed by a record the stager writes on every run:

```
STAGE_HFS_RECORD=$PWD/src/supply/hfs_tree_status.txt tools/stage_hfs.sh
```

`stage_hfs.sh` writes `git -C "$DST" status --short --untracked-files=all` to that path
unconditionally; the gate diffs the **live** status against it. A fresh write is what makes the string
trustworthy — a tree that has moved since the stager ran no longer compares equal, so a hand edit is
caught, which is the property 155's rule was protecting. `external/xnu-upstream` (the source every
staged file is copied **from**) keeps 155's rule unchanged, because it is read-only for this port.

`--untracked-files=all` is load-bearing rather than tidy: the default collapses the 36 files under
`bsd/hfs/` to the single line `?? bsd/hfs/`, which cannot distinguish the stager's own output from
that plus a stray file inside the same directory. An earlier draft that tested membership against
`src/supply/hfs_files.txt` was **refused on the correct state** for exactly this reason — it could not
see inside the collapsed line, and the list does not name `bsd/machine/spl.h` or
`bsd/vfs/vfs_journal.h`, which are staged but not compiled.

**The honest scope of the check**: it binds the tree to *a write by this stager*, not to *a correct
staging*. `tools/check_hfs_staged.sh` still re-derives the footprints it can see, and neither tool can
tell that the stager itself is right — that is what the port's compile count (877) and the root row's
guard (874) are for.

## 7. The gate, the park, and one row that had to be *guarded* rather than added

This arm differs from 866's parked rung-60 arm in exactly **one entry switch** — `STORAGE_PROBE=59`,
`MOUNT=1` identical. `tools/verify_press_ready.sh`'s narration table keys the rung-60 row on
`$wst == 59`, so without a change it described **866's arm** for this image — readable prose, one arm
wrong, the shape that has bitten this file five times (683, 686, 690, 692, 713). The fix is a new
parser for `STAGE90_XNU_HFS_ROOT_MEDIA`, and the rung-60 row is **guarded** with `$whfs == 1` refusing
it rather than a second row appended below, which could never be reached.

`scripts/preflight_boot_check.sh`'s `ENTRY_CFG_KEYS` gained the new key (twenty-first name) and its
converse clause would have refused the record without it.

Status:

- Park `out/stage90/frozen/armed-storage-65c6424b/` (11 members, plain `cp -r`; the manifest is
  absolute-pathed, so **never `sha256sum -c` against it**).
- `records/revert-set.txt`: `set=armed-storage-65c6424b …` (11 member lines). The set name is the
  sha256 prefix of its own `xnu_arm_entry.bin`; `tools/check_set_name_rule.sh` exit 0.
- `tools/verify_revert_set.sh --set=armed-storage-65c6424b …` — **11 ok / 0 failed**.
- `tools/verify_press_ready.sh --park … --set=armed-storage-65c6424b` — **5 of 5**, gate flags
  `--allow-xnu-entry`.
- `make check` exit 0, with both new tools green.

## 8. Nothing moved that must not move

- The **blob inside the linked entry image is byte-identical to the artifact**: slicing
  `.data[g_stage90_root_hfs .. g_stage90_root_hfs_end]` out of `xnu_arm_entry.elf` gives sha256
  `3fb9273c8db4b18237c9b6bc1afdb151991244c08d4ac13e3c8bd06e30b95d7b`, the same digest as
  `src/entry/blob/xnu_arm_entry_root_hfs.img`. (The two are 12288 bytes apart within `.data`, which is
  the 4096-byte alignment on `g_stage90_root_hfs` plus the 8192-byte `g_stage90_ramdisk` before it.)
- The entry-group page move did **not** fire: `STAGE90_XNU_SEAM_LR` stays `0x8004d2dc` in
  `entry_trace.c` and `EXIT_POP_LR_LITERAL` in `run_and_capture.sh`.
- `STAGE90_XNU_PWR_WAIT_TICKS=384000` inherited from rung 9, unchanged.
- **The payload switch record is NOT byte-identical to rung 23's, and that is recorded rather than
  papered over**: it carries `STAGE90_XNU_ENTRY=1` for the first time. The payload had to be **rebuilt
  with the entry flag** after `check_payload_config_entry` refused a payload whose entry path was
  switched off (`#define STAGE90_XNU_ENTRY 0u`) — the build that never reaches XNU. That is the check
  doing exactly what it was written for.
- **An uncommitted-work loss, and the proof of its recovery**: in the course of this step the ~500
  lines of uncommitted `build_entry.sh` work were destroyed by a `cp` of my own with no backup, and
  re-applied by hand. The recovery is proved by rebuilding **both arms** from the re-applied script:
  the on arm reproduces **byte-for-byte** at sha256 `65c6424bafc14b1cb1af3cf2ff2e093e9871812023033cf823af1bc896eef251`,
  6,109,852 bytes — the same digest the park records; and the off arm (the rung-60 switch set with
  only `HFS_ROOT_MEDIA=0`) builds to `95686647736a3888aa7560cc90a0befdaa84775476b52ebd234df36ef384860a`,
  5,585,564 bytes, with **zero `g_stage90_root_hfs` symbols** in the linked ELF and
  `entry_root_media_hfs_root_arm_off` in the platform object — so the switch really binds the artifact
  and not just the record.
- **Two interlocks fired during that check and both were right**, which is why they are worth naming:

  - **the arm-change refusal** ("this build would change which arm `out/xnu_arm_entry.bin` is, and no
    deliberate change was asked for") — a blind `./build_entry.sh` does not reproduce the arm on disk,
    because four of its switches are off their defaults. `STAGE90_ENTRY_ARM_CHANGE=1` is the opt-in.
  - **the one-object/two-scripts check** ("the platform block compiled `stage90_root_media.c` with
    `STAGE90_XNU_HFS_ROOT_MEDIA='1'` and this build's is `'0'`") — the module object and the entry
    build must carry the **same** switch, and it refused rather than let a module that serves the
    volume be linked under a build that believes it serves the Mach-O. That is the split of §1 guarded
    a second time, at the object boundary rather than at the source.
- A **pre-existing** defect at HEAD, found while checking the off arm and *not* introduced here: with
  `STAGE90_XNU_STORAGE_PROBE=0` the entry build fails `-Werror` on `entry_storage.c`'s `st_read8` /
  `st_read16` / `st_write32` (`defined but not used`). No arm in this ladder builds at `PROBE=0`, so
  it has never fired; it is recorded as a fact rather than repaired here.

## 9. What this is not

A **parked** arm is a record, not a queue; re-pressing a spent arm buys nothing and this one has never
been pressed. And even a successful press would not meet the goal: a root reachable through the
payload's own synthetic medium is **not** an eMMC driver that moves a byte off the card. The medium
(867) is still open, and the cprotect engine (`cp_register_wraps` has no caller) is still owed — so
**TWRP-to-storage stays done-and-separate (860), and THE GOAL IS NOT MET.**