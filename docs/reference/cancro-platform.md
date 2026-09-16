# Xiaomi Mi 4 LTE (`cancro`) Platform Research

This document records platform facts and source pointers for the Xiaomi Mi 4 LTE (`cancro`) owner-controlled boot experiment.

## Platform identity

- Device codename: `cancro`
- Device family: Xiaomi Mi 3 / Mi 4 ecosystem
- Local device model: Xiaomi MI 4LTE
- Qualcomm platform: MSM8974-family
- GPU family: Adreno 330
- CPU architecture: ARMv7 / Krait

LineageOS device configuration identifies:

```make
TARGET_BOARD_PLATFORM := msm8974
TARGET_BOOTLOADER_BOARD_NAME := MSM8974
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_MKBOOTIMG_ARGS := --ramdisk_offset 0x02000000 --tags_offset 0x01E00000
```

The observed local `boot.img` matches these important parameters:

- kernel address: `0x8000`
- ramdisk address: `0x02000000`
- tags address: `0x01e00000`
- page size: `2048`

## Source repositories

### Device tree

Useful Android device tree:

- [LineageOS `android_device_xiaomi_cancro`](https://github.com/LineageOS/android_device_xiaomi_cancro)
- [LineageOS `BoardConfig.mk` raw](https://raw.githubusercontent.com/LineageOS/android_device_xiaomi_cancro/cm-14.1/BoardConfig.mk)
- [LineageOS `board-info.txt` raw](https://raw.githubusercontent.com/LineageOS/android_device_xiaomi_cancro/cm-14.1/board-info.txt)
- [LineageOS `fstab.qcom`](https://github.com/LineageOS/android_device_xiaomi_cancro/blob/cm-14.1/rootdir/root/fstab.qcom)

### Kernel source

Reliable kernel sources:

- [MiCode `Xiaomi_Kernel_OpenSource`](https://github.com/MiCode/Xiaomi_Kernel_OpenSource)
- [MiCode `cancro-kk-oss` branch](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/cancro-kk-oss)
- [MiCode `cancro-m-oss` branch](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/cancro-m-oss)
- [LineageOS `android_kernel_xiaomi_cancro`](https://github.com/LineageOS/android_kernel_xiaomi_cancro)

### Recovery source / downloads

Useful TWRP references:

- [TeamWin `android_device_xiaomi_cancro`](https://github.com/TeamWin/android_device_xiaomi_cancro)
- [TWRP Xiaomi Mi 3 / cancro page](https://twrp.me/xiaomi/xiaomimi3.html)
- [TWRP cancro downloads](https://dl.twrp.me/cancro)

TWRP’s documented install path is persistent and should not be run without explicit approval:

```bash
adb reboot bootloader
fastboot flash recovery twrp.img
fastboot reboot
```

## Partition and boot image details

From LineageOS `BoardConfig.mk` / `fstab.qcom` and local observation:

| Partition | Size / path clue |
| --- | --- |
| `boot` | `16384000` bytes in LineageOS config; local partition is `/dev/block/mmcblk0p19` |
| `recovery` | `16384000` bytes in LineageOS config; local partition is `/dev/block/mmcblk0p20` |
| `system` | `1342177280` bytes in LineageOS config; local partition is `/dev/block/mmcblk0p23` |
| `cache` | `393216000` bytes in LineageOS config; local partition is `/dev/block/mmcblk0p24` |
| `userdata` | `13291503000` bytes in LineageOS config; local partition is `/dev/block/mmcblk0p25` |
| `persist` | `16384000` bytes in LineageOS config; local partition is `/dev/block/mmcblk0p21` |

LineageOS fstab paths use `/dev/block/bootdevice/by-name/*`; local Android exposes `/dev/block/platform/msm_sdcc.1/by-name/*`.

Important fstab targets:

- `/system`: `/dev/block/bootdevice/by-name/system`
- `/cache`: `/dev/block/bootdevice/by-name/cache`
- `/data`: `/dev/block/bootdevice/by-name/userdata`
- `/boot`: `/dev/block/bootdevice/by-name/boot`
- `/recovery`: `/dev/block/bootdevice/by-name/recovery`
- `/misc`: `/dev/block/bootdevice/by-name/misc`
- encryption footer/device clue: `/dev/block/bootdevice/by-name/bk1`

## Fastboot / secondary bootloader notes

### Stock fastboot

Before any non-persistent boot test, verify actual bootloader behavior on this device:

```bash
adb reboot bootloader
fastboot devices -l
fastboot getvar product
fastboot getvar secure
fastboot getvar unlocked
fastboot getvar all
```

`fastboot getvar all` is read-only but may print identifiers. Do not paste it publicly without review.

### `lk2nd`

`lk2nd` is a secondary bootloader packaged as an Android boot image. It is loaded by the existing stock bootloader rather than replacing the primary firmware.

Potential benefits:

- Exposes Android Fastboot.
- Can boot Android boot images.
- Supports `fastboot boot boot.img` once running.
- Provides logs through commands such as `fastboot oem log && fastboot get_staged <output-file>`.

Important caution:

- The `lk2nd` documentation lists Xiaomi 4 LTE / `cancro` among MSM8974-supported devices.
- The fetched MSM8974 build rules did not visibly list a Xiaomi cancro DTB target during research.
- Resolve this source-level inconsistency against the exact release/build before flashing anything.

Relevant `lk2nd` sources:

- [lk2nd repository](https://github.com/msm8916-mainline/lk2nd)
- [lk2nd README](https://raw.githubusercontent.com/msm8916-mainline/lk2nd/main/README.md)
- [lk2nd boot documentation](https://raw.githubusercontent.com/msm8916-mainline/lk2nd/main/Documentation/boot.md)
- [lk2nd fastboot documentation](https://raw.githubusercontent.com/msm8916-mainline/lk2nd/main/Documentation/fastboot.md)
- [lk2nd devices documentation](https://raw.githubusercontent.com/msm8916-mainline/lk2nd/main/Documentation/devices.md)
- [lk2nd MSM8974 rules.mk](https://github.com/msm8916-mainline/lk2nd/blob/main/lk2nd/device/dts/msm8974/rules.mk)

## Serial console / UART clues

No reliable Xiaomi Mi 4 board-specific UART test-point mapping was confirmed during research.

Useful MSM8974 clues from `lk2nd` source:

- MSM8974 target init conditionally enables debug UART with `WITH_DEBUG_UART`.
- It initializes UART DM with `uart_dm_init(1, 0, BLSP1_UART1_BASE);`.
- MSM8974 UART base addresses from `lk2nd` `iomap.h` include:

| Symbol | Address |
| --- | --- |
| `BLSP1_UART0_BASE` | `0xF991D000` |
| `BLSP1_UART1_BASE` | `0xF991E000` |
| `BLSP1_UART2_BASE` | `0xF991F000` |
| `BLSP1_UART3_BASE` | `0xF9920000` |
| `BLSP1_UART4_BASE` | `0xF9921000` |
| `BLSP1_UART5_BASE` | `0xF9922000` |

The local/LineageOS-style boot command line uses `console=none`, so serial output likely requires changing bootargs and kernel configuration, plus identifying the correct physical UART pins.

Use a 1.8 V UART adapter unless board-level evidence proves another voltage.

Sources:

- [lk2nd MSM8974 target init.c](https://github.com/msm8916-mainline/lk2nd/blob/main/target/msm8974/init.c)
- [lk2nd MSM8974 iomap.h](https://github.com/msm8916-mainline/lk2nd/blob/main/platform/msm8974/include/platform/iomap.h)

## Recommended next steps

1. Preserve known-good recovery assets.
2. Verify fastboot mode and stock bootloader support for `fastboot boot`.
3. Perform local-only boot image unpack/repack round-trip tests.
4. If non-persistent boot is supported, test a known-good repacked image with `fastboot boot`.
5. Resolve `lk2nd` cancro support before considering any `lk2nd` flash.
6. Clone and inspect LineageOS cancro device/kernel sources.
7. Compare cancro with other MSM8974 devices such as Nexus 5 `hammerhead` and OnePlus One `bacon` for bootloader/secondary-loader strategy.
8. For UART, start from MSM8974 BLSP1 UART1/UART2 clues, but do not assume board pins without evidence.

## Risk notes

- Boot and recovery partitions are around 16 MiB nominal; oversized images can fail.
- Stock cancro command lines may suppress serial output with `console=none`.
- Public cancro UART/test-point documentation was not confirmed.
- `lk2nd` looks promising but must be verified against exact cancro build artifacts before any flash.
