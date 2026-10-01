# 881 — THE HFS+ ROOT IMAGE, AND THE ARM THAT MOUNTS IT (design + the image, built host-side)

**A host-side build of an image and a plan: no device, no boot, no arm, no park, no press.** The rung
57–60 arms stay parked and unpressed. **THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: 880 established that XNU's root and the medium's filesystem must be the same
filesystem, and that this device carries only ext4/f2fs — so the medium must **carry** an HFS+ volume,
which this repository has to build. This step builds it: `tools/build_hfs_root_image.sh` makes a 512 KiB
HFS+ image whose `/sbin/launchd` is the **same 8192-byte Mach-O** `entry_ramdisk.s` already carries, so
the process 1 that boots today can be exec'd from HFS+ instead. It then lays out the arm that mounts it,
and — the load-bearing safety result — shows the arm is **non-destructive by construction**: the HFS row
(879) is *before* mockfs, so `vfs_mountroot` tries HFS first and falls through to the working mockfs boot
if HFS fails. An HFS-first arm cannot brick or regress the boot.

## 1. Why the medium must be built, not found

880, device-verified: `blkid` says `mmcblk0p25` (`userdata`, the partition rung 57 selects by largest
extent) is **ext4**; no partition is HFS; the only filesystem this repo can port is **HFS+**. So the
routes are (a) format a device partition HFS+ — a destructive write, the operator's; (b) stage an HFS+
image the kernel reads; (c) port ext4 — no source. **This step takes route (b), which needs no device
write**, and builds the image it needs.

## 2. The image, and why an image is the whole of it

`tools/build_hfs_root_image.sh`:

1. `mkfs.hfsplus -v STAGE90ROOT` a 512 KiB zeroed file → a valid HFS+ volume (signature `0x482B` at
   sector 2, blockSize 4096, totalBlocks 128). 880 measured 512 KiB is the smallest `mkfs.hfsplus` will
   make **and** holds `/sbin/launchd` with a byte-exact round-trip.
2. loop-mount it (`losetup` + `mount -t hfsplus`) and `cp` in `sbin/launchd`. **`hfsutils`' `hpmount`
   is not used** — it refuses these images ("This is not a HFS+ volume", measured); the Linux kernel's
   HFS+ driver reads them, so the populate step is a loop mount. It touches only the image file.
3. `cmp` the bytes back **through the filesystem**, and read the volume header out of the finished image.
   Both must pass or the script exits non-zero.

The `launchd` bytes are **not** a second copy: the script extracts them from
`out/stage90/xnu_arm_entry_ramdisk.o` (section `.data.ramdisk`), the object `entry_ramdisk.s` already
produces — so the image's process 1 and mockfs's process 1 are one definition
([`mi4-one-value-two-definitions`]). Measured: the resulting image's `sbin/launchd` is 8192 B and
byte-identical to that object's section.

## 3. The arm's shape, and the one property that makes it safe

The current root is mockfs over `g_stage90_ramdisk` (the raw Mach-O, mapped straight onto memory; no
filesystem in the path). To mount HFS+ instead:

| # | change | where |
| --- | --- | --- |
| A | **enable the port**: build with `STAGE90_XNU_HFS=1` | `build_xnu_arm_kernel.sh:103` |
| B | **the image is the medium**: disk 0's medium becomes the HFS+ image (its bytes linked into the payload, `g_stage90_ramdisk`'s role), and `st_medium_disk_bytes(0)` returns its length | `stage90_root_media.c` |
| C | **the HFS row is reached first**: it is already *before* mockfs (879), and the port is on, so `vfs_mountroot` calls `hfs_mountroot` before `mockfs_mountroot` | `vfs_conf.c` (879's patch) |
| D | **C is sufficient only if B is real**: `hfs_mountroot` reads blocks through `buf_strategy` → `st_media_strategy`, which today serves disk 0's whole RAM disk and disk 1's one staged sector | `stage90_root_media.c:324` |

**The safety property — and the correction the maps forced on it.** `vfs_mountroot`
(`vfs_subr.c:1069-1080`) walks `vfstbllist[]` and calls each row's `vfc_mountroot`, breaking on the
**first** that returns 0. 879 put the HFS row **before** mockfs, so **order** gives fall-through: if
`hfs_mountroot` returns non-zero the walk continues to mockfs.

**But order is not enough, and this is the correction.** The mockfs fall-through only reaches a working
exec if disk 0 still serves it the **Mach-O**: `mockfs_mountroot` asks the root device
`DKIOCGETMEMDEVINFO` and maps its one file node onto `mi_base << 12`; today `mi_base` is
`g_stage90_ramdisk`, the Mach-O. HFS, by contrast, reads the **same root device** through `buf_strategy`
→ `st_media_strategy`, and for it to mount the image the strategy must serve the **image**. So the two
rows want disk 0 to serve *different bytes by different access paths*:

| reader | access path | bytes it needs |
| --- | --- | --- |
| `hfs_mountroot` | `buf_meta_bread` → `VNOP_STRATEGY` → `st_media_strategy` | the **HFS+ image** |
| `mockfs_mountroot` | `DKIOCGETMEMDEVINFO` → `mi_base << 12`, mapped | the **Mach-O** |

**The medium must answer them differently** for the arm to be safe: `st_media_strategy` serves the image
while `st_media_memdev_info` keeps answering `mi_mdev = 1`, `mi_base = g_stage90_ramdisk >> 12` so the
mockfs fallback still maps the Mach-O. (866 set `mi_mdev = 0` to force mockfs *through* the strategy for
the driver goal; this arm deliberately **reverses that one word**, because here the strategy is the HFS
path and mockfs must stay on its own.) With that split:

- HFS mounts → the root is the HFS+ image; the exec reads `/sbin/launchd` from it (the branch under test).
- HFS fails → `hfs_mountroot` returns non-zero, mockfs mounts, maps the Mach-O, and the boot is **exactly
  today's boot**. **A failed HFS mount is a fall-through, not a panic.**

Without the split — if disk 0 served the image to *both* paths — a failed HFS mount would also break
mockfs (it would map the image, not a Mach-O, as its "executable"), and the boot would panic at
`load_init_program`. So the split is **load-bearing for the safety property**, and the arm must carry a
check that `st_media_memdev_info`'s disk-0 answer still names the Mach-O while the strategy serves the
image. Nothing is written to any partition; both are RAM the payload already owns.

## 4. What the maps found, and what is left to wire

The three maps (this step's) settled the medium's surface:

- **The medium already answers every ioctl the mount requires.** `hfs_mountfs` mandates
  `DKIOCGETBLOCKSIZE` and `DKIOCGETBLOCKCOUNT` (`hfs_vfsops.c:1326,1373`; both `ENXIO` if unserved), and
  a first read of **512 bytes at byte offset 1024** (sector 2, `HFS_PRI_SECTOR(512)=2`). `st_media_ioctl`
  answers both, and `st_media_strategy` serves `b_blkno * 512` — so sector 2 is byte 1024 for it too.
  `DKIOCGETPHYSICALBLOCKSIZE`, `DKIOCGETFEATURES`, `DKIOCISSOLIDSTATE`, `DKIOCISVIRTUAL` may return
  `ENOTTY` and are tolerated. **Nothing new is required of the ioctl set.**
- **The image is panic-safe and needs no cprotect engine for a mount** (§3a below): B-tree node size
  4096 (not 512, which `BTree.c:272` panics on), content-protection attribute bit 30 clear (so the NULL
  `g_cp_wrap_func` deref 878 flagged is not reached), `lastMountedVersion` ≠ `'HFSJ'` (no journal work),
  and the pure-HFS+ branch (`drEmbedSigWord` = 0).
- **What is left is the disk-0 byte split of §3** — `st_media_strategy` serving the image while
  `st_media_memdev_info` keeps naming the Mach-O — plus putting the image's bytes where the payload can
  read them (the `g_stage90_ramdisk` role). That is the arm, and it is the next step.

## 3a. The image's own structure, measured

`tools/build_hfs_root_image.sh`'s output was read structurally (not just magic-checked):

```
VH:  sig=0x482b ver=4 blockSize=4096 totalBlocks=128 free=77
     attributes=0x80000100  (bit30 content-protection = 0, bit8 unmounted = 1)
     lastMountedVersion = 'H+Lx'  (not 'HFSJ'; journaled bit clear)
     drEmbedSigWord @1024+124 = 0x0000  (pure HFS+, not the embedded-MDB branch)
     allocationFile 1 blk, extentsFile 8 blk, catalogFile 8 blk, attributesFile 16 blk, startupFile 0
     catalog node0 nodeSize=4096, extents node0 nodeSize=4096
```

`cat_idlookup(kHFSRootFolderID=2)` must resolve the root folder from this catalog for the mount to
succeed, and the full path (`sbin`→`launchd`) for the exec — `mkfs.hfsplus` writes those, which is why a
**real formatter** was used rather than a hand-built tree.

## 5. The honest bound

**An image and a plan; no arm, no build of the kernel, no device.** A successful HFS+ mount still needs
the port's runtime to work — 878 closed the *compile* gap, but the cprotect layer it restored is
**declarations**, and the cprotect **engine** (`cp_register_wraps` has no caller) is still owed; the
shims `src/supply/stage90_hfs_shims.c` define ten symbols **empty** where 4570 dropped the state they
wrote. Whether those matter for a **mount** (as opposed to a full working filesystem) is exactly what
the arm measures. **THE GOAL IS NOT MET.**