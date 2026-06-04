# Experiment 02 — USB-Only Persistent Kernel Log Loop

Date: 2026-06-04

Goal: verify that kernel/debug output can be recovered over USB after a temporary boot/reboot cycle, without opening the phone or attaching a physical UART.

Result: partially successful for cmdline marker, successful for printk/kmsg marker.

## Background

The Xiaomi Mi 4 (`cancro`) exposes Android persistent RAM logging through:

```text
/proc/last_kmsg
```

This is a 2 MiB previous-boot kernel log region backed by Android `ram_console` / persistent RAM. It is not the newer upstream pstore interface.

Relevant kernel source findings:

- `external/android_kernel_xiaomi_cancro/arch/arm/configs/cancro_user_defconfig` enables `CONFIG_ANDROID_RAM_CONSOLE=y`.
- `external/android_kernel_xiaomi_cancro/arch/arm/mach-msm/board-8974.c` reserves a `ram_console` descriptor.
- `msm_8974_reserve()` reserves `SZ_1M * 2`, i.e. 2 MiB, at the top of DRAM.

## Test A — cmdline marker through temporary boot

A boot image was patched to add a unique command-line marker and verbose logging flags:

```text
console=ttyHSL0,115200,n8 loglevel=8 ignore_loglevel initcall_debug mi4ios6exp=cmdline-log-loop-20260604 ...
```

Only the boot image cmdline header field was modified.

Patched image:

```text
/tmp/cancro-boot-logloop.img
sha256=12fce5924e93770b16c09d42ec2176ca66b37d8b487197e56573f68ec4cf0998
```

Non-persistent boot:

```bash
sudo adb reboot bootloader
sudo fastboot boot /tmp/cancro-boot-logloop.img
```

The running Android kernel confirmed the marker in `/proc/cmdline`:

```text
console=ttyHSL0,115200,n8
loglevel=8
ignore_loglevel
initcall_debug
mi4ios6exp=cmdline-log-loop-20260604
```

After rebooting and pulling `/proc/last_kmsg`, the marker itself was not found. Analysis showed that `last_kmsg` retained approximately boot time `9.96s` through shutdown at `38.6s`; the earliest boot lines had been overwritten by the 2 MiB ring buffer, especially because `loglevel=8` and `initcall_debug` produced heavy output.

Conclusion for Test A:

- The cmdline marker definitely took effect.
- `/proc/last_kmsg` is a ring and may lose very early boot output if log volume is too high.
- For custom bring-up, emit concise breadcrumbs periodically or near failure, not just at the very beginning.

## Test B — explicit `/dev/kmsg` marker survives reboot

A unique marker was written directly into the live kernel log:

```bash
MARK="MI4IOS6_KMSG_MARKER_20260604_124726"
sudo adb shell "echo '$MARK before-reboot-from-android' > /dev/kmsg"
sudo adb reboot
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-kmsg-marker-last_kmsg.txt
grep "$MARK" /tmp/cancro-kmsg-marker-last_kmsg.txt
```

Result:

```text
<4>[   77.250903] MI4IOS6_KMSG_MARKER_20260604_124726 before-reboot-from-android
```

Conclusion for Test B:

- A printk/kmsg record written before reboot survives into `/proc/last_kmsg` and is recoverable over USB after the next normal Android boot.
- This validates the no-teardown debug loop for any custom kernel or shim that can write into the same persistent log mechanism or emit logs late enough before a reboot.

## Practical bring-up loop

1. Build or patch a custom boot image.
2. `sudo fastboot boot custom.img`.
3. If it reaches Android, read `/proc/cmdline` and `/dev/kmsg` directly.
4. If it crashes/reboots/hangs but leaves persistent log data, boot back to Android or recovery.
5. Pull logs:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > log.txt
```

## Lessons for XNU/Darwin bring-up

- Physical UART is not mandatory for every iteration.
- A future XNU-like kernel should reserve the same top-of-DRAM 2 MiB region before VM allocation and implement a simple persistent ring or Android-ram-console-compatible writer.
- Keep early output concise, because 2 MiB can be overwritten quickly.
- For the earliest pre-MMU failures, physical UART may still be necessary, but many later failures can be debugged purely over USB.
