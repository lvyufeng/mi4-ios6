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

## Fastboot read-only check

A read-only fastboot check was performed on 2026-06-04. The phone was rebooted into bootloader and then rebooted back to Android without flashing or erasing anything.

Important host note: non-root `fastboot` waited for a device, while `sudo fastboot` worked. Use `sudo fastboot` unless host USB permissions are fixed for the fastboot-mode vendor/product combination.

Observed output:

```text
sudo fastboot devices -l
4a2fe00b               fastboot usb:3-10

sudo fastboot getvar product
product: MSM8974

sudo fastboot getvar max-download-size
max-download-size: 0x30000000
```

These queried variables returned empty values on this bootloader:

```text
secure:
unlocked:
partition-size:boot:
partition-size:recovery:
all:
```

Conclusion:

- Fastboot mode is reachable.
- `fastboot reboot` returns the phone to Android successfully.
- This bootloader exposes very little through `getvar`.
- Actual `fastboot boot <image>` support is still untested.

## Non-persistent `fastboot boot` test (success)

On 2026-06-04 a non-persistent temporary boot test was performed with the locally repacked recovery image. No partition was flashed or erased.

Image used:

```text
/tmp/cancro-recovery-repacked.img
sha256=cf112d805630b0998f685fb26a31f646170fae331368e0e387aa70ea7a89fb7f
```

Command and result:

```text
sudo fastboot boot /tmp/cancro-recovery-repacked.img
Sending 'boot.img' (16384 KB)   OKAY
Booting                         OKAY
Finished. Total time: 0.571s
```

After booting, ADB reported recovery mode:

```text
4a2fe00b   recovery   product:omni_cancro   model:MI_4LTE   device:cancro
adb get-state = recovery
```

Read-only identity collected from the temporarily booted recovery:

```text
ro.twrp.version=3.7.0_9-0
ro.build.display.id=omni_cancro-eng 7.1.2 NJH47F 20 test-keys
ro.product.device=cancro
uname=Linux localhost 3.4.113-perf-gad3eeff2596 #1 SMP PREEMPT Wed Dec 26 21:28:56 CST 2018 armv7l GNU/Linux
```

`sudo adb reboot` then returned the phone to normal Android (`product:cancro`, state `device`).

Key conclusions:

- The cancro bootloader accepts non-persistent `fastboot boot` of a locally repacked Android boot image.
- The installed recovery is TWRP 3.7.0_9-0 on an OmniROM `omni_cancro` 7.1.2 base.
- This confirms a safe iterate path: build a custom boot image, test it with `fastboot boot` first, and only flash after a successful temporary boot.

## On-disk GPT geometry (measured 2026-10-01, read-only via `adb`, no press)

Experiment 859 wrote that the Mi 4's GPT geometry is "recorded **nowhere** in this archive", which is
why rung 57 selects the mountable partition **by extent** rather than by name. It is recorded now. The
read is `dd if=/dev/block/mmcblk0 … bs=512 count=34` (LBA 0–33: the protective MBR, the GPT header at
LBA 1, and the whole 28-entry array at LBA 2), pulled with `adb` — **read-only, no boot, no write, no
`fastboot`**; the same class of device read 880 §3 used.

Header: `EFI PART` at LBA 1, `MyLBA = 1`, `AlternateLBA = 30777343`, usable `34 .. 30777310`,
`PartitionEntryLBA = 2`, `NumberOfPartitionEntries = 28`, `SizeOfPartitionEntry = 128`
(= **7** array sectors, exactly `st_gpt_array_nsec`).

| idx | first_lba | last_lba | extent (sectors) | size | GPT name | type GUID |
| --: | --------: | -------: | ---------------: | ---: | --- | --- |
| 0 | 34 | 4095 | 4062 | 2.0 MB | `sbl1` | DEA0BA2C-CBDD-4805-B4F9-F428251C3E98 |
| 1 | 4096 | 6143 | 2048 | 1.0 MB | `rpm` | 098DF793-D712-413D-9D4E-89D711772228 |
| 2 | 6144 | 8191 | 2048 | 1.0 MB | `tz` | A053AA7F-40B8-4B1C-BA08-2F68AC71A4F4 |
| 3 | 8192 | 10239 | 2048 | 1.0 MB | `DDR` | 20A0C19C-286A-42FA-9CE7-F64C3226A794 |
| 4 | 10240 | 12287 | 2048 | 1.0 MB | `ssd` | 2C86E742-745E-4FDD-BFD8-B6A7AC638772 |
| 5 | 12288 | 14335 | 2048 | 1.0 MB | `dbi` | D4E0D938-B7FA-48C1-9D21-BC5ED5C4B203 |
| 6 | 14336 | 22527 | 8192 | 4.0 MB | `aboot` | 400FFDCD-22E0-47E7-9A23-F16ED9382388 |
| 7 | 22528 | 32767 | 10240 | 5.0 MB | `bk1` | 0FC63DAF-8483-4772-8E79-3D69D8477DE4 |
| 8 | 32768 | 40959 | 8192 | 4.0 MB | `misc` | 20117F86-E985-4357-B9EE-374BC1D8487D |
| 9 | 40960 | 57343 | 16384 | 8.0 MB | `logo` | 0FC63DAF-8483-4772-8E79-3D69D8477DE4 |
| 10 | 57344 | 131071 | 73728 | 36.0 MB | `bk2` | 0FC63DAF-8483-4772-8E79-3D69D8477DE4 |
| 11 | 131072 | 134143 | 3072 | 1.5 MB | `modemst1` | EBBEADAF-22C9-E33B-8F5D-0E81686A68CB |
| 12 | 134144 | 137215 | 3072 | 1.5 MB | `modemst2` | 0A288B1F-22C9-E33B-8F5D-0E81686A68CB |
| 13 | 137216 | 137217 | 2 | — | `fsc` | 57B90A16-22C9-E33B-8F5D-0E81686A68CB |
| 14 | 137218 | 262143 | 124926 | 61.0 MB | `bk3` | 0FC63DAF-8483-4772-8E79-3D69D8477DE4 |
| 15 | 262144 | 265215 | 3072 | 1.5 MB | `fsg` | 638FF8E2-22C9-E33B-8F5D-0E81686A68CB |
| 16 | 265216 | 327679 | 62464 | 30.5 MB | `bk4` | 0FC63DAF-8483-4772-8E79-3D69D8477DE4 |
| 17 | 327680 | 393215 | 65536 | 32.0 MB | `bk5` | 0FC63DAF-8483-4772-8E79-3D69D8477DE4 |
| 18 | 393216 | 458751 | 65536 | 32.0 MB | `boot` | 20117F86-E985-4357-B9EE-374BC1D8487D |
| 19 | 458752 | 491519 | 32768 | 16.0 MB | `recovery` | 20117F86-E985-4357-B9EE-374BC1D8487D |
| 20 | 491520 | 524287 | 32768 | 16.0 MB | `persist` | EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 |
| 21 | 524288 | 786431 | 262144 | 128.0 MB | `modem` | EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 |
| 22 | 786432 | 3407871 | 2621440 | 1280.0 MB | `system` | EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 |
| 23 | 3407872 | 4194303 | 786432 | 384.0 MB | `cache` | EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 |
| 24 | 4194304 | 30777310 | 26583007 | 12980.0 MB | `userdata` | EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 |

**The GPT index is not the kernel partition number.** `mmcblk0pN` is 1-based in the order the entries
appear, so GPT index `k` = `mmcblk0p(k+1)`: `userdata` is GPT index 24 = `p25`, `sbl1` index 0 = `p1`.
`st_gpt_data_sector` (rung 57's selection) is a **sector address, not a partition number**.

**Rung 57's selector, run over this table**, keeps the entry with the largest `EndingLBA - StartingLBA`
→ **entry 24 = `userdata`**, `StartingLBA = 4194304`, extent 26,583,007 sectors (`entry_storage.c:8012`).
That is the claim 863 §"What the rung asks" made from prose, now a **device measurement**. Two of rung
56's pressed cells are device-verified by the same table: entry 0 = `sbl1` with `first_lba = 34`
(`0x22`) and `last_lba = 4095` (`0xfff`), byte-for-byte `_ge_first_lba = 0x22` / `_ge_last_lba = 0xfff`
(and `_ge_index = 0`); and `st_gpt_array_nsec = 7` (the header's own `28 * 128 / 512`).

**The selected partition carries ext4, not HFS.** Reading the selection's superblock sector —
`st_gpt_data_sector + ST_FS_SB_SECTOR_OFF` = LBA `4194304 + 2` = `4194306` — gives `s_magic = 0xEF53`
at partition byte 1024, `s_log_block_size = 2` (4096-byte blocks), `s_inodes_count = 835584`,
`s_blocks_count = 3322875`, `s_rev_level = 1`, `s_feature_incompat = 0x46`. **This is exactly the
sector rung 57's arm would land and the sector no press has yet read** (rung 56 read partition 1's,
which held ARM code). It confirms 880's constraint directly: the selected medium's filesystem is
**ext4**, and this repository ports **HFS+** — the two do not match. No partition in this table carries
an HFS type GUID.

## Safety notes

- Avoid touching `sbl1`, `aboot`, `rpm`, `tz`, `modem`, `modemst1`, `modemst2`, `persist` unless there is a very specific recovery plan.
- Prefer non-persistent `fastboot boot <image>` experiments before any `fastboot flash`.
- Re-check that fastboot mode is reachable before any destructive experiment.
- Keep the verified backup directory out of source commits.

