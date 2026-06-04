# Xiaomi Mi 4 iOS 6 / Darwin Experiment

This repository tracks an experimental, owner-controlled research project around the Xiaomi Mi 4 LTE (`cancro`) and the feasibility of running iOS 6 / Darwin / XNU-like components on non-Apple ARMv7 hardware.

## Current device

Connected device observed via ADB:

- Model: Xiaomi MI 4LTE
- Device codename: `cancro`
- Hardware: Qualcomm / `qcom`
- CPU ABI: `armeabi-v7a`
- Current Android: Android 10 userdebug-style build
- Kernel: Linux 3.4.113, ARMv7
- ADB shell: root-capable (`uid=0` observed)

## Important technical reality

The goal is experimental research, not a normal ROM port.

A stock Apple iOS 6 image cannot simply be flashed to this phone. iOS depends on Apple-specific hardware, boot chain, device tree, XNU platform support, IOKit drivers, graphics stack, code-signing infrastructure, and proprietary userland components. Xiaomi Mi 4 uses a Qualcomm MSM8974-family platform and Android/Linux boot images.

A realistic research path is incremental:

1. Keep the original device recoverable.
2. Understand the existing Android boot image and partition layout.
3. Prove that custom boot/recovery images can be booted safely.
4. Study whether an open-source Darwin/XNU-derived or XNU-like minimal kernel experiment can be adapted to the Qualcomm platform.
5. Treat full iOS 6 userspace as a separate, much harder problem because SpringBoard/UIKit/CoreAnimation and related Apple frameworks are proprietary and platform-specific.

## Local backup status

Before any write/flash operation, key boot-critical partitions were backed up locally from the connected phone.

Backup directory currently present in the working tree but intentionally not committed:

```text
xiaomi4-cancro-backup-20260604-112053/
```

The backup contains images such as:

- `sbl1.img`
- `rpm.img`
- `tz.img`
- `aboot.img`
- `boot.img`
- `recovery.img`
- `persist.img`
- `modem.img`
- `SHA256SUMS.txt`

The SHA256 manifest was verified successfully after backup.

## Observed boot image layout

Backed-up `boot.img` is a standard Android boot image:

- Page size: 2048
- Kernel load address: `0x8000`
- Ramdisk load address: `0x2000000`
- Tags address: `0x1e00000`
- Device tree blob present in the boot image
- Command line includes `androidboot.hardware=qcom` and `androidboot.bootdevice=msm_sdcc.1`

Backed-up `recovery.img` is also a standard Android boot image and uses a serial-console-oriented command line (`console=ttyHSL0,115200,n8`).

## Repository contents

- `docs/local-device-findings.md` — detailed local observations, partition map, backup status, and parsed boot/recovery image fields.
- `docs/recovery-and-rollback.md` — required recovery checklist and rollback procedure before any persistent write.
- `docs/boot-tooling.md` — local boot image tooling plan and no-op round-trip results.
- `docs/cancro-platform.md` — Xiaomi Mi 4 / MSM8974 platform source pointers and bootloader notes.
- `docs/darwin-xnu-research.md` — open Darwin/XNU research notes and milestone framing.
- `docs/experiment-01-cmdline.md` — first successful experiment: custom kernel cmdline via non-persistent boot.
- `tools/parse_android_bootimg.py` — dependency-free parser/extractor for Android boot image v0/v1-style files.
- `tools/patch_bootimg_cmdline.py` — surgical editor that changes only the kernel command line, preserving kernel/ramdisk/QCDT and the boot `id`.

Example parser usage:

```bash
./tools/parse_android_bootimg.py xiaomi4-cancro-backup-20260604-112053/boot.img
```

The backup directory is ignored by git, so this command only works on a host where the local backup exists.

## Safety rules for this repo

- Do not flash or erase partitions without an explicit confirmation for that specific operation.
- Prefer `fastboot boot` or other non-persistent tests before persistent writes.
- Never include personal data or large partition backup images in normal source commits.
- Keep recovery instructions and hashes close to any experimental boot image work.
- Focus on open-source, owned-device research. Do not rely on leaked proprietary Apple code.

## Next milestones

- Add `.gitignore` rules for backup images and generated artifacts.
- Document the exact partition map and recovery procedure.
- Install or vendor boot image tooling (`unpackbootimg`, `mkbootimg`, or equivalent scripts).
- Identify Xiaomi `cancro` kernel source and device trees.
- Identify relevant open-source Darwin/XNU ARMv7 materials for study.
- Build a minimal non-destructive boot experiment before attempting any deeper kernel work.
