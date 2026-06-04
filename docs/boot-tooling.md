# Boot Image Tooling Plan

This document describes the local tooling path for Xiaomi Mi 4 LTE (`cancro`) Android boot image experiments.

The goal is to prove that the host can inspect and round-trip existing `boot.img` / `recovery.img` images before attempting any non-persistent boot test. This document does not authorize flashing.

## Golden copies

Treat these local backup files as read-only golden copies:

```text
xiaomi4-cancro-backup-20260604-112053/boot.img
xiaomi4-cancro-backup-20260604-112053/recovery.img
```

Never overwrite the backup directory. Work from scratch directories such as `/tmp/cancro-boot-*` or ignored output directories.

## Tooling options

Recommended Ubuntu tools:

```bash
sudo apt update
sudo apt install -y abootimg mkbootimg android-tools-adb android-tools-fastboot
```

Tool notes:

- `abootimg` is useful for inspecting and recreating legacy Android boot images using an extracted `bootimg.cfg`.
- `mkbootimg` can build Android boot images explicitly, but tool versions vary. Header version, base, and offsets must be checked carefully.
- `magiskboot` can be useful, but it is not a normal Ubuntu apt package. Prefer apt-packaged tools first for reproducible local work.
- This repository also includes `tools/parse_android_bootimg.py`, a small dependency-free parser for local inspection.

Check available commands:

```bash
command -v abootimg
command -v mkbootimg
command -v unpackbootimg
command -v unpack_bootimg
command -v fastboot
```

## Local image facts

Observed from the verified local backups.

### `boot.img`

- SHA-256: `b2119252d046aa2e8e682674949bc5c60a9536358ad83a2c34dfb2de060b4874`
- Magic: `ANDROID!`
- Page size: `2048`
- Kernel size: `6540944`
- Kernel address: `0x8000`
- Ramdisk size: `904576`
- Ramdisk address: `0x2000000`
- Second-stage size: `0`
- Second-stage address: `0xf00000`
- Tags address: `0x1e00000`
- Device tree size: `2521088`
- Command line:

```text
console=none vmalloc=340M androidboot.hardware=qcom msm_rtb.filter=0x3b7 ehci-hcd.park=3 androidboot.bootdevice=msm_sdcc.1 androidboot.selinux=permissive buildvariant=userdebug
```

### `recovery.img`

- SHA-256: `fbb01c5562a27faa9c20c6f818a9dd518768348d745858f00d55fa4a275ab769`
- Magic: `ANDROID!`
- Page size: `2048`
- Kernel size: `9066373`
- Kernel address: `0x8000`
- Ramdisk size: `7171052`
- Ramdisk address: `0x1000000`
- Second-stage size: `0`
- Second-stage address: `0xf00000`
- Tags address: `0x100`
- Device tree size: `0`
- Command line:

```text
console=ttyHSL0,115200,n8 androidboot.hardware=qcom user_debug=31 msm_rtb.filter=0x37 ehci-hcd.park=3 androidboot.bootdevice=msm_sdcc.1 androidboot.selinux=permissive buildvariant=eng
```

## Local inspection commands

Set paths:

```bash
BACKUP_DIR=/mnt/data/mi4-ios6/xiaomi4-cancro-backup-20260604-112053
BOOT="$BACKUP_DIR/boot.img"
RECOVERY="$BACKUP_DIR/recovery.img"
```

Verify hashes and file type:

```bash
sha256sum "$BOOT" "$RECOVERY"
file "$BOOT" "$RECOVERY"
```

Use this repository's parser:

```bash
./tools/parse_android_bootimg.py "$BOOT"
./tools/parse_android_bootimg.py "$RECOVERY"
```

Use `abootimg`:

```bash
abootimg -i "$BOOT"
abootimg -i "$RECOVERY"
```

## No-op round-trip with `abootimg`

This is the preferred first round-trip test because `bootimg.cfg` preserves legacy header fields.

Boot image:

```bash
rm -rf /tmp/cancro-boot-abootimg
mkdir -p /tmp/cancro-boot-abootimg
cd /tmp/cancro-boot-abootimg
abootimg -x "$BOOT"
abootimg --create /tmp/cancro-boot-repacked.img \
  -f bootimg.cfg \
  -k zImage \
  -r initrd.img
abootimg -i /tmp/cancro-boot-repacked.img
```

Recovery image:

```bash
rm -rf /tmp/cancro-recovery-abootimg
mkdir -p /tmp/cancro-recovery-abootimg
cd /tmp/cancro-recovery-abootimg
abootimg -x "$RECOVERY"
abootimg --create /tmp/cancro-recovery-repacked.img \
  -f bootimg.cfg \
  -k zImage \
  -r initrd.img
abootimg -i /tmp/cancro-recovery-repacked.img
```

If a repacked image is byte-different from the original, that is not automatically bad. Compare parsed header fields. Critical values should remain stable: page size, kernel and ramdisk addresses, tags address, command line, and second-stage fields.

## Local round-trip result on this host

A no-op round-trip was performed with `abootimg` on 2026-06-04, writing only to `/tmp`.

Generated files:

```text
/tmp/cancro-boot-repacked.img
/tmp/cancro-recovery-repacked.img
```

Results:

- `boot.img` and `/tmp/cancro-boot-repacked.img` have different SHA-256 hashes.
- `recovery.img` and `/tmp/cancro-recovery-repacked.img` have different SHA-256 hashes.
- `abootimg -i` shows the critical boot fields stayed the same for both images:
  - image size
  - page size
  - kernel size
  - ramdisk size
  - kernel address
  - ramdisk address
  - tags address
  - command line
- The `id` words in the repacked images were zeroed by `abootimg`, while the original images had non-zero IDs.

Conclusion: `abootimg` is suitable for local header-preserving experiments, but any repacked image should be treated as a new artifact and tested non-persistently first.

## Explicit `mkbootimg` examples

These commands express observed addresses as base `0x00000000` plus offsets. Use only after checking the installed `mkbootimg --help` output.

Boot image:

```bash
mkbootimg \
  --kernel /tmp/cancro-boot-abootimg/zImage \
  --ramdisk /tmp/cancro-boot-abootimg/initrd.img \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline 'console=none vmalloc=340M androidboot.hardware=qcom msm_rtb.filter=0x3b7 ehci-hcd.park=3 androidboot.bootdevice=msm_sdcc.1 androidboot.selinux=permissive buildvariant=userdebug' \
  --header_version 0 \
  --output /tmp/cancro-boot-mkbootimg.img
```

Recovery image:

```bash
mkbootimg \
  --kernel /tmp/cancro-recovery-abootimg/zImage \
  --ramdisk /tmp/cancro-recovery-abootimg/initrd.img \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x01000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x00000100 \
  --pagesize 2048 \
  --cmdline 'console=ttyHSL0,115200,n8 androidboot.hardware=qcom user_debug=31 msm_rtb.filter=0x37 ehci-hcd.park=3 androidboot.bootdevice=msm_sdcc.1 androidboot.selinux=permissive buildvariant=eng' \
  --header_version 0 \
  --output /tmp/cancro-recovery-mkbootimg.img
```

If `--header_version 0` is rejected, inspect `mkbootimg --help`; some older tools default to legacy output and may not expose the option.

## Fastboot boot test path

A temporary boot test is safer than flashing, but still depends on bootloader support.

Read-only fastboot checks:

```bash
adb reboot bootloader
fastboot devices -l
fastboot getvar product
fastboot getvar secure
fastboot getvar unlocked
fastboot getvar all
```

Non-persistent boot test, only after confirming fastboot mode and choosing a known-good image:

```bash
fastboot boot /tmp/cancro-recovery-repacked.img
```

Some older Xiaomi/Qualcomm bootloaders may reject `fastboot boot`, hang, or require a specific accepted image format. Failure of `fastboot boot` does not prove an image is flash-safe.

## Sources

- [Ubuntu 22.04 `abootimg` package](https://packages.ubuntu.com/jammy/abootimg)
- [Ubuntu 22.04 `android-tools-mkbootimg` package](https://packages.ubuntu.com/jammy/android-tools-mkbootimg)
- [Ubuntu `abootimg` manpage](https://manpages.ubuntu.com/manpages/jammy/man1/abootimg.1.html)
- [Android boot image header documentation](https://source.android.com/docs/core/architecture/bootloader/boot-image-header)
