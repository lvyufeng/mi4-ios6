# 965 — the real iOS 7.1.2 rootfs is HFSX, and the proven serving cannot hold 896 MiB (2026-10-09)

The 913 pivot made the **real, decrypted iOS 7.1.2 rootfs** the target — it lives, device-free, at
`/mnt/data/ios7-payload/v{1,2}/ios7/rootfs.hfs` (896 MiB; v2 = `c9b9080d…`, the `finite-app-launch070`
baseline). But every mount/exec rung this project has *pressed* (903/906/908) ran against a **512 KiB,
hand-built, HFS+ v4** fixture (`src/entry/blob/xnu_arm_entry_root_hfs.img`). This rung is the
host-side measurement of the gap between the two — reading the real volume header and asking our own
checker to mount it. No device action.

## 1. Measured: the real rootfs is **HFSX**, our fixture is **HFS+**

Both volume headers, read host-side (`rootfs.hfs` @0x400, device-free):

| field | real iOS 7.1.2 `rootfs.hfs` (v2) | our pressed fixture |
|---|---|---|
| signature | **`0x4858` = `kHFSXSigWord` 'HX'** | `0x482B` = `kHFSPlusSigWord` 'H+' |
| version | **5** (`kHFSXVersion`) | 4 (`kHFSPlusVersion`) |
| blockSize | 4096 | 4096 |
| totalBlocks | 229376 | 128 |
| volume bytes | **939524096 (896.0 MiB)** | 524288 (512 KiB) |
| fileCount / folderCount | 7142 / 2850 | 1 / a few |
| catalogFile | logicalSize 6291456 B, 1536 blocks, first extent (9220,768) | blk 26 +8, nodeSize 4096 |
| attributes | `0x80000100` (unmounted set, **journaled clear**) | `0x80000100` (same) |
| lastMountedVersion | `'fsck'` | (freshly made) |

**Our fixture is a different filesystem *family* from the target.** HFS+ v4 and HFSX v5 share a
layout but differ in the one property the driver branches on: case sensitivity. `hfs_format.h:58`:
*"'HX' volumes start with version 5."* The 913 decision to target this exact rootfs therefore changes
what the mount path must survive, and that change has never been exercised.

**The lab-analysis doc mislabeled the signature.** `docs/reference/hd2-ios7-lab-analysis.md:24` says the
volume header *"begins `HX 0005` (HFS+ signature `'H+'` + version 5)"*. `HX` is not `H+`; it is
`kHFSXSigWord` (0x4858) — a different constant. The doc folded the two families together and, being
dated 2026-10-08 (before 913 landed 2026-10-09), still speaks of *"our target is iOS 6"*
(`:13`, `:109`). Since 913 the target **is** this HFSX rootfs. The row in its reusability table
(`rootfs.hfs` = "Layout, not image") predates the pivot and now reads wrong: the volume is our target,
not a model.

## 2. Measured: our own checker accepts the real rootfs's *shape* — the only objection is SIZE

`tools/check_hfs_root_blob.py` reads the fields `hfs_MountHFSPlusVolume` (`hfs_vfsutils.c:333-352`)
indexes by, and its doc already encodes both families: *"requires `kHFSPlusSigWord` (0x482B) with
version 4, **or** `kHFSXSigWord` (0x4858) with version 5."* Run on the real rootfs:

```
check_hfs_root_blob: REFUSING: …/v2/ios7/rootfs.hfs: 939524096 bytes, not the 524288 the image is
built as. Disk 0's block count is derived from this array's length, so a short one is a volume whose
extents run past the medium
```

Its **only** objection is `ROOT_HFS_BYTES = 524288` — a size constant, not a signature, blocksize, or
catalog objection. The real volume's catalog first extent (block 9220) and its extents file are
perfectly indexable. So the driver's HFSX branch is **implemented and structurally ready**; it has
simply never run in this port.

## 3. The two gaps, named

**(a) The signature — cheap to close, and cheapest to close host-side.** `mkfs.hfsplus -s` builds an
HFSX volume (verified this session, device-free, no root: a 512 KiB image begins `4858 0005`). So a
**small HFSX fixture** is buildable at the *proven* size class, same mount mechanism (linked array),
real iOS signature — it exercises `hfs_vfsutils.c:339-345` and the `kHFSHasFolderCount` / case-sensitive
paths for the first time, without needing (b) solved and without a press. This is the de-risking rung
the current arms lack: every "the mount works" result so far is on a filesystem family the target does
not use.

**(b) The serving mechanism — the real size wall.** The proven root-volume serving is a **linked
array**: `src/entry/blob/xnu_arm_entry_root_hfs.S` does `.incbin` of the 524288-byte image, and
`st_medium_disk_size(0)`/`st_medium_disk_base(0)` return the array's length and base
(`stage90_root_media.c:527-528, 574-575`). **Disk 0's block count *is* the array length** — so to serve
the 896 MiB iOS rootfs, disk 0 must be a real block device (the eMMC card strategy, `ST_MEDIA_DRIVER`),
not an embedded array. The array cannot be 896 MiB: the payload region holds the entry image, and
`build_hfs_root_image.sh` chose 512 KiB precisely *"small enough to sit in the payload's own region."*
This is why 903's note said the mounted volume was a RAM blob (`md0`) — the read path is real, the
medium is not the device's storage. **Serving a real iOS-sized rootfs requires the root volume to live
on the card, served through the 903 card read path** — a distinct, larger port than the signature fix.

## 4. Why this is the honest next frontier

The goal's userspace clause needs the real rootfs to mount. The two things standing in the way are now
both measured, not guessed:

1. the **signature** — target is HFSX v5, the port has only ever mounted HFS+ v4 (fixable cheaply, §3a);
2. the **size/medium** — 896 MiB cannot be a linked payload array; disk 0 must serve the card (§3b).

Neither is a host-side *device* action; both are buildable and testable host-side (mkfs + the checker +
the entry build), and both belong on the road to "no byte so far has been Apple's, and the next byte
that is must land on a mount path that has never seen the target's filesystem family."

**Next rungs (both host-side, both parkable):**
- **965a:** a small HFSX fixture (`mkfs.hfsplus -s`, 512 KiB, same `launchd` blob) as a selectable root
  arm — first execution of the HFSX branch at the proven size. Refused-or-parked, no device write.
- **965b:** move disk 0 to the **card** for the root volume (the 896 MiB path), so the root medium is
  the device's own storage. This is the larger port and needs the 903 card read path as its base.

**PRESS IS THE OPERATOR'S** — neither rung above presses. Both are host-side/reversible and write no
device.

*Provenance: real volume headers read host-side from `/mnt/data/ios7-payload/v{1,2}/ios7/rootfs.hfs`
(device-free); `tools/check_hfs_root_blob.py` run on both the real rootfs and the fixture;
`mkfs.hfsplus -s` HFSX probe in `/tmp`; `bsd/hfs/hfs_format.h:53-58`, `hfs_vfsutils.c:333-352`,
`src/entry/blob/xnu_arm_entry_root_hfs.S`, `src/platform/stage90_root_media.c:527-578` read.
Device unmodified; adb showed Android running, `33e80afe` absent — idle, no action taken. Corrects
`docs/reference/hd2-ios7-lab-analysis.md:24,109`. Follows [[mi4-913-ios7-rebase-decision]] and
[[mi4-903-xnu-mounts-the-emmc-goal-met]].*