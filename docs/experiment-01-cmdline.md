# Experiment 01 — Custom Kernel Cmdline via Non-Persistent Boot

Date: 2026-06-04

Goal: prove we can control and observe a custom kernel command line on the Xiaomi Mi 4 (`cancro`) without flashing any partition.

Result: success.

## Approach

The stock system `boot.img` uses a Qualcomm appended device tree (QCDT, `dt_size=2521088`). `abootimg` does not understand QCDT and would drop it on repack, producing an unbootable image. The recovery image is safe to repack with `abootimg` only because its `dt_size=0`.

To change just the kernel command line of the system boot image, a surgical header patch was used instead of unpack/repack:

- Tool: `tools/patch_bootimg_cmdline.py`
- It overwrites only the 512-byte `cmdline` field (and the 1024-byte `extra_cmdline` field if needed).
- Kernel, ramdisk, second stage, and appended QCDT are preserved byte-for-byte.
- The boot image `id` checksum does not cover the command line, so it stays valid.

## Patch

Source image (read-only golden copy):

```text
xiaomi4-cancro-backup-20260604-112053/boot.img
sha256=b2119252d046aa2e8e682674949bc5c60a9536358ad83a2c34dfb2de060b4874
```

Command:

```bash
./tools/patch_bootimg_cmdline.py \
  xiaomi4-cancro-backup-20260604-112053/boot.img \
  /tmp/cancro-boot-serial.img \
  --replace 'console=none' 'console=ttyHSL0,115200,n8'
```

Patched image:

```text
/tmp/cancro-boot-serial.img
sha256=367462ac9b819fd0be63fccbb24f41a1fa14d372789f3241c5be39fbe2485c18
```

## Byte-level verification

- Image size unchanged: `33554432` bytes.
- Differing bytes are confined to the cmdline field. First/last differing offsets (1-indexed) were `73` and `253`, both within bytes `65..576`.
- Diffs outside the cmdline field: `0`.
- Changes to the `id` field (bytes `577..608`): `0`.
- Parser confirms `dt_size=2521088` preserved and all load addresses unchanged.

## Non-persistent boot test

```bash
sudo adb reboot bootloader
sudo fastboot boot /tmp/cancro-boot-serial.img
# Sending 'boot.img' (32768 KB)  OKAY
# Booting                        OKAY
```

The device booted to normal Android (state `device`).

## Active kernel command line (proof)

`cat /proc/cmdline` after the temporary boot:

```text
console=ttyHSL0,115200,n8 vmalloc=340M androidboot.hardware=qcom msm_rtb.filter=0x3b7 ehci-hcd.park=3 androidboot.bootdevice=msm_sdcc.1 androidboot.selinux=permissive buildvariant=userdebug androidboot.emmc=true androidboot.serialno=... androidboot.hwversion=47 syspart=system bl_version=1.0 androidboot.baseband=msm mdss_mdp.panel=1:dsi:0:qcom,mdss_dsi_sharp_fhd_cmd_95
```

Key points:

- The injected `console=ttyHSL0,115200,n8` is present and replaced the stock `console=none`.
- The bootloader still appends its own `androidboot.*` arguments.
- `/dev/ttyHSL0` exists on the running system.

## Conclusions

- We can fully control the kernel command line of a custom boot image and confirm it took effect, with zero partition writes.
- The MSM8974 serial console device is `ttyHSL0` at 115200 8n1.
- This establishes the iterate loop for kernel bring-up work: patch/build a boot image, `fastboot boot` it, observe, and only flash later if ever needed.

## Next

- Capture actual UART output on `ttyHSL0` (needs the physical BLSP1 UART pads and a 1.8 V adapter); the command line is now configured for it.
- Begin assembling cancro kernel source / LineageOS device tree references for deeper bring-up.
