# No-Teardown Debugging on Xiaomi Mi 4 (`cancro`)

Date: 2026-06-04

Question: can we do early-boot / kernel bring-up debugging without opening the phone and soldering a UART?

Answer: yes. Several USB-only channels are available and were verified on this device.

## Why this matters

Physical UART (`ttyHSL0`, configured in experiment 01) is the ultimate early-boot console, but it requires disassembly, finding the BLSP1 UART pads, and a 1.8 V adapter. For most bring-up iterations we do not need it, because this phone retains kernel logs across reboots in a RAM-backed store.

## Channel 1 — `/proc/last_kmsg` (most important)

The previous boot's kernel log is preserved.

- Path: `/proc/last_kmsg`
- Size on this device: `2097160` bytes (about 2 MiB ring)
- Backed by Qualcomm ramoops / persistent RAM, not by flash

Pull the full previous-boot log to the host:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-last_kmsg.txt
```

Verified content includes the tail of the prior boot, ending with the reboot reason, for example:

```text
init: Reboot ending, jumping to kernel
Restarting system with command 'bootloader'.
Going down for restart now
Calling SCM to disable SPMI PMIC arbiter
```

Implication for bring-up: if a custom kernel writes to this persistent RAM region (ramoops / pstore console) before it dies or reboots, the next normal Android boot can read those logs over USB. No soldering required.

## Channel 2 — live `/dev/kmsg`

The stock `dmesg` binary segfaults in this restricted shell, but the kernel log device is directly readable.

Read the live kernel log:

```bash
sudo adb exec-out 'cat /dev/kmsg' > /tmp/cancro-kmsg.txt   # streaming; Ctrl-C / kill to stop
sudo adb shell 'dd if=/dev/kmsg bs=4096 count=1 2>/dev/null'  # quick sample
```

`/dev/kmsg` lines use the printk record format `level,seq,timestamp;message`.

## Channel 3 — `ramoops` module

The kernel exposes `ramoops`:

```text
/sys/module/ramoops/parameters/
```

This is the mechanism behind `/proc/last_kmsg`. It is the hook to target when instrumenting a custom kernel to leave a readable breadcrumb across a crash/reboot.

## Channel 4 — recovery-mode ADB

The installed TWRP recovery provides a full root ADB shell (confirmed in earlier testing). Recovery is a safe environment to read logs, inspect partitions, and run experiments because it does not mount or modify the normal system.

## Channel 5 — fastboot non-persistent boot

Already proven: `sudo fastboot boot <image>` runs a custom boot image without flashing. Combined with `/proc/last_kmsg`, this is the core iterate loop.

## No-teardown bring-up loop

1. Patch or build a custom boot image locally (see `tools/`).
2. Ensure the kernel command line keeps a persistent-RAM console and, ideally, `console=ttyHSL0,...` for later hardware UART.
3. `sudo fastboot boot custom.img` — non-persistent.
4. Let it run or crash.
5. Reboot to known-good Android or recovery.
6. `sudo adb exec-out 'cat /proc/last_kmsg' > log.txt` and analyze on the host.

Only if persistent-RAM logging itself fails to come up do we need the physical UART.

## Caveats

- `/proc/last_kmsg` reflects the *previous* boot only; each experiment overwrites it.
- The persistent RAM region survives reboot but not a full power-off in all cases; read it promptly.
- A custom (non-Android) kernel must initialize the same ramoops/pstore RAM region and format for these logs to be readable; otherwise fall back to UART.
- `dmesg` userspace binary is unreliable here; prefer `/dev/kmsg` and `/proc/last_kmsg` directly.
