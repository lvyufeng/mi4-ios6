# 913 — The pivot to an iOS 7 (Darwin 13) base: decision and first steps (2026-10-08)

The operator's request 「我觉得我们也应该改成iOS7」 and the arrival of the HD2 iOS7 lab package
(2026-10-08) forced a version question the project had been deferring. This doc records the
measurement that answered it, the decision taken, and the acquisition that starts the pivot.

## 1. The measurement that decides it: our "launchd" is not iOS

Until now the project has been called `mi4-ios6` and its docs target iOS 6.1.3 / `xnu-2050`.
Three facts, all verified this session, break that framing:

1. **The live kernel base is `xnu-4570.1.46`**, `config/MasterVersion = 17.0.0` (**Darwin 17** =
   macOS 10.12-era), not iOS 6. `src/entry/build_entry.sh:1726` hardcodes
   `XNU=$REPO_ROOT/external/xnu-4570.1.46` with **no env switch**; every `tools/*.sh` defaults
   `XNU_TREE` to it. The name "ios6" is aspirational.
2. **No Darwin-13 (iOS 7) tree exists in the repo**, and **Apple never published iOS-6/7 ARM
   kernel source**. Every iOS-6-era tree present — `xnu-2050.18.24` (Darwin 12.2.0),
   `xnu-upstream` (12.3.0), `apple-xnu-rel-2050` (12.5.0), `distribution-iOS-*` — has **no
   `osfmk/arm/`**. Only `xnu-4570` has the ARM boot path (`arm_init.c`, `arm_vm_init.c`,
   `pexpert/arm/`), which is *why* `docs/reference/ios-613-oss-baseline.md:129` chose it: 2050's
   public source has no ARM boot code.
3. **What our 903/906 "iOS" boot actually execs is a 0x2000-byte synthetic ARM Mach-O fixture**
   (`src/entry/entry_ramdisk.s`), mapped as `/sbin/launchd` by `tools/build_hfs_root_image.sh`
   (line 13). The mount/exec path is real; **no byte of Apple iOS has ever run on the device.**

So the project proves the *path* (mount own eMMC → exec a Mach-O → syscalls work) but has never
had iOS. That is the gap the version choice is about.

## 2. What the HD2 lab actually supplies (measured, read-only)

- `rootfs.hfs` (939,524,096 B) = a **real, complete, decrypted iOS 7.1.2 root filesystem**
  (`SystemVersion.plist` → `ProductVersion 7.1.2`, `ProductBuildVersion 11D257`). Real
  `/sbin/launchd`, `/usr/lib/dyld`, 58 frameworks, apps, and a 315,952,666 B
  `dyld_shared_cache_armv7` holding **696 images**. `attributes = 0x100` (`kHFSVolumeUnmountedBit`),
  unjournaled — the same shape as the 903 volume.
- It is **armv7 and decrypted** (no `LC_ENCRYPTION_INFO`). Krait executes it natively. The blocker
  is **AMFI / code-signing** (`LC_CODE_SIGNATURE` present on every binary) — a *kernel policy*, not
  hardware and not FairPlay.
- `initrd.gz` is NOT gzip: it is the XNU `UNXF`/PRELINK container of the **HD2 kernel** itself
  (Darwin 13.0.0, root `DEBUG_ARM_QSD8250_LEO`); its `boot_args` are `physBase 0x12b00000`,
  `memSize 0x02d00000` (a 45 MB single-bank 2010-era phone). **This kernel cannot boot msm8974.**
- The HD2 kernel tree (`Garysss123/xnu-hd2`) is **Darwin 13.0.0** (`MasterVersion = 13.0.0`),
  i.e. **exactly the iOS 7 kernel generation**. `osfmk/arm/` is a full ARMv7 port; the generic
  parts inherit **winocm's darwin-on-arm** (APSL 2.0, reusable).

## 3. Why Darwin 17 cannot run iOS 7.1.2 — the version must go DOWN

A kernel's userland ABI is version-tied: Mach trap table, MIG subsystem IDs, BSD syscall numbers
and struct layouts drift every major Darwin. A Darwin-17 kernel will not present the ABI that
iOS 7.1.2's dyld/launchd/frameworks were built against. **To run that rootfs, the kernel must be
Darwin 13** (the `xnu-2422.x` line). Being on 4570 is therefore **not** "ahead" — it is ahead only
for a synthetic fixture, and **behind** for any real iOS. The user's instinct was right.

## 4. The decision (operator, 2026-10-08)

**Full pivot: adopt a Darwin-13 ARM kernel base**, write an msm8974 board port, add an AMFI policy,
and target the real iOS 7.1.2 rootfs. Keep `xnu-4570` as a working fallback — do not delete it.

Concrete plan, in order:

1. **Acquire the Darwin-13 ARM tree.** Source = winocm's darwin-on-arm (via the HTC fork / the HD2
   repo). The **generic** `osfmk/arm/*`, `pexpert/arm/common/*`, `pexpert/pexpert/arm/boot.h` are
   APSL-2.0 and reusable. **Do NOT lift the HD2 board files** (`pe_qsd8250_leo.c`, `leo_*.c/.h`,
   `IOS7Leo*`) — they carry no reuse license and every address is QSD8250.
2. **Retarget our build harness** (`tools/build_xnu_arm_*.sh`, `assemble_arm_layer.sh`) at the new
   tree; measure how far it compiles. The harness, not the tree, is our asset.
3. **Write an msm8974 PE** (board port) — model its *shape* on HD2's PE + the `leo_handoff` contract,
   but with real msm8974 values (GIC, timer, memory map).
4. **Feed the measured two-bank map** (911 §22: `[0,0x60000000) ∪ [0x80000000,0xe0000000)`) into the
   handoff — the 3 GB clause, now on the correct kernel generation.
5. **Adopt the iOS 7.1.2 rootfs.hfs + dyld shared cache**, and add the **AMFI bypass** so its
   signed binaries run.
6. **Keep 4570 as a fallback arm.**

## 5. First step: acquisition

The next concrete action is §4.1 — fetch a Darwin-13 ARM tree and point the existing harness at it.
Nothing on the device changes; this is host-side, reversible, no press. The measurement it yields
(how far the Darwin-13 ARM core compiles under our toolchain) is the number that prices the pivot.

*Provenance: local tree inspection (`MasterVersion` files, `find osfmk/arm`), `rootfs.hfs` read-only
mount by the recon agent, GitHub API reads; device read-only (`adb shell`, no press, `33e80afe`
absent).*