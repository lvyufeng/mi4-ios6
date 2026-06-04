# iOS 6.1.3 Apple OSS Baseline Notes

Date: 2026-06-04

Goal: verify what Apple's public Open Source repositories provide for iOS 6.1.3 and how that should affect the Xiaomi Mi 4 XNU/Darwin bring-up path.

## Repositories checked

User-supplied references:

```text
https://github.com/apple-oss-distributions/distribution-iOS/tree/ios-613
https://github.com/apple-oss-distributions/xnu
```

GitHub access was initially flaky for `git` smart HTTP, but later recovered. The following refs were confirmed reachable:

```text
apple-oss-distributions/xnu:
  refs/heads/main
  refs/heads/rel/xnu-2050
  refs/heads/rel/xnu-4570
  refs/tags/xnu-2050.18.24
  refs/tags/xnu-2050.22.13
  refs/tags/xnu-2050.24.15
  refs/tags/xnu-2050.48.11
```

The `distribution-iOS` repository has `ios-613` as a tag, not a branch:

```text
refs/heads/rel/iOS-6 -> 674cfc474a8a561738534ae50dbd665ef87eaaaa
refs/tags/ios-613   -> same commit/tag target for iOS 6.1.3
```

The local checkout is:

```text
external/distribution-iOS-ios-613/
commit/tag: 674cfc474a8a561738534ae50dbd665ef87eaaaa (iOS 6.1.3)
```

## What `distribution-iOS@ios-613` contains

The checkout contains only:

```text
.gitmodules
release.json
```

`release.json` identifies the release:

```json
{
  "major_release": "iOS 6",
  "release": "iOS 6.1.3",
  "projects": [ ... ]
}
```

Projects listed in `release.json`:

```text
JavaScriptCore-1097.13
WTFEmbedded-20
WebCore-1640.28
cctools-836
gdb-1822
ld64-134.9
libiconv-35
libstdcxx-56
```

Important finding: **`distribution-iOS@ios-613` does not list XNU**. It is an Apple Open Source distribution manifest for some iOS 6.1.3 components, not a complete iOS source tree and not even a complete kernel-source manifest.

## What this means

The user's intuition is correct in an important sense: Apple does publish iOS-related open-source component distributions.

But this does **not** mean full iOS is open source. Missing from public OSS include, among many other things:

- UIKit / SpringBoard / BackBoard private stack
- many Core* and private frameworks
- full graphics/touch/audio/power management userspace stack
- iBoot/SecureROM
- Apple SoC-specific closed drivers and firmware
- code-signing production policy/material

For this project, the useful conclusion is narrower and still valuable:

> We can legally align the kernel/boot research with public Apple OSS components, but the Xiaomi Mi 4 path remains an XNU/Darwin-like bring-up first, not a direct full iOS ROM build.

## XNU baseline implications

Because `distribution-iOS@ios-613` does not pin XNU, use the public XNU repository directly.

Confirmed Apple GitHub refs include:

```text
rel/xnu-2050
xnu-2050.18.24
xnu-2050.22.13
xnu-2050.24.15
xnu-2050.48.11
rel/xnu-4570
```

Local source currently present:

```text
external/xnu-2050.18.24
external/xnu-4570.1.46
external/apple-xnu-rel-2050
```

Observed versions:

```text
external/xnu-2050.18.24/config/MasterVersion       -> 12.2.0
external/apple-xnu-rel-2050/config/MasterVersion   -> 12.5.0 (xnu-2050.48.11)
```

Important practical detail:

- The older `xnu-2050.*` public source is useful for Darwin/iOS-6-era structure, but the public checkout inspected here does not expose the modern ARM boot source files used by this project (`pexpert/pexpert/arm/boot.h`, `osfmk/arm/arm_init.c`, etc.).
- The later `xnu-4570.1.46` public source remains the practical reference for ARM `boot_args`, ARM `arm_init`, Apple flattened device tree parsing, and pexpert/ARM entry behavior.

Therefore Stage5 should use a two-source alignment strategy:

1. Use `xnu-2050.*` / iOS 6.1.3 OSS release notes for era/version context.
2. Use `xnu-4570.1.46` ARM public source to shape the legal ARM boot contract and early pexpert-like skeleton.

## Stage5 impact

Stage5 should proceed as an **XNU-adjacent skeleton**, not a direct xnu-2050 build:

- boot wrapper constructs `boot_args` and Apple-DT,
- a separate `kernel_entry(struct boot_args *)` consumes that contract,
- pexpert-like code discovers `/memory`, `/cpus`, `/interrupt-controller`, and `/timer`,
- Stage4 timebase maps into XNU-like `ml_init_timebase()` / `ml_get_timebase()` stubs,
- Stage4 exception vectors stay installed to log aborts while increasingly XNU-like code is tested.

This keeps the implementation legally aligned and technically realistic while acknowledging that full iOS userspace remains out of scope for the current hardware bring-up stage.
