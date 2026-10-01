# 893 — rung 61 corrected: `mi_mdev` follows `STAGE90_XNU_HFS_ROOT_MEDIA`, and the arm is parked

**A BUILD-PARK. No device touched.** 892 root-caused the rung-61 ENOEXEC to a conflict between two
rungs that had never been pressed together; this experiment is the one-line repair of the entry image,
the park of the corrected arm, and the readiness that says a press of it would be caught.
`out/stage90/` now holds **arm `armed-storage-1cb894c8`** — **NOT PRESSED.**

## 1. The defect (892, restated as one invariant)

`st_media_memdev_info` (`src/platform/stage90_root_media.c`) answers `DKIOCGETMEMDEVINFO`, the ioctl
`mockfs_mountroot` (`mockfs_vfsops.c:101`) branches on. 866 hardcoded `info->mi_mdev = 0`, which sends
mockfs's file node through `cluster_pagein → mockfs_strategy → buf_strategy → spec_strategy →
st_media_strategy`. Its written safety argument was that *both* branches read the same bytes, because
*"the strategy serves disk 0 from `g_stage90_ramdisk`."* **That sentence is true only off the HFS arm.**

882/891 set `STAGE90_XNU_HFS_ROOT_MEDIA=1`, which moves `st_medium_disk_base(0)` to the **volume**
(`g_stage90_root_hfs`). The rung-61 arm set both at once, so the strategy served the VOLUME while
`mi_mdev=0` forced mockfs onto that same strategy — process 1's executable read `0x482B` (HFS+
signature) instead of `0xfeedface`, and `exec_mach_imgact` returned ENOEXEC (8). **The base
`DKIOCGETMEMDEVINFO` names is dead when the flag is 0**, so 882's care to keep the base on the Mach-O
bought nothing while 866's flag stayed 0.

**The invariant the arm needs:** `DKIOCGETMEMDEVINFO` must describe the medium mockfs can exec from
(the Mach-O), **and** `mi_mdev` must be 1 on the HFS arm so mockfs actually uses it.

## 2. The repair

```
#if STAGE90_XNU_HFS_ROOT_MEDIA
    info->mi_mdev = 1;   /* the strategy serves the volume here, so mockfs must map the Mach-O from memory */
#else
    info->mi_mdev = 0;   /* 866: decline memory-backing; the strategy already serves the Mach-O off this arm */
#endif
    info->mi_base = (uintptr_t)g_stage90_ramdisk >> ST_MEDIA_PAGE_SHIFT;   /* the Mach-O, on both arms */
```

- **`HFS=0`** → `mi_mdev=0`, base `g_stage90_ramdisk` — **byte-for-byte 866/890**, the arm whose exec
  worked. Unchanged.
- **`HFS=1`** → `mi_mdev=1`, base `g_stage90_ramdisk`. Both mount paths now reach an exec:
  - `hfs_mountroot` returns 0 → the root is HFS+ and `/sbin/launchd` comes through the HFS reader →
    `cluster_pagein` → `st_media_strategy` → `g_stage90_root_hfs` (the volume, at the correct in-volume
    offset);
  - `hfs_mountroot` returns non-zero → **the fall-through** maps `g_stage90_ramdisk` from memory — the
    Mach-O at file offset 0 — so it execs too.

The repair is made structural in `tools/check_hfs_root_arm_split.py`, which now refuses an `mi_mdev`
that is not gated on `STAGE90_XNU_HFS_ROOT_MEDIA` or that does not carry both `mi_mdev = 1;` and
`mi_mdev = 0;`. `src/entry/build_entry.sh`'s `xnu_entry_866` clause is switch-aware: it expects
`mi_mdev == 1` under `HFS_ROOT_MEDIA=1` and `== 0` off it, so the linked image's own word is a build
refusal in both directions (`mi4-a-claim-in-a-comment-is-not-a-check`).

## 3. The arm

Entry image `xnu_arm_entry.bin` sha256 **`1cb894c8a3dab4b45809e63354c4bb100588814f0d2e71c176dedcf1d0aa3411`**,
6,109,852 B — the **same size as 892's `5c440167…`**, only the `mi_mdev` store differs. Entry switch set
(entry record `259375de…`): `STAGE90_XNU_STORAGE_PROBE=60`, `STAGE90_XNU_MOUNT=1`,
`STAGE90_XNU_HFS_ROOT_MEDIA=1`, `STAGE90_XNU_EMMC_STRATEGY=1`. The payload record is the same
`6c2b6038…` every rung since 23 has carried — only `STAGE90_XNU_ENTRY=1` is a payload switch and it did
not move. `stage90-qcdt.img` = `de743c30…`, 9,129,984 B.

Set **`armed-storage-1cb894c8`** parked at `out/stage90/frozen/armed-storage-1cb894c8/` (11 members +
`SHA256SUMS.txt`, absolute-pathed); the 11 `set=` lines are in `records/revert-set.txt`.

## 4. What was measured (no device)

| check | reading |
| --- | --- |
| `tools/check_set_name_rule.sh` | exit 0 — 75 sets; every `armed-storage-*` name is the entry image's hash |
| `tools/resolve_arm_set.sh out/stage90` | `armed-storage-1cb894c8` |
| `tools/verify_revert_set.sh … --set=armed-storage-1cb894c8` | VERIFIED, 11 file(s), 7 manifest checks |
| `tools/verify_press_ready.sh … --set armed-storage-1cb894c8` | **5 of 5**, gate flags `--allow-xnu-entry` |
| `scripts/preflight_boot_check.sh --allow-xnu-entry` | exit 0 |
| `make check` | exit 0 (`check_hfs_root_arm_split` prints the new mi_mdev clause) |

The readiness row "the press would be caught" reads `adb lists 4a2fe00b as 'device'` — so the catcher's
`usable()` is true and a press would be fired on. **THE PRESS IS THE OPERATOR'S; the standing reading is
that an autonomous press is authorized, envelope = `fastboot boot` only, nothing flashed, the `33e80afe`
device absent from both lists.**

## 5. What the press would answer

If the exec now succeeds, the boot either mounts the HFS+ root (a mounted root — the goal's storage
clause for the XNU track) or falls through to mockfs (890's state restored, HFS read path next). Either
way the ENOEXEC is answered: a run that reaches userland again means the fix was the word; a run that
still ENOEXECs with `_rootmedia_strategy_bytes=0x80000` means the file node's pages are still coming
from the volume and the HFS reader (not the mockfs branch) is the path to debug. **THE GOAL IS NOT MET
until a run execs an init and reaches userland off the root.**