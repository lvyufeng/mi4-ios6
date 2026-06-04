# Recovery and Rollback Procedure

This document defines the safety baseline for Xiaomi Mi 4 LTE (`cancro`) boot experiments in this repository.

The connected phone is an owner-controlled experimental device. Even so, bootloader and partition writes can permanently brick a Qualcomm device. Treat this document as the required checklist before any persistent write.

## Current recovery assets

A verified local backup exists at:

```text
xiaomi4-cancro-backup-20260604-112053/
```

The backup is intentionally ignored by git and is expected to stay local to the host that created it.

The backup includes:

- Boot chain / firmware-ish partitions: `sbl1`, `rpm`, `tz`, `DDR`, `ssd`, `dbi`, `aboot`
- Android boot partitions: `boot`, `recovery`
- Radio/calibration/persist partitions: `modem`, `modemst1`, `modemst2`, `fsg`, `fsc`, `persist`
- Misc/logo/reserved partitions: `misc`, `logo`, `bk1`..`bk5`
- `SHA256SUMS.txt`

Before relying on the backup, verify hashes from inside the backup directory:

```bash
sha256sum -c SHA256SUMS.txt
```

## Golden rules

1. Prefer tests that do not write to the phone.
2. Prefer `fastboot boot <image>` over `fastboot flash <partition> <image>`.
3. Do not write boot chain or radio/calibration partitions during normal experiments.
4. Do not erase partitions as a troubleshooting step.
5. Do not flash an image unless the target partition, image origin, and rollback path are explicit.
6. Confirm fastboot mode works before making a persistent boot/recovery change.
7. Keep a known-good recovery path available before testing an experimental boot image.

## Partitions to avoid writing

Avoid writing these unless there is a very specific reason and a device-specific unbrick plan:

```text
sbl1
rpm
tz
DDR
ssd
dbi
aboot
modem
modemst1
modemst2
fsg
fsc
persist
```

These can affect bootloader execution, TrustZone, radio firmware, calibration, and device identity. Mistakes here may require EDL/JTAG-style recovery or may be unrecoverable with normal fastboot/ADB.

## Lower-risk experimental targets

The least bad persistent targets are:

```text
boot
recovery
```

Even these can temporarily brick normal boot/recovery if flashed incorrectly. Prefer non-persistent tests first.

## Non-destructive preflight checks

From Android/ADB mode:

```bash
sudo adb devices -l
sudo adb shell 'id; getprop ro.product.device; getprop ro.hardware; uname -a'
sudo adb shell 'ls -l /dev/block/platform/msm_sdcc.1/by-name/boot /dev/block/platform/msm_sdcc.1/by-name/recovery'
```

From fastboot mode:

```bash
fastboot devices -l
fastboot getvar product
fastboot getvar secure
fastboot getvar unlocked
fastboot getvar all
```

`fastboot getvar all` is read-only, but it may print device serials or identifiers. Avoid pasting full output publicly without review.

## Preferred experimental order

### 1. Local-only image inspection

Parse backed-up images locally:

```bash
./tools/parse_android_bootimg.py xiaomi4-cancro-backup-20260604-112053/boot.img
./tools/parse_android_bootimg.py xiaomi4-cancro-backup-20260604-112053/recovery.img
```

### 2. Local-only unpack/repack round trip

Before changing anything, prove that the toolchain can unpack and repack an image while preserving expected header fields. Compare parser output before and after.

### 3. Fastboot reachability test

Reboot to bootloader and check visibility:

```bash
sudo adb reboot bootloader
fastboot devices -l
```

Return to Android without flashing:

```bash
fastboot reboot
```

### 4. Non-persistent boot test

Only after fastboot reachability is confirmed, test a known-good image non-persistently:

```bash
fastboot boot path/to/known-good-recovery-or-boot.img
```

If it fails, reboot/power-cycle. Do not immediately flash.

### 5. Persistent flash only with explicit approval

A persistent flash should be a separate, explicitly approved step, for example:

```bash
fastboot flash recovery path/to/known-good-recovery.img
fastboot reboot
```

Do not flash boot chain partitions as part of this project unless the entire task is specifically about bootloader recovery and the risks are accepted.

## Rollback examples

If a boot image flash breaks normal Android boot but fastboot still works, restore the backed-up boot image:

```bash
fastboot flash boot xiaomi4-cancro-backup-20260604-112053/boot.img
fastboot reboot
```

If a recovery image flash breaks recovery but fastboot still works:

```bash
fastboot flash recovery xiaomi4-cancro-backup-20260604-112053/recovery.img
fastboot reboot
```

If fastboot does not work but ADB still works from Android, do not attempt random writes. Re-check the exact current state first.

## Emergency notes

Qualcomm devices may have EDL modes, but this repository does not currently document a verified EDL recovery path for this specific phone. Until an EDL path is verified, assume boot chain partition writes can be unrecoverable in this workflow.

## Before every future write

Record:

- Current mode: Android / recovery / fastboot
- `adb devices -l` or `fastboot devices -l` output
- Target partition
- Exact image path and SHA256
- Whether the image was built locally or obtained externally
- The rollback command

Then ask for explicit confirmation for that exact write.
