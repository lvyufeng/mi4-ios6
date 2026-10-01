# 885 — THE HFS+ PORT INTEGRATES INTO THE STAGE90 KERNEL (built host-side, no device)

**A host-side build: the kernel, with the HFS+ port on, compiles and LINKS.** No device, no boot, no
arm, no park, no press. The rung-58 arm `armed-storage-5936b246` stays parked and unpressed. **THE
PRESS IS THE OPERATOR'S; THE GOAL IS NOT MET.**

In one paragraph: 884 washed the storage ladder's *selection* premise off the device's own GPT and
settled that the reformat (880 §3 route 1) must come **after** the driver and the linked port, not
before. The port (868–879) had been priced (865/868/869/871: compile counts, link gap, the root row's
wiring) but had **never been built into the real STAGE90 kernel** — `STAGE90_XNU_HFS` is a switch
(default 0) that adds the 37 HFS sources to the kernel build and activates the 879 root row, and no
build had ever turned it on. This step turns it on and measures: **663 C files tried, 662 compile, 1
fail**, and the 1 fail is **`osfmk/kperf/kperfbsd.c`** — a pre-existing 4570 header conflict
(`libkern.h` vs `misc_protos.h`), recorded as `failed.txt`'s one entry since experiment 459 and
**untouched by the port**. So every HFS source compiles in place and the port links: `hfs_mountroot` is
`T` in the pool and `bsd_vfs_vfs_conf.o` has `U hfs_mountroot`/`U hfs_vfsops` — the root row is active
and the HFS sources satisfy it.

## 1. The switch, and why it had never been on

`tools/build_xnu_arm_kernel.sh`: `HFS_PORT=${STAGE90_XNU_HFS:-0}`. When 1, it

- adds `src/supply/hfs_files.txt` (37 paths: 36 `.c` under `bsd/hfs/` plus `bsd/vfs/vfs_journal.c`) to
  the manifest via `list_sources.py --extra`;
- force-includes `src/shims/hfs/hfs_port_force.h` and `hfs_cprotect_port.h` **for those 37 files only**
  (the path test, line ~1027: `$src` is `bsd/hfs/*` or `bsd/vfs/vfs_journal.c`);
- adds `-DSTAGE90_HFS_ROOT=1`, which activates `bsd/vfs/vfs_conf.c`'s HFS row — a tracked patch
  (`tools/patch_vfs_conf_hfs_row.py`, applied by `tools/stage_hfs.sh`) that places the HFS `vfstable`
  **before mockfs**, so `vfs_mountroot` tries it first.

The row is `#if STAGE90_HFS_ROOT` and the 37 sources are only in the manifest when `HFS_PORT=1`, so
**both the sources and the row switch together** — a build cannot silently have one without the other.
Earlier steps measured the parts (868/869/871 the compile/link in a `/tmp` probe or a shim tree;
879 the row's inertness); this step is the first to turn the switch on in the kernel build itself.

## 2. The measurement

```
STAGE90_XNU_HFS=1 XNU_KERNEL_CONFIG=STAGE90_XNU \
  XNU_MASTER_LOCAL=<abs>/tools/xnu_config/boot/STAGE90_XNU.local \
  ./tools/build_xnu_arm_kernel.sh
```

```
  C files tried:        663
  compile:              662
  fail:                 1
  C++ files tried:      83
  C++ compile:          83
  objects in /mnt/data/mi4-ios6/out/xnu_kernel_obj
```

**All 37 HFS objects are present and correct** (`bsd_hfs_hfs_vfsops.o`, `bsd_hfs_hfs_catalog.o`,
…, `bsd_vfs_vfs_journal.o`) — zero missing.

**The 1 fail is not HFS.** `out/xnu_kernel_obj/failed.txt` names exactly
`osfmk/kperf/kperfbsd.c`, whose diagnostic is a **pre-existing 4570 self-inconsistency**: including
`bsd/libkern/libkern.h` after `osfmk/kern/misc_protos.h` gives `conflicting types for 'ffs' / 'fls' /
'copyinstr' / 'copyin' / 'copyout'`. This is **experiment 459's own entry** — its `failed.txt` "still
names one file (`osfmk/kperf/kperfbsd.c`), unchanged" — and the port cannot reach it: the HFS
force-includes apply only to `bsd/hfs/*` and `bsd/vfs/vfs_journal.c`, and `kperfbsd.c` is `osfmk/`.

**The link is real.** In the HFS-on pool: `nm` shows `T hfs_mountroot` (the port defines it) and
`bsd_vfs_vfs_conf.o` carries `U hfs_mountroot` / `U hfs_vfsops` (the row references it) beside the
existing `U mockfs_mountroot` / `U mockfs_vfsops`. **The 879 root row and the 868–879 port are joined in
one kernel image** — the prerequisite for any HFS mount, now built rather than designed.

## 3. What this is, and what it is not

- **It is**: the HFS+ port and its static root row **compile and link into the STAGE90 kernel** with no
  new failure — the integration 880/881 left owed, done.
- **It is not a boot and not a mount.** No device was touched; the entry image and the payload are
  unchanged (the arm `5936b246` is byte-for-byte the same). Whether `hfs_mountroot` runs, whether it
  finds a volume, and whether `vfs_mountroot` falls through to mockfs correctly on failure are **device
  facts** for a press — and they need the **medium** (867) to be an HFS volume, which it is not (884:
  the selected partition is ext4).
- **It does not by itself advance the goal.** The goal is XNU mounting **storage**; a kernel that can
  mount HFS+ still needs (a) a driver that moves the bytes (867 §3, now buildable per 884) and (b) a
  medium that holds HFS+ (880 §3 route 1, the operator's reformat — which must come **after** this and
  the driver). This step is the filesystem half's last host-side prerequisite, not the mount.

## 4. The measurement's own defect, caught and repaired

The first `./tools/build_xnu_arm_kernel.sh` run failed **before compiling anything** — `device_table.py`
refused: `make_defines.sh STAGE90_XNU` "returned non-zero exit status 1." It was **not** a config defect
and not the HFS switch (`make_defines.sh` runs clean standalone, with or without `STAGE90_XNU_HFS`). The
cause: the build script `cd`s into `tools/` (line 54), and `XNU_MASTER_LOCAL=<relative path>` no longer
resolved from there, so `expand.sh` failed on the missing fragment. **The remedy is an absolute path.**
This is the same class the build's own comments warn about and worth stating as a rule: **a path passed
into a script that `cd`s is resolved against the wrong directory** — pass an absolute path, or the
failure names a file that exists.

## 5. Status

**Built host-side, host-side only.** The HFS-on kernel compiles and links; the pool is then restored to
the baseline (HFS off) so the live tree matches the parked arm `armed-storage-5936b246`. No device, no
`fastboot`, no arm spent, nothing armed. **THE PRESS IS THE OPERATOR'S. THE GOAL IS NOT MET** — the
medium is still ext4, the driver rung is not built, and no XNU `strategy` moves a byte off the card yet.