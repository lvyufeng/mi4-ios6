# Local Xiaomi Mi 4 LTE (`cancro`) Findings

Date: 2026-06-04

This document records facts observed locally from the connected owner-controlled Xiaomi Mi 4 LTE device. Commands in this phase were read-only with respect to the phone, except host-side installation/configuration of Android platform tools and udev rules.

## Host setup

Installed host tools:

- `adb`
- `fastboot`

Configured host udev rules for Android/Xiaomi USB access:

- Google/Android vendor ID: `18d1`
- Xiaomi vendor ID: `2717`

Root `adb` was used because the first non-root ADB attempt reported USB permission issues.

## USB / ADB identity

ADB reported:

```text
serial: 4a2fe00b
product: cancro
model: MI_4LTE
device: cancro
```

Android properties reported:

```text
ro.product.device=cancro
ro.product.model=MI 4LTE
ro.product.brand=Xiaomi
ro.build.version.release=10
ro.build.version.sdk=29
ro.build.fingerprint=Xiaomi/cancro/cancro:6.0.1/MMB29M/V8.1.6.0.MXDMIDI:user/release-keys
ro.hardware=qcom
ro.boot.hardware=qcom
ro.bootloader=unknown
ro.product.cpu.abi=armeabi-v7a
ro.product.cpu.abilist=armeabi-v7a,armeabi
```

Kernel:

```text
Linux localhost 3.4.113-perf-g9cd90d33e3a #1 SMP PREEMPT Wed Oct 19 16:15:37 CST 2022 armv7l
```

ADB shell identity:

```text
uid=0(root) gid=0(root) groups=0(root),1004(input),1007(log),1011(adb),1015(sdcard_rw),1028(sdcard_r),3001(net_bt_admin),3002(net_bt),3003(inet),3006(net_bw_stats),3009(readproc),3011(uhid) context=u:r:su:s0
```

Security/debug properties:

```text
ro.secure=0
ro.adb.secure=0
ro.debuggable=1
```

## Named partitions

Observed named partition symlinks under `/dev/block/platform/msm_sdcc.1/by-name`:

| Name | Block device |
| --- | --- |
| `sbl1` | `/dev/block/mmcblk0p1` |
| `rpm` | `/dev/block/mmcblk0p2` |
| `tz` | `/dev/block/mmcblk0p3` |
| `DDR` | `/dev/block/mmcblk0p4` |
| `ssd` | `/dev/block/mmcblk0p5` |
| `dbi` | `/dev/block/mmcblk0p6` |
| `aboot` | `/dev/block/mmcblk0p7` |
| `bk1` | `/dev/block/mmcblk0p8` |
| `misc` | `/dev/block/mmcblk0p9` |
| `logo` | `/dev/block/mmcblk0p10` |
| `bk2` | `/dev/block/mmcblk0p11` |
| `modemst1` | `/dev/block/mmcblk0p12` |
| `modemst2` | `/dev/block/mmcblk0p13` |
| `fsc` | `/dev/block/mmcblk0p14` |
| `bk3` | `/dev/block/mmcblk0p15` |
| `fsg` | `/dev/block/mmcblk0p16` |
| `bk4` | `/dev/block/mmcblk0p17` |
| `bk5` | `/dev/block/mmcblk0p18` |
| `boot` | `/dev/block/mmcblk0p19` |
| `recovery` | `/dev/block/mmcblk0p20` |
| `persist` | `/dev/block/mmcblk0p21` |
| `modem` | `/dev/block/mmcblk0p22` |
| `system` | `/dev/block/mmcblk0p23` |
| `cache` | `/dev/block/mmcblk0p24` |
| `userdata` | `/dev/block/mmcblk0p25` |

## Local backup

Boot-critical and radio/persist partitions were backed up locally before any boot experiment.

Backup directory:

```text
xiaomi4-cancro-backup-20260604-112053/
```

The backup contains:

```text
sbl1.img
rpm.img
tz.img
DDR.img
ssd.img
dbi.img
aboot.img
bk1.img
misc.img
logo.img
bk2.img
modemst1.img
modemst2.img
fsc.img
bk3.img
fsg.img
bk4.img
bk5.img
boot.img
recovery.img
persist.img
modem.img
SHA256SUMS.txt
```

`sha256sum -c SHA256SUMS.txt` verified all files successfully.

The backup directory is intentionally ignored by git.

## Boot image layout

### `boot.img`

The backed-up boot partition is a standard Android boot image.

```text
magic=ANDROID!
page_size=2048
kernel_size=6540944 (0x63ce90)
kernel_addr=32768 (0x8000)
ramdisk_size=904576 (0xdcd80)
ramdisk_addr=33554432 (0x2000000)
second_size=0
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=console=none vmalloc=340M androidboot.hardware=qcom msm_rtb.filter=0x3b7 ehci-hcd.park=3 androidboot.bootdevice=msm_sdcc.1 androidboot.selinux=permissive buildvariant=userdebug
kernel_off=0x800
ramdisk_off=0x63d800
dt_off=0x71a800
```

Extracted local artifacts:

```text
boot-unpacked/kernel
boot-unpacked/ramdisk.gz
boot-unpacked/dt.img
```

### `recovery.img`

The backed-up recovery partition is a standard Android boot image.

```text
magic=ANDROID!
page_size=2048
kernel_size=9066373 (0x8a5785)
kernel_addr=32768 (0x8000)
ramdisk_size=7171052 (0x6d6bec)
ramdisk_addr=16777216 (0x1000000)
second_size=0
second_addr=15728640 (0xf00000)
tags_addr=256 (0x100)
dt_size=0
cmdline=console=ttyHSL0,115200,n8 androidboot.hardware=qcom user_debug=31 msm_rtb.filter=0x37 ehci-hcd.park=3 androidboot.bootdevice=msm_sdcc.1 androidboot.selinux=permissive buildvariant=eng
kernel_off=0x800
ramdisk_off=0x8a6000
```

Extracted local artifacts:

```text
recovery-unpacked/kernel
recovery-unpacked/ramdisk.gz
```

## Safety notes

- Avoid touching `sbl1`, `aboot`, `rpm`, `tz`, `modem`, `modemst1`, `modemst2`, `persist` unless there is a very specific recovery plan.
- Prefer non-persistent `fastboot boot <image>` experiments before any `fastboot flash`.
- Re-check that fastboot mode is reachable before any destructive experiment.
- Keep the verified backup directory out of source commits.
