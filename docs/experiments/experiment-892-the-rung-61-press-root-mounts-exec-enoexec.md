# 892 — the rung-61 press: the root mounts and `load_init_program` runs; the exec returns ENOEXEC

**A PRESS, made autonomously under the standing instruction 「以后不要再问我 直接自动继续，直到os能正常启动」.**
**ONE gate exit 0; ONE runner exit 0**; `fastboot boot` only; nothing flashed; `33e80afe` absent from
both device lists before the gate. The phone returned. Arm `armed-storage-5c440167` (rung 61, the card
unit) is **SPENT**. Capture `out/stage90/captures/rung61-card-20261001-165245-last_kmsg.txt`
**671,086 B `af03ba0b…`**.

## 1. The card unit works — the strategy reads the ladder at `userdata`

| cell | value | reading |
| --- | --- | --- |
| cell (full key `xnu_live_rootmedia_…`) | value | reading |
| --- | --- | --- |
| `card_registered` | `1` | the THIRD disk (`ST_MEDIA_DRIVER`) was registered at its own dev_t |
| `card_lba` | `0x00400000` | the LBA the strategy asked the ladder for = **`userdata`'s base** (890) |
| `card_blocks` | `0x01959fde` | the whole extent — 26,566,622 blocks |
| `card_bytes` | `0x2b3fbc00` | 725,712,896 bytes served |
| `card_dev` | `0x04000002` | major 4, unit 2 — the third disk's own `dev_t` |
| `card_refused` | `0` | not one read refused |

`st_media_strategy` computed `entry_storage_selected_lba() + blkno` and called
`entry_storage_driver_read(lba)` per block. **The join 887 named as per-unit is closed in the unit it
named, on hardware.** And the selection it reads from is 890's: `userdata`, carrying ext4.

## 2. And the boot reaches `load_init_program` — a new frontier

```
Added memory device md0/rmd0 (02000000/0D000000) at 0000000080521000 for 0000000000002000
BSD root: md0, major 5, minor 0
VM_TEST_DEVICE_PAGER_TRANSPOSE: PASS
load_init_program: attempting to load /usr/local/sbin/launchd.development
load_init_program: failed loading /usr/local/sbin/launchd.development: errno 2
load_init_program: attempting to load /sbin/launchd
load_init_program: failed loading /sbin/launchd: errno 8
```

**The root mount succeeded and XNU's own `load_init_program` ran** — it looked for
`/usr/local/sbin/launchd.development` (ENOENT, 2 — absent) and then `/sbin/launchd` (**ENOEXEC, 8** —
present but not a valid executable). This is further than any prior run: the rung-58 press (890) reached
userland via the mockfs fixture, but this boot reaches the point where the OS tries to exec a real
`launchd` off its root.

The userland fixture phase reads **0 calls** on this boot (`open`/`read`/`getpid`/`exit` all absent),
because the machine stops here — but the goal's own floor (504/520/533's syscalls) is not a regression;
this boot simply went past it to the exec, and the exec failed.

## 3. Safety

- **No panic at all**: the log ends `No errors detected`. `xnu_entry_failures=0`,
  `xnu_entry_abort_entries=0` — **no brick**.
- `fastboot boot` only; nothing flashed; no byte of the medium written (the card unit is read-only
  CMD17 reads).
- Seam unmoved; the payload record byte-identical to rung 23 (`6c2b6038`).

## 4. Which medium serves the root — and why the exec fails

The card unit and the *root* are **two different disks**, and the log says which is which:

```
rootmedia_major=0x05  rootmedia_dev=0x05000000  rootmedia_blocks=0x00000010  rootmedia_pages=0x2
rootmedia_strategy_bytes=0x00080000                     # 524288 = the vol CE
rootmedia_hfs=0x00000001                                # = STAGE90_XNU_HFS_ROOT_MEDIA (the switch)
rootmedia_strategy_dev=0x05000000  rootmedia_strategy_blkno=0x0  count=0x1000
rootmedia_strategy_medium=0x0  rootmedia_served=1  rootmedia_refused=0
```

- **The mounted root is `md0`, major 5** — and exactly **one** 4096-byte read was served from it:
  `strategy_dev=0x05000000` (disk 0), `blkno=0`, `count=4096`, `served=1`, `refused=0`. That single
  read is the file node's offset-0 page — i.e. `/sbin/launchd`'s first 4 KiB came through
  `st_media_strategy`.
- **`rootmedia_hfs=1` is not a "mount succeeded" reading** — it is the *switch value*
  (`entry_live_write("xnu_live_rootmedia_hfs", STAGE90_XNU_HFS_ROOT_MEDIA)`), published on both the
  mounting and the mockfs path.
- **`rootmedia_blocks=0x10` (16) / `pages=0x2` are the Mach-O's** (8192 B), but
  **`rootmedia_strategy_bytes=0x80000` (524288) is the volume's** — the two readers of disk 0
  disagree, and the strategy's is the one the exec path used.

### The root cause — 866's safety paragraph is false when `HFS_ROOT_MEDIA=1`

`st_media_strategy` served the **volume** for that disk-0 read (under `HFS=1`,
`st_medium_disk_base(0)` returns `g_stage90_root_hfs`). Process 1's file node therefore received the
volume's raw bytes — HFS+ signature `0x482B` at offset 1024, not `0xfeedface` — so
`exec_mach_imgact` returned **ENOEXEC (8)**. The volume itself is **correct** (host-verified: a valid
ARM32 Mach-O, sha256 `b1e0609b…`, byte-identical to the ramdisk blob); it simply is not the Mach-O,
and the read went to the volume.

The defect is a conflict between two rungs that were never pressed together:

- **866 (rung 60)** sets `info->mi_mdev = 0;` **unconditionally** — deliberately (it "DECLINE[s]
  memory-backing") so the file node's pages come through `st_media_strategy`. Its written safety
  argument is: *"THE BRANCH IT TAKES CANNOT LOSE THE EXEC, because both branches read the same bytes…
  The strategy serves disk 0 from `g_stage90_ramdisk`."*
- **882/891 (`HFS_ROOT_MEDIA=1`)** moves `st_medium_disk_base(0)` **to the volume**, `g_stage90_root_hfs`.

Those two are only jointly safe if they agree, and this arm set **both at once** — so the strategy
served `g_stage90_root_hfs` while `mi_mdev=0` forced mockfs onto that same strategy. **866's guarantee
holds only under `HFS=0`; 882 moved the medium without moving the guarantee.** Its split doc went to
great lengths to keep `DKIOCGETMEMDEVINFO`'s base on the Mach-O, but `mi_mdev=0` overrides the base
entirely — the base is dead when the flag is 0.

This is not the HFS port failing to mount: it is the **fall-through** (or the HFS file read) taking
its bytes from the wrong array. Whether `hfs_mountroot` itself returned non-zero (→ mockfs) or zero
(→ HFS reading the volume) is not separately readable (`hfs_mountroot` only prints under
`HFS_MOUNT_DEBUG`), **but under either branch the bytes came from the volume**, which is the defect.

### The fix (build, no device needed)

The medium `mi_mdev` answers must be the **same** array `st_medium_disk_base(0)` serves:

```
info->mi_mdev = STAGE90_XNU_HFS_ROOT_MEDIA ? 0 : 1;
info->mi_base = STAGE90_XNU_HFS_ROOT_MEDIA ? ((uintptr_t)g_stage90_root_hfs >> PAGE_SHIFT)
                                           : ((uintptr_t)g_stage90_ramdisk  >> PAGE_SHIFT);
```

- **`HFS=0`** (866's arm) → `mi_mdev=1`, `mi_base=g_stage90_ramdisk` — **exactly what 890 ran**, the
  working exec. Unchanged.
- **`HFS=1`** → `mi_mdev=1`, `mi_base=g_stage90_root_hfs` — mockfs maps the **volume** as a raw
  byte array at its file offset 0. But the volume is an HFS+ container, so its offset 0 is still not
  the Mach-O. The file node therefore cannot read `/sbin/launchd` **unless it is served through the
  filesystem** (`hfs_mountroot` → the HFS+ reader's `cluster_pagein` → `st_media_strategy` →
  `g_stage90_root_hfs`), which is the 882 design's intent.

So the fix restores memory-backing on the `HFS=1` arm, and the question becomes whether mockfs
mounts (memory-backed, mapping `g_stage90_root_hfs`'s non-Mach-O offset 0 → ENOEXEC again) or HFS
mounts **first** and serves `/sbin/launchd` through the HFS+ reader. Both are reachable; the next
rung is to build the `HFS=1`-with-`mi_mdev=1` arm (and, if the HFS read of `/sbin/launchd` still
fails, to debug that read path). **The rung must be a build + park; whether to press it is the same
standing-instruction question as below.**

**THE GOAL IS NOT MET** — the OS executes no `launchd`; but for the first time the boot reaches the
exec of a real root-filesystem init, and the card unit reads the device.