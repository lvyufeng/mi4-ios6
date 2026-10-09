# 966 — the HD2's writable iOS root is a block-layer COW, and its code is already in our tree (2026-10-09)

The session that produced 965c/965d audited what stands between the real 896 MiB iOS 7.1.2 HFSX volume and
a *running* iOS userspace. It found that the D13 mount path has no host-side gate left (965d §4b) — but
that a **read-only** root cannot serve iOS userspace, which writes `/private/var`. This rung records where
that missing half already lives: the **HD2 iOS7 lab**, whose running log we have, whose XNU tree we already
build, and whose writable-root mechanism — a **block-layer copy-on-write overlay** — is a plain-C file
already sitting in `external/xnu-hd2-darwin13/xnu`.

This is a **transferable mechanism + a concrete next rung**, not a claim that mi4 boots iOS.

## 1. The HD2 lab, re-read after the 913 pivot

`/mnt/data/ios7-payload/v2/ios7/` is the HD2 lab v2 package (`hd2-kernel-070`,
`Status=SEALED_FINITE_APP_LAUNCH_GRACE_HARDWARE_PENDING`). `docs/reference/hd2-ios7-lab-analysis.md` (written
2026-10-08) judged its reusability, but **its framing predates 913**: it calls the HD2 kernel "a *different
and later* XNU than the mi4 target (`xnu-2050.18.24` = iOS 6)". After 913 the target is **Darwin-13 / iOS 7**,
and `external/xnu-hd2-darwin13/xnu` **is the HD2 lab's own tree** (the doc's own `KernelSha256 3e03ee13…`
line already points there). So the HD2 is no longer "a later XNU than ours" — it is **the same XNU tree,
patched to boot on a different SoC**. That reframes every HD2 artifact as a *same-generation reference*, not
a future one.

Two things the 2026-10-08 note did not extract, both now measured from `BOOTLOG.BIN` (1 MiB ring log,
`LEOLOG01`/`LEOREC01` record framing) and `BOOT-MAP.json`:

**The HD2 mounted *our* rootfs.** The log records the root file as
`...ROOTFS.HFS bytes=939524096 extents=2 HFS=4858 version=5 BASE_SD_RO=1 COW_RW=1 VOLATILE=1` —
HFSX `0x4858` v5, 939,524,096 B, **the same volume** as our `v2/ios7/rootfs.hfs` (sha256 `c9b9080d…`). The
HD2 reads it as a **file** off its SD card through `IOS7LeoHFSFile`.

**The HD2 reached iOS userspace.** The log records `Getting boot device…`, `BSD root: disk1, major 14,
minor 2`, `root_device -> / (hfs)`, `Leo boot: init execve begin path=/sbin/launchd`,
`com.apple.launchd 1 … *** launchd[1] has started up. ***`, and then `IOS7LAB HEALTH` samples naming
`backboardd pid=11` and `SpringBoard pid=12` with `status=2` (running). The package's own `Limits` in
`BOOT-MAP.json` also record `068 launchd fault recorded; causal newdelta not identified` and the tail of the
log is that health-monitor loop — i.e. **the HD2 got an iOS-7 userspace up (launchd, backboardd, SpringBoard)
and then hit a launchd fault the lab itself never root-caused.** Honest reading: userspace came up on
hardware; long-run app launch did not.

## 2. The mechanism: a read-only base + a RAM COW at the block device

The HD2's root volume is **read-only** (`BASE_SD_RO=1`), yet `hfs_mountfs` mounts it **read-write**
(`Leo root: … COW=1 VOLATILE=1`, `Leo boot: root mount readonly=0`) and userspace writes succeed
(`Leo COW: cap=1405 pages used=2 dirty=2 writes=2 sync=0 error=00000000`). The whole trick is one layer:
`IOS7LeoHFSFile` (a `IOBlockStorageDevice`) implements `doAsyncReadWrite` and, on the **out** direction,
calls `leo_cow_prepare_write` + `leo_cow_write_prepared_sector`; on **in**, `leo_cow_read_sector`. The
filesystem never learns the base is read-only — it sees a writable block device.

`leo_cow` (82 lines of plain C) is a **page-granular RAM shadow**:

- `leo_cow_read_sector(sector)` — if the 4096-byte page holding `sector` is resident, return its shadow
  copy; else read the base through the caller's `read512` callback.
- `leo_cow_prepare_write(first,count)` — for each page touched but not yet resident, **read the whole
  page from the read-only base into a free arena slot first** (so the unwritten sectors keep their original
  bytes), then mark it resident. Capacity is proved *before* any base read, and nothing is visible until
  every original page has been read.
- `leo_cow_write_prepared_sector` — write 512 B into the resident shadow; mark dirty.

The base is **never written**. `leo_cow_init` takes the base's `reads` as a `leo_cow_read512_fn` callback,
so **the module is SoC-independent** — it knows nothing of QSD8250 or the SD card. That is exactly what
makes it portable.

## 3. It is already in our tree, and only needs a wiring

```
external/xnu-hd2-darwin13/xnu/iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/
    leo_cow.h / leo_cow.c              the RAM COW itself (plain C, SoC-free)
    IOS7LeoHFSFile.cpp                 the block device: doAsyncReadWrite -> COW, base = the SD file
    IOS7LeoSDCC2.cpp / IOS7LeoHFSDriver the HD2 SD read path and the IOMedia it publishes
    IOS7LeoDisplay.cpp / IOS7LeoHIDTouch.cpp  HD2 display/touch (SoC-specific, not portable)
```

So the HD2's *writable-root* half is **not to be re-derived** — it is a file in the tree we already build.
What the mi4 has that the HD2 lacks is a **different base**: the HD2 base is a file on an SD card; the mi4
base is the **eMMC card unit**, read (and, for 905, written) through the ladder
(`entry_storage_driver_read`/`_write`). The mi4's base-serving already exists — `st_media_strategy`'s card
branch serves the selected partition block by block (965c). What is missing is exactly the COW layer the HD2
puts **between** that serving and `hfs_mountfs`.

## 4. The concrete next rung: a COW overlay for the mi4 card block device

The port is a **block-layer** change to our own device, in the same place the HD2 put it:

| where | what | why |
|---|---|---|
| `src/platform/stage90_root_media.c` | for the card unit, add a RAM COW shadow: `B_READ` → `cow_read_sector`; `B_WRITE` → `cow_prepare_write` + per-sector shadow write | this is the whole HD2 trick, at our strategy |
| the COW itself | **reuse `leo_cow.c` verbatim** (it is SoC-free and already in the tree) or a mi4-side twin with the same four functions | one definition; do not re-derive a page shadow |
| the arena | a fixed page budget in the entry payload (`.bss` array; a named constant with a build refusal, `[[mi4-entry-group-page-move-pins-two-copies]]`) | the HD2 computes its budget from XNU's free-page count (`leo_cow_budget_pages`); a payload-side arena has no allocator to ask, so its size is a stated constant |
| `DKIOCISWRITABLE` | answer **1** for the card unit on this arm | the mount reads this byte to choose rw (`hfs_vfsops.c:1555`); with the COW present, 1 is honest |
| build/check | the pair is asserted together (as 905's `xnu_entry_905` does): a COW write branch with `DKIOCISWRITABLE=0`, or vice versa, is refused | `[[mi4-a-claim-in-a-comment-is-not-a-check]]` |

**Why this is the right model and not a hack.** It never writes the base volume (`/sdcard` head stays
byte-exact), so it is **non-destructive and reversible** — a power cycle reverts every write, which is also
why the HD2 marks the volume `VOLATILE=1`. For a first boot of iOS userspace it is strictly better than
writable-root hacks (an `MNT_RDONLY` clear + real HFS writes), because it cannot corrupt the base and needs
no journal.

**What this rung does not do.** It does not by itself make iOS *boot* — it removes the read-only wall that
would stop userspace the moment it writes `/private/var`. The mount itself (965c) and the ladder's read path
are the rungs below it; the userspace boot is the rung above. It is host-side-buildable and press-gated.

## 5. What would falsify this

- `leo_cow.c` absent from, or not the tree we build — it is present under `external/xnu-hd2-darwin13/xnu/`
  (read this session) and that tree is our `XNU_TREE` for D13.
- The log's `rootfs.hfs` being a different volume than ours — its `HFS=4858 version=5 bytes=939524096`
  and the package's `RootImage.Sha256 c9b9080d…` both match `v2/ios7/rootfs.hfs`.
- The COW being a VFS-level or journal dependency rather than a block-layer shadow — the code path is
  `doAsyncReadWrite` → `leo_cow_*`, entirely below `hfs_mountfs`, which is why it can be ported without
  touching the filesystem.
- The log being a single (failed) boot rather than one that reached SpringBoard — the tail's
  `IOS7LAB HEALTH` samples name `SpringBoard pid=12 status=2`; the log is a ring and may hold more than one
  boot, so the *run* is the thing to reproduce, not this log alone.

## 6. Provenance

- `BOOTLOG.BIN` and `BOOT-MAP.json` read host-side from `/mnt/data/ios7-payload/v2/ios7/` (the log is a
  1 MiB ring, `LEOLOG01`/`LEOREC01` framing; `BOOT-MAP.json` `RootImage`/`Payload`/`Limits`).
- `leo_cow.h`/`leo_cow.c` (82 lines), `IOS7LeoHFSFile.cpp` (393), `IOS7LeoSDCC2.cpp` (275) read in
  `external/xnu-hd2-darwin13/xnu/iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/`.
- The mi4 base-serving the COW would wrap: `src/platform/stage90_root_media.c` (`st_media_strategy`'s card
  branch, 965c) and `src/entry/entry_storage.c` (`entry_storage_driver_read`/`_write`).
- Corrects the framing of `docs/reference/hd2-ios7-lab-analysis.md` (post-913: same XNU tree, not a later
  one) and adds the COW model it omitted. Follows [[mi4-965-real-rootfs-is-hfsx]],
  [[mi4-913-ios7-rebase-decision]]. Device unmodified.