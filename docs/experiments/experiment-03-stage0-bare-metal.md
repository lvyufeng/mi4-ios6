# Experiment 03 — Stage0 Bare-Metal ARMv7 Payload

Date: 2026-06-04

Goal: prove that the Xiaomi Mi 4 (`cancro`) bootloader can execute our own non-Linux ARMv7 code through non-persistent `fastboot boot`, and that the payload can leave a recoverable breadcrumb without opening the phone.

Result: **successful**.

## Why this experiment matters

Earlier experiments only proved that Android boot image headers and Linux command lines could be changed safely. This experiment crosses a more important boundary: the stock bootloader loaded a tiny raw ARMv7 program as the boot image `kernel` and jumped into it.

That is the first real hardware execution milestone for an eventual XNU/Darwin bring-up. It still does not run stock iOS 6, but it proves that the device can execute our own non-Linux entry code and that USB-only persistent logging is usable for early bring-up.

## Payload

Source files:

```text
stage0/stage0.S
stage0/linker.ld
stage0/build.sh
```

The payload is deliberately tiny and self-contained:

- assembled for ARMv7-A / Krait-compatible execution,
- linked at physical/entry address `0x00008000`, matching the observed Android boot image `kernel_addr`,
- uses no stack, libc, MMU setup, or external symbols,
- writes a fresh Android `persistent_ram_buffer` / `ram_console` record,
- then attempts MSM8974 warm reset by lowering PS_HOLD.

Important constants in `stage0/stage0.S`:

```text
STAGE0_BASE        = 0x00008000
RAM_CONSOLE_BASE   = 0xde500000
RAM_CONSOLE_SIG    = 0x43474244  # 'DBGC'
MSM8974_PSHOLD     = 0xfc4ab000
RESTART_REASON     = 0x0fa0065c
```

Marker written into persistent RAM:

```text
MI4IOS6_STAGE0 v1 entered; wrote Android ram_console; attempting MSM8974 PS_HOLD reset
```

## QCDT requirement discovered

A first non-persistent attempt used the small `stage0.img` produced by Ubuntu's `mkbootimg`. The bootloader rejected it before execution:

```text
FAILED (remote: 'dtb not found')
```

This showed that cancro's bootloader requires a Qualcomm device tree table appended through the legacy Android boot image v0 `dt_size` field, even when the `kernel` payload is not Linux.

A follow-up attempt with Ubuntu `mkbootimg --dtb` did not fix the image: the resulting file still parsed as a tiny 4 KiB image with `dt_size=0`. For this legacy bootloader, it is not enough to have a modern `--dtb` option; the old v0 header field must be populated exactly.

To make this reproducible, this repository now includes:

```text
tools/mkbootimg_v0_qcdt.py
```

That tool writes the legacy header layout used by the backed-up cancro `boot.img`, including `dt_size`, and appends the extracted QCDT blob.

## Successful image

Build command:

```bash
./stage0/build.sh
```

The build now produces both:

```text
out/stage0/stage0.img       # no QCDT; useful as a negative-control artifact
out/stage0/stage0-qcdt.img  # includes backed-up boot QCDT; accepted by bootloader
```

Successful image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=244 (0xf4)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage0 mi4ios6=stage0
```

Hashes from the successful run:

```text
856f02b9f14ea7d0e80954149916626ff8225211a09a5d8a79017aa30bf55657  out/stage0/stage0.bin
c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe  xiaomi4-cancro-backup-20260604-112053/boot-unpacked/dt.img
25cc05b2b7288be8ec4488eaf7dab30b1a7020ab69a8899f017cec3f0bad799e  out/stage0/stage0-qcdt.img
```

The `out/` artifacts and backup images are intentionally git-ignored.

## Non-persistent boot test

Command used:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage0/stage0-qcdt.img
```

Fastboot accepted and booted the image:

```text
Sending 'boot.img' (2466 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

After about 25 seconds, the phone returned to normal Android without a manual power-button reset:

```text
4a2fe00b               device usb:3-10 product:cancro model:MI_4LTE device:cancro transport_id:18
```

This strongly suggested that the payload's PS_HOLD reset path executed.

## Persistent log verification

After Android came back over USB:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage0-last_kmsg.txt
grep -a -n 'MI4IOS6_STAGE0' /tmp/cancro-stage0-last_kmsg.txt
```

Result:

```text
1:MI4IOS6_STAGE0 v1 entered; wrote Android ram_console; attempting MSM8974 PS_HOLD reset
```

The full `/proc/last_kmsg` content was only 107 bytes:

```text
MI4IOS6_STAGE0 v1 entered; wrote Android ram_console; attempting MSM8974 PS_HOLD reset

No errors detected
```

## Conclusions

This experiment proves:

1. The cancro bootloader will execute our own non-Linux ARMv7 payload through `fastboot boot`.
2. QCDT is mandatory for accepted temporary boot images on this bootloader.
3. `0xde500000` is the correct Android `ram_console` physical base for this boot environment.
4. The Android `persistent_ram_buffer` format was written correctly: signature `DBGC`, `start=0`, `size=strlen(marker)`, and marker bytes in `data[]`.
5. MSM8974 PS_HOLD at `0xfc4ab000` successfully reset the device back into the normal boot path.
6. USB-only bring-up debugging is viable: a pre-MMU bare-metal payload can leave a breadcrumb that is recoverable from `/proc/last_kmsg` after reboot.

## Next work packages

With Stage0 complete, the next step toward an XNU/Darwin bring-up is to evolve from a one-shot marker payload into a boot wrapper:

1. Reserve and preserve the same top-of-DRAM persistent log region.
2. Implement a minimal reusable log writer so later failures leave breadcrumbs.
3. Parse or embed enough hardware description to synthesize XNU `boot_args` and an Apple-style device tree.
4. Load or link a public-source XNU ARMv7 kernel image and jump into its early entry path.
5. Replace Apple platform assumptions in `pexpert` with MSM8974/GIC/timer/UART/logging support.

This is still far from running iOS userspace, but it is the first confirmed execution point for the custom boot chain needed to get there.
