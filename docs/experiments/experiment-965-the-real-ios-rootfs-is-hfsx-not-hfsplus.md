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

**Verified through the whole loop, this session:** `mkfs.hfsplus -s -v ios7probe /tmp/hfsx_fx.img`
(512 KiB) → header `4858 0005` → `tools/check_hfs_root_blob.py /tmp/hfsx_fx.img` **rc=0** ("HFS+ 0x4858
v5, 128 blocks"). So 965a has **no unknowns**: an HFSX volume at the proven size passes the project's
own mount gate. (The builder still needs `sudo` for the `losetup`+populate step — already how
`build_hfs_root_image.sh` works.)

**(b) The serving mechanism — the real size wall.** The proven root-volume serving is a **linked
array**: `src/entry/blob/xnu_arm_entry_root_hfs.S` does `.incbin` of the 524288-byte image, and
`st_medium_disk_size(0)`/`st_medium_disk_base(0)` return the array's length and base
(`stage90_root_media.c:527-528, 574-575`). **Disk 0's block count *is* the array length** — so to serve
the 896 MiB iOS rootfs, disk 0 must be a real block device (the eMMC card strategy, `ST_MEDIA_DRIVER`),
not an embedded array. The array cannot be 896 MiB: the payload region holds the entry image, and
`build_hfs_root_image.sh` chose 512 KiB precisely *"small enough to sit in the payload's own region."*
**Serving a real iOS-sized rootfs requires the root volume to live on the card** — a distinct, larger
port than the signature fix.

**Correction (965a, verified): on master the mount is the tree's own `hfs_mountroot`, not 4570's
mockfs.** The first draft of this section repeated 903's "the volume was a RAM blob (`md0`)" note. That
is a **4570 inheritance**: 4570's root provider is `mockfs` (which memory-backs a blob as `md0`), but
**D13 ships no mockfs** — its `bsd/vfs/vfs_conf.c:120` carries the native row
`{ &hfs_vfsops, "hfs", 17, …, hfs_mountroot, … }`, and `build_entry.sh`'s D13 branch *requires*
`bsd_hfs_hfs_vfsops.o` in the pool for exactly this reason (`36998-37003`). So on master, with
`STAGE90_XNU_HFS_ROOT_MEDIA=1`, `vfs_mountroot` genuinely calls `hfs_mountroot`, which reaches the
strategy-served volume. The medium (array vs card) is still the wall in §3b, but it is a **size**
wall, not a "the mount never happens" wall.

## 4. Why this is the honest next frontier

The goal's userspace clause needs the real rootfs to mount. The two things standing in the way are now
both measured, not guessed:

1. the **signature** — target is HFSX v5, the port has only ever mounted HFS+ v4 (fixable cheaply, §3a);
2. the **size/medium** — 896 MiB cannot be a linked payload array; disk 0 must serve the card (§3b).

Neither is a host-side *device* action; both are buildable and testable host-side (mkfs + the checker +
the entry build), and both belong on the road to "no byte so far has been Apple's, and the next byte
that is must land on a mount path that has never seen the target's filesystem family."

**Next rungs (both host-side, both parkable):**
- **965a (DONE, this rung):** the committed fixture is now **HFSX** — see §5.
- **965b:** move disk 0 to the **card** for the root volume (the 896 MiB path), so the root medium is
  the device's own storage. This is the larger port and needs the 903 card read path as its base.

## 5. 965a — the fixture is HFSX (built and verified host-side)

Per the user's directive 「跑iOS7需要什么格式就用什么格式」, the format is whatever iOS 7 uses, so the
fixture was flipped to HFSX. It is a **one-file generator change + one regenerated blob** — nothing else,
because the strategy serves raw bytes (no format assumption anywhere in the port; the arm split, the
size constant `524288`, and every switch are unchanged):

- **`tools/build_hfs_root_image.sh`** gained `-X|--hfsx` (mkfs's `-s` case-sensitive flag; `-s` was
  already SIZE). The post-format header readback now accepts **either** family and checks signature and
  version **as a pair** (`0x482B`+4 or `0x4858`+5), the way the kernel does (`hfs_vfsutils.c:340`).
- **`src/entry/blob/xnu_arm_entry_root_hfs.img`** regenerated as a 512 KiB HFSX volume (`mkfs.hfsplus -s`),
  `/sbin/launchd` = the same 8192-byte Mach-O blob (round-trip verified).
- **`tools/check_hfs_root_blob.py`** already accepted HFSX; its pass line now prints the family.

**Verified, host-side, no press:**
- generator → `HFSX 0x4858 v5, blockSize 4096, totalBlocks 128 (524288 B)`; header `4858 0005`.
- `check_hfs_root_blob.py` rc=0 on the fixture (`HFSX 0x4858 v5`).
- the **full entry build** on the recorded switch set (the c54fc40d arm, `HFS_ROOT_MEDIA=1`) → rc=0, and
  the built `out/stage90/xnu_arm_entry.bin` **embeds the HFSX volume** (header `0x4858 v5` at image
  offset `0x570400`; **zero** HFS+ v4 occurrences left). The new record sha256 is
  `b9c224c0…`/6327380 B.
- `make check` rc=0.

So on master, when this arm is pressed, `hfs_mountroot` will parse an **HFSX** volume for the first time
— the driver branch `hfs_vfsutils.c:339-345` that no prior arm ever reached. That is the de-risking
965a buys: if the HFSX parse path is broken, it fails here, at the proven 512 KiB size, with one changed
variable — not tangled with 965b's medium change.

**PRESS IS THE OPERATOR'S** — 965a is built and host-verified, not pressed. Both rungs write no device.

## 5a. 965a's premise was wrong on the arm it parked — see 965b

The fixture flip above changed the **committed blob** `src/entry/blob/xnu_arm_entry_root_hfs.img`. But
the arm 965a parked, `armed-d13-b9c224c0`, carries `STAGE90_XNU_ROOT_FROM_CARD=1`: on that arm the root
is the **CARD unit** (`__wrap_mdevlookup` answers `entry_root_media_register_card()`,
`entry_trace.c:5131`) and disk 0 — the blob — is never registered (a build clause even forbids it,
`build_entry.sh:29016-29020`). The filesystem the press would mount is a **separate** image,
`out/stage90/xnu_card_hfs.img`, written to `userdata`'s head by `scripts/press_965b.sh`, which 965a left
**HFS+ v4**. So 965a changed an inert byte array on this arm. **965b** is the correcting rung: it builds
the card medium as HFSX and adds a preflight refusal so a stale HFS+ card cannot ship silently. See
`docs/experiments/experiment-965b-the-pressed-root-medium-is-the-card-not-the-blob.md`. This also
resolves §4's "next rungs": **965b is not a move from array to card (the card is already the pressed
root) — it is making the card the right family.**

*Provenance: real volume headers read host-side from `/mnt/data/ios7-payload/v{1,2}/ios7/rootfs.hfs`
(device-free); `tools/check_hfs_root_blob.py` run on both the real rootfs and the fixture;
`mkfs.hfsplus -s` HFSX probe in `/tmp`; `bsd/hfs/hfs_format.h:53-58`, `hfs_vfsutils.c:333-352`,
`src/entry/blob/xnu_arm_entry_root_hfs.S`, `src/platform/stage90_root_media.c:527-578` read.
Device unmodified; adb showed Android running, `33e80afe` absent — idle, no action taken. Corrects
`docs/reference/hd2-ios7-lab-analysis.md:24,109`. Follows [[mi4-913-ios7-rebase-decision]] and
[[mi4-903-xnu-mounts-the-emmc-goal-met]].*