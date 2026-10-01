# 861 — THE PAYLOAD OWNS THE ROOT DEVICE: 530 §9's MOUNT ROUTE, BUILT AND LINK-PROVEN

**A build and a link, and nothing else. NO DEVICE WAS TOUCHED, NO ARM IS ARMED, NO PRESS IS OWED.**
This is the first step of a *new* track — making **XNU itself** mount storage — and it is deliberately
the smallest one that is still evidence-bearing: it gives the mount path a device to mount.

## The route, and why it needs no eMMC driver

530 §2 proved the number `bsd_init` hands to `vfs_mountroot` comes from `mdevlookup(xchar)`
(`iokit/bsddev/IOKitBSDInit.cpp:467`, inlined into `bsd_init`), and that `__wrap_mdevlookup` is
**already linked** (`0x8047db1c`) — so *the root device's number has a supplier the payload owns*, with
no edit to Apple's `bsd_init` and, as 530 §2 puts it, **"No I/O Kit object, no `IOMedia`"**. 530 §9
named the next step: *"root the existing mockfs root at a payload-owned **fake** `dev_t` whose `bdevsw`
answers `DKIOCGETBLOCKSIZE`/`DKIOCSETBLOCKSIZE`/`DKIOCGETBLOCKCOUNT` from a buffer ... with no eMMC
driver at all"*, which exercises `vfs_mountroot` → `bdevvp` → `vfs_init_io_attributes` (all
`VNOP_IOCTL` on the device vnode, `bsd/vfs/vfs_subr.c:3134-3352`).

This experiment builds that `dev_t`. It is **the medium, not the controller**: its `strategy` is
`eno_strat`, it moves no byte of the eMMC, opens no controller window and touches no device register.

## What was added

- **`src/platform/stage90_root_media.c`** — a `bdevsw` in `memdev.c`'s shape (`mdevbdevsw`,
  `memdev.c:133`): `open`/`close` return 0, `strategy` is `eno_strat`, `psize` returns the block size,
  `d_type` is `D_DISK` (**2**, `bsd/sys/conf.h:92`), and `st_media_ioctl` answers the ioctl set
  `vfs_init_io_attributes` reads — the **twelve it hard-requires** (a non-zero return there is a
  failed mount) plus `DKIOCGETTHROTTLEMASK`/`DKIOCISVIRTUAL`/`DKIOCISSOLIDSTATE` and
  `DKIOCGETBLOCKCOUNT`/`DKIOCISWRITABLE`, with `ENOTTY` for anything else.
- **`entry_root_media_register(int disk)`** — `bdevsw_add(-1, &sw)` (mirroring `memdev.c:591`) and a
  `dev_t` (`makedev(major, disk)`), publishing `xnu_live_rootmedia_major` and `xnu_live_rootmedia_dev`.
  The geometry is **keyed by `disk`**, so selecting a different partition selects a *different device* —
  the shape the storage ladder's rung-57 selection ("by extent, not by name") needs.
- **`__wrap_mdevlookup`** (`src/entry/entry_trace.c`) gained `#if STAGE90_XNU_MOUNT`: off, it is the
  pass-through it has been since 457; on, it answers `entry_root_media_register(devid)` instead of
  `__real_mdevlookup` (there is no RAM disk to look up).
- **The arm switch `STAGE90_XNU_MOUNT`** (default 0) is plumbed as a full arm-key in
  `src/entry/build_entry.sh`: var, `ENTRY_ARM_KEYS`, the value-dispatch case, the `entry_trace.c`
  compile line, and **the record writer** — the last one is not optional, and the first link of this
  step was refused by the build's own two-way check (`does not carry key(s) ENTRY_ARM_KEYS requires:
  STAGE90_XNU_MOUNT`) until it was added.
- **The object is linked**: `stage90_root_media.c` joins `PLATFORM_BSD_SOURCES` in
  `tools/build_xnu_arm_kernel.sh` (the **bsd** component's defines and roots, so `struct bdevsw`,
  `makedev`, `D_DISK` and the `DKIOC*` commands are the kernel's own headers, not copies) and
  `STAGE90_ROOT_MEDIA_OBJ` joins `LINK_OBJS` in `build_entry.sh`, with a `require` and a
  `platform_obj_fresh` age check beside the four C++ platform objects.

## Why a C file in the BSD list, not a C++ class

`memdev.c` is this tree's only block device and it is **C**; the BSD layer reaches a `bdevsw` through
`bdevsw_add`, a plain C symbol. Compiling this as a C++ `IOService` with a hand-copied `struct bdevsw`
would have been the "one value, two definitions" defect — a layout wrong by one field puts `d_ioctl`
where the kernel reads `d_strategy`, and it would not fail loudly. In the BSD list the layout *is*
`conf.h`'s.

## `devfs_make_node` is deliberately NOT called

`memdevadd` also makes a `/dev` node (`memdev.c:607`), but `devfs_make_node` lives in
`bsd_miscfs_devfs_devfs_tree.o`, which this image does **not** link — a call here would become one of
the auto-generated stand-ins and `xnu_live_rootmedia_node` would report a non-NULL pointer to a stub
that made nothing (the project's `mi4-a-claim-in-a-comment-is-not-a-check`/stand-in class). The mount
path does not need it: `bdevvp` builds its vnode directly from the `dev_t` (`bd.vdev = bdev`), so the
number is the whole interface.

## The measurements (all host-side, none from a device)

- **The module compiles** under the tree's own flags: `./tools/build_xnu_arm_kernel.sh --platform-only`
  exits 0 with `stage90_root_media.o` produced. Its only undefined symbols are `bdevsw_add`, `enodev`,
  `enodev_strat`, `entry_live_write` — **all four defined as text in objects already in `LINK_OBJS`**
  (`bsd_kern_bsd_stubs.o`, `bsd_kern_subr_xxx.o` ×2, `xnu_arm_entry_stubs.o`).
- **The wrapper's callee flips with the switch**: `entry_trace.c` compiled `-DSTAGE90_XNU_MOUNT=0`
  calls `__real_mdevlookup`; `=1` calls `entry_root_media_register` (read out of the object's
  disassembly). The no-`-D` default also compiles (default 0, matching the build's `$MOUNT`).
- **The full entry image links**: `./build_entry.sh` with the rung-57 arm's switches **plus**
  `STAGE90_XNU_MOUNT=0` exits 0, writes `xnu_arm_entry.bin`, and **every one of its checks passes**
  (the only refusal of the first run — the missing record key — was repaired, and the second run is
  clean). This is the first proof that `entry_root_media_register` and the new `bdevsw` **close in the
  entry image**, not just in isolation.

## The arm's own status

- **Default is off.** `STAGE90_XNU_MOUNT=0` adds the module to the link (its one exported symbol is
  unreferenced — dead code the linker may drop) but changes **no** existing behaviour: `__wrap_mdevlookup`
  is the pass-through it has been. The `=1` arm (which makes `vfs_mountroot` mount the payload's device)
  is a **different arm**, to be built and parked as its own step.
- **The live `out/` is unchanged**: this build was run with the rung-57 arm's switches and then the
  live `out/stage90/` was **restored from `frozen/armed-storage-71b54d73`** (the park is self-contained
  and carries its own `xnu_arm_entry.bin` and config), so `out/stage90/xnu_arm_entry.bin` is again
  `71b54d73…` and the rung-57 arm is byte-for-byte as it was. **The live record carries no
  `STAGE90_XNU_MOUNT` key** — 861 has **no arm on disk**; it is source, an object and a link proof.

## What this does and does not do

- **Does**: proves 530 §9's route is buildable and links — a payload-owned block device exists, and
  `__wrap_mdevlookup` can hand it to `vfs_mountroot` on one switch.
- **Does not**: mount anything, on a device or otherwise. **No run has been made**: whether the mount
  path *accepts* this device (whether `vfs_mountroot` gets past `bdevvp` and `vfs_init_io_attributes`
  to a filesystem probe) is exactly the next step's reading, and it needs an arm the operator presses.
  **The XNU storage goal is not met**; this is its first brick.

## Next

1. Build the `STAGE90_XNU_MOUNT=1` arm and park it (a device action, so the operator's press).
2. Couple the arm to the storage ladder's selection: `st_gpt_data_sector`/`st_gpt_best_extent` are
   `static` in `entry_storage.c`, so the ladder needs an accessor (or to pass the disk) for the root to
   be a *different* disk than partition 1's — the "select by extent" shape this file's keyed geometry
   is already built for.

## Safety

No file outside `src/`, `tools/` and `docs/` was written; `make check` exits 0; no device was touched,
nothing was flashed, and no arm is armed or owed. The rung-57 arm remains **parked and unpressed** and
the live `out/` is byte-identical to it.