# Stage0 Payload Plan

Goal: prove that the Xiaomi Mi 4 (`cancro`) bootloader can execute our own non-Linux ARMv7 code when packaged as an Android boot image.

This is the first practical step toward an XNU/Darwin-like bring-up. It is still far from running iOS userspace, but it crosses an important boundary: from modifying a Linux boot image to executing our own payload.

## Safety model

- Use only `fastboot boot`; do not flash.
- Payload must be tiny and self-contained.
- Payload must leave a visible/recoverable marker, preferably in Android `ram_console` persistent RAM.
- Payload should attempt reboot back to the normal boot path after writing the marker.
- If automatic reboot fails, the fallback is manual long-press power. This is acceptable only because the test is non-persistent.

## Bootloader contract

Cancro bootloader accepts Android boot images and has already accepted:

- original boot/recovery images
- locally repacked recovery image
- cmdline-patched system boot image

Known Android boot image parameters:

- kernel load/entry address: `0x00008000`
- page size: `2048`
- bootloader product: `MSM8974`
- fastboot non-persistent boot: confirmed

Stage0 will be packed as the `kernel` payload of an Android boot image. It should ignore Linux boot arguments for the first version.

## Persistent marker target

The stock kernel uses Android `ram_console`, not upstream pstore.

Source facts:

- `external/android_kernel_xiaomi_cancro/arch/arm/mach-msm/board-8974.c`
  - `ram_console_debug_reserve(SZ_1M * 2)` reserves 2 MiB.
  - `ram.start = memblock_end_of_DRAM() - ram_console_size`.
- `external/android_kernel_xiaomi_cancro/drivers/staging/android/persistent_ram.c`
  - buffer header:
    - `uint32_t sig`
    - `atomic_t start`
    - `atomic_t size`
    - `uint8_t data[]`
  - signature: `PERSISTENT_RAM_SIG = 0x43474244` (`DBGC`).
  - old log is read from `data[start..] + data[..start]`.

Observed current memory map:

```text
80000000-de6fffff : System RAM
```

Therefore the likely ram_console physical start is:

```text
0xde700000 - 0x00200000 = 0xde500000
```

Initial Stage0 will write a complete fresh persistent RAM buffer at physical `0xde500000`:

```text
sig   = 0x43474244
start = 0
size  = strlen(marker)
data  = "MI4IOS6_STAGE0 ...\n"
```

If correct, the next normal Android boot should expose the marker via:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' | grep MI4IOS6_STAGE0
```

## Reboot options

Linux on this kernel uses `CONFIG_MSM_RESTART_V2=y`.

Relevant source:

- `external/android_kernel_xiaomi_cancro/arch/arm/mach-msm/restart.c`
- `external/android_kernel_xiaomi_cancro/arch/arm/mach-msm/include/mach/msm_iomap-8974.h`

Important physical addresses:

- `MSM8974_MPM2_PSHOLD_PHYS = 0xFC4AB000`
- Linux restart v2 eventually writes `0` to MPM2 PS_HOLD.

Stage0 v1 can attempt:

```text
*(volatile uint32_t *)0xFC4AB000 = 0;
```

Caveat: the Linux path disables the SPMI PMIC arbiter via SCM before lowering PS_HOLD. Stage0 v1 will not do that yet. If PS_HOLD does not work, the payload may hang until manual power-cycle.

## Stage0 v1 success criteria

1. Host can build an ARMv7 raw binary.
2. Host can package it as an Android boot image.
3. `sudo fastboot boot stage0.img` is accepted.
4. Device either reboots automatically or can be manually rebooted.
5. After normal Android boot, `/proc/last_kmsg` contains `MI4IOS6_STAGE0`.

## Why this matters for “running iOS”

If Stage0 succeeds, we have proven:

- the bootloader will execute our own non-Linux code,
- we can leave breadcrumbs without UART,
- we can start building the boot wrapper needed to synthesize XNU `boot_args`, construct an Apple-style device tree, and jump into an XNU-derived kernel.

This still does not run stock iOS 6. It is the first real hardware execution milestone on the path toward an XNU/Darwin-like kernel.
