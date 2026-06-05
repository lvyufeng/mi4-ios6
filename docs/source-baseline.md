# Source Baseline

Date: 2026-06-05

This document records the external source checkouts used as the current reference baseline for the Xiaomi Mi 4 (`cancro`) iOS/Darwin/XNU experiment.

The sources are checked out under `external/`, which is intentionally ignored by git because the checkouts are large and reproducible. Do not commit external source checkouts.

## Checkouts

| Path | Source | Branch/tag | Commit observed | Purpose |
| --- | --- | --- | --- | --- |
| `external/android_device_xiaomi_cancro` | `https://github.com/LineageOS/android_device_xiaomi_cancro.git` | `cm-14.1` | `7485559` | Cancro Android device-tree/BoardConfig baseline |
| `external/android_kernel_xiaomi_cancro` | `https://github.com/LineageOS/android_kernel_xiaomi_cancro.git` | `cm-14.1` | `b263a891` | MSM8974 Linux hardware-reference baseline |
| `external/xnu-upstream` | `https://github.com/apple-oss-distributions/xnu.git` | detached `xnu-2050.22.13` | `cc8a9b0c` | Selected public iOS 6 / Darwin 12.3-era XNU baseline for current analysis |
| `external/xnu-2050.18.24` | `https://github.com/apple-oss-distributions/xnu.git` | detached `xnu-2050.18.24` | `d4e188f` | Earlier Darwin 12 / iOS 6-era public XNU structure reference |
| `external/apple-xnu-rel-2050` | `https://github.com/apple-oss-distributions/xnu.git` | `rel/xnu-2050` / `xnu-2050.48.11` | `4abd0e59` | Later public Darwin 12 branch-head reference |
| `external/xnu-4570.1.46` | `https://github.com/apple-oss-distributions/xnu.git` | detached `xnu-4570.1.46` | `76e12aa` | Later public ARM/ARMv7 XNU implementation reference |
| `external/distribution-iOS-ios-613` | Apple OSS distribution metadata | `iOS 6.1.3` manifest | local checkout | iOS 6.1.3 OSS project list; notably no public XNU tag listed |
| `external/distribution-iOS-rel-iOS-6` | Apple OSS distribution metadata | `iOS 6.1.3` manifest | local checkout | iOS 6 release metadata cross-check |

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

### `xnu-upstream` at `xnu-2050.22.13`

Selected public iOS 6 / Darwin 12-era XNU baseline for current work.

The checkout was initially cloned from upstream `main`, but the user correctly noted that analysis should use an iOS 6-matching branch/tag. It is therefore detached at:

```text
xnu-2050.22.13
commit cc8a9b0ce917bb7115f5c97a78b38db871557db0
config/MasterVersion 12.3.0
```

Rationale:

- The local public iOS 6.1.3 distribution manifests do not list an XNU project/tag.
- The public `xnu-2050.*` family is Darwin 12 / iOS 6-era.
- `xnu-2050.22.13` reports `MasterVersion` `12.3.0`, making it the closest public Darwin 12.3 / iOS 6.1.x-era source baseline available in the checked Apple OSS tags.
- This tree is useful for Darwin 12-era kernel organization, Mach-O headers, device-tree parser shape, IOKit import concepts, and source-layout comparison.

Important limitation: the public 2050 trees here are not complete iOS ARMv7 XNU source releases, so later public ARM sources remain necessary for practical ARM boot-path details.

### `xnu-2050.18.24`

Earlier Darwin 12 / iOS 6-era public XNU structure reference:

- `config/MasterVersion` is `12.2.0`.
- Useful for comparison against the selected `xnu-2050.22.13` baseline.
- Includes generic device-tree and Mach-O concepts.

Important limitation: this public tag is not a complete iOS ARMv7 source release.

### `apple-xnu-rel-2050`

Public `rel/xnu-2050` branch-head checkout:

- Currently at `xnu-2050.48.11`.
- `config/MasterVersion` is `12.5.0`.
- Useful as a later Darwin 12 reference, but less directly matched to iOS 6.1.3 than `xnu-2050.22.13`.

### `xnu-4570.1.46`

Reference for later public ARM XNU code:

- ARM boot arguments
- ARM `_start` / `arm_init` entry path
- ARM platform expert
- ARM machine identification
- ARM device-tree expectations
- ARM VM/pmap bootstrap
- ARM timer and interrupt hook structure

Important limitation: this is not iOS 6-era code and should be treated as architecture reference material only.

### iOS 6 distribution manifests

The local `distribution-iOS` checkouts are useful to avoid guessing which public Apple OSS projects were listed for iOS 6.1.3. Their `release.json` files list projects such as JavaScriptCore, WebCore, cctools, gdb, ld64, libiconv, and libstdcxx, but do not list a public XNU tag. This is why the selected XNU source baseline is the closest public Darwin 12 tag rather than an exact manifest-provided iOS 6.1.3 ARM XNU release.

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

git clone https://github.com/apple-oss-distributions/xnu.git xnu-upstream
git -C xnu-upstream fetch --tags origin
git -C xnu-upstream switch --detach xnu-2050.22.13

git clone --depth 1 --branch xnu-2050.18.24 \
  https://github.com/apple-oss-distributions/xnu.git \
  xnu-2050.18.24

git clone https://github.com/apple-oss-distributions/xnu.git apple-xnu-rel-2050
git -C apple-xnu-rel-2050 switch rel/xnu-2050

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
   - `_start` / `arm_init` entry expectations
   - Apple flattened device tree parser/import
   - platform expert hooks
   - early console/log path
   - VM/pmap bootstrap and `topOfKernelData`
   - timer and interrupt bring-up
   - Mach-O / kernelcache segment layout
3. The gap between Stage42 and a real XNU loader/handoff.

See also `docs/upstream-xnu-analysis.md` for the Stage42 comparison and revised Stage43 direction.
