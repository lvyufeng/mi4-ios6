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

**The safety property — why this cannot brick or regress the boot.** `vfs_mountroot`
(`vfs_subr.c:1069-1080`) walks `vfstbllist[]` and calls each row's `vfc_mountroot`, breaking on the
**first** that returns 0. 879 put the HFS row **before** mockfs. So:

- HFS mounts → the root is the HFS+ image; the exec reads `/sbin/launchd` from it (the branch this arm
  is testing).
- HFS fails (bad image, a port runtime bug, a stub returning garbage) → `hfs_mountroot` returns non-zero,
  the walk **continues to mockfs**, and the boot is **exactly today's boot** — the 8 KB RAM disk, the
  Mach-O, process 1. **A failed HFS mount is a fall-through, not a panic.**

That is the property that makes this arm worth building before the medium is real: it exercises the
whole HFS+ mount path on hardware **without giving up the working boot**. Nothing is written to any
partition; the image is RAM the payload already owns.

## 4. What this step does NOT build (the arm itself), and why

The image is built and the shape is fixed. Wiring B (the medium) is the next step and it is **not done
here**, for one measured reason at the time of writing: **`hfs_mountroot`'s exact block-device
requirements are being mapped** (the ioctls it issues before its first read, the block size it assumes,
whether it needs `VFS_STATFS`) so the medium answers them correctly rather than by guess. The medium
already answers the twelve `vfs_init_io_attributes` ioctls and `DKIOCGETMEMDEVINFO`; what is left is
the HFS-specific set, and building against a guessed set is the "stand-in the right size and the wrong
value" class ([`mi4-stand-in-size-is-not-value`]).

## 5. The honest bound

**An image and a plan; no arm, no build of the kernel, no device.** A successful HFS+ mount still needs
the port's runtime to work — 878 closed the *compile* gap, but the cprotect layer it restored is
**declarations**, and the cprotect **engine** (`cp_register_wraps` has no caller) is still owed; the
shims `src/supply/stage90_hfs_shims.c` define ten symbols **empty** where 4570 dropped the state they
wrote. Whether those matter for a **mount** (as opposed to a full working filesystem) is exactly what
the arm measures. **THE GOAL IS NOT MET.**