# Source Baseline

Date: 2026-06-04

This document records the external source checkouts used as the current reference baseline for the Xiaomi Mi 4 (`cancro`) iOS/Darwin/XNU experiment.

The sources are checked out under `external/`, which is intentionally ignored by git because the checkouts are large and reproducible.

## Checkouts

| Path | Source | Branch/tag | Commit observed |
| --- | --- | --- | --- |
| `external/android_device_xiaomi_cancro` | `https://github.com/LineageOS/android_device_xiaomi_cancro.git` | `cm-14.1` | `7485559` |
| `external/android_kernel_xiaomi_cancro` | `https://github.com/LineageOS/android_kernel_xiaomi_cancro.git` | `cm-14.1` | `b263a891` |
| `external/xnu-2050.18.24` | `https://github.com/apple-oss-distributions/xnu.git` | `xnu-2050.18.24` | `d4e188f` |
| `external/xnu-4570.1.46` | `https://github.com/apple-oss-distributions/xnu.git` | `xnu-4570.1.46` | `76e12aa` |

## Why these sources

### `android_device_xiaomi_cancro`

Reference for:

- Android boot image parameters
- partition sizes
- fstab paths
- recovery configuration
- cancro-specific BoardConfig values

### `android_kernel_xiaomi_cancro`

Reference for:

- MSM8974 board support used by the phone
- UART / `ttyHSL` implementation
- Qualcomm timer and interrupt controller code
- ramoops / last_kmsg configuration
- display, storage, USB, and power-management clues
- appended device tree / QCDT handling

### `xnu-2050.18.24`

Reference for iOS 6 / Darwin 12-era XNU structure:

- Mach/BSD/IOKit kernel organization
- generic device-tree parser
- IOKit device tree import
- boot-args concepts

Important limitation: this public tag is not a complete iOS ARMv7 source release.

### `xnu-4570.1.46`

Reference for later public ARM XNU code:

- ARM boot arguments
- ARM platform expert
- ARM machine identification
- ARM device-tree expectations

Important limitation: this is not iOS 6-era code and should be treated as architecture reference material only.

## Recreate commands

```bash
mkdir -p external
cd external

git clone --depth 1 --branch cm-14.1 \
  https://github.com/LineageOS/android_device_xiaomi_cancro.git \
  android_device_xiaomi_cancro

git clone --depth 1 --branch cm-14.1 \
  https://github.com/LineageOS/android_kernel_xiaomi_cancro.git \
  android_kernel_xiaomi_cancro

git clone --depth 1 --branch xnu-2050.18.24 \
  https://github.com/apple-oss-distributions/xnu.git \
  xnu-2050.18.24

git clone --depth 1 --branch xnu-4570.1.46 \
  https://github.com/apple-oss-distributions/xnu.git \
  xnu-4570.1.46
```

## Current analysis focus

The next analysis pass should map:

1. Cancro/MSM8974 hardware primitives:
   - boot image and QCDT
   - RAM map and reserved persistent log memory
   - UART / `ttyHSL0`
   - timer
   - interrupt controller
   - framebuffer/display or a minimal headless path
2. XNU/Darwin boot interface:
   - `boot_args`
   - device tree parser/import
   - platform expert hooks
   - early console/log path
   - timer and interrupt bring-up
3. The gap between the two.
