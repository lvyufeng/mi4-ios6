# Source Baseline

Date: 2026-06-09

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

Stage50 consumed this checkout read-only and validated the exact selected ref (`xnu-2050.22.13`, `cc8a9b0c`, first-line `config/MasterVersion` `12.3.0`) through `stage50/xnu_workspace_validate.sh`. Stage50 records the missing public 2050 ARM build tree as a known limitation and prepares a minimal Stage51 object-subset path instead of claiming a full public ARM `mach_kernel` build is possible.

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

Stage75 is the current hardware-validated focus. It adds a Stage-owned XNU IOKit client-open / provider-claim / close-readiness dry-run contract on top of the Stage74 provider-callback / client-notification readiness dry-run contract, the Stage73 provider-notification / interest / delivery-readiness dry-run contract, the Stage72 lifecycle/register-service readiness dry-run contract, the Stage71 attach/start readiness dry-run contract, the Stage70 registry-plane / IODeviceTree topology dry-run contract, the Stage69 property-inheritance / registry-entry dry-run contract, the Stage68 catalog/property/personality dry-run contract, the Stage67 provider-plane publish/order dry-run contract, the Stage66 broader IOKit registry/service dry-run contract, the Stage65 local IOKit service/driver match dry-run contract, the Stage64 IOKit/platform-driver scaffold readiness contract, the Stage63 MSM8974 pexpert interrupt/timer hook readiness contract, and the Stage56 through Stage62 bootstrap, pmap/bootstrap, section-table dry-run, page-granular dry-run, cache/MMU attribute dry-run, multi-window dry-run, and safe live-table transition prerequisite contracts.

It consumes `external/xnu-upstream` at `xnu-2050.22.13` read-only for Darwin 12/iOS 6-era context, uses `external/xnu-4570.1.46` as a later public ARM/IOKit reference, classifies twenty-three selected public candidates through a fail-closed compile graph, allows only five graph-approved public pexpert sources (`pexpert/gen/device_tree.c`, `pexpert/gen/bootargs.c`, `pexpert/gen/pe_gen.c`, bounded public ARM `pexpert/arm/pe_bootargs.c`, and bounded public ARM `pexpert/arm/pe_consistent_debug.c`), explicitly blocks runtime-heavy public ARM pexpert sources (`pe_kprintf.c`, `pe_serial.c`, `pe_identify_machine.c`, and `pe_init.c`), treats public ARM VM/pmap sources and public IOKit files (`IODeviceTreeSupport.cpp`, `IOPlatformExpert.cpp`, and `IOCPU.cpp`) as reference-only, compiles five public objects plus one Stage75-owned shim object, links them into a host-only ARM ELF proof artifact under ignored `out/stage75/xnu-link/`, records zero undefined symbols, and validates the Stage-owned IOKit client-open / provider-claim / close-readiness dry-run path locally and on real `cancro` hardware through non-persistent `fastboot boot` only.

Local Stage75 validation confirmed loader target status `0x75000001` through the build artifacts, selected client-open entry mask `0x0000000f`, rejected client-open mask `0x00000010`, client-open matrix `0x0000001f`, client-open checksum `0xf4f8e8f5`, expected checksum `0xf4f8e8f5`, no open runtime execution, no claim runtime execution, no close runtime execution, four selected client-open / provider-claim / close-readiness dry-runs, one rejected/no-open/no-claim/no-close personality, fixed `CommandLine[256]` strings at 222 bytes including NUL, Android boot-image cmdlines at 1496 bytes including NUL, and `stage75-qcdt.img` `dt_size=2521088`. Non-persistent hardware validation recovered 248164 bytes from `/proc/last_kmsg`, confirmed the Stage75 client-open / provider-claim / close-readiness status, checksum, selected/rejected masks, loader roll-up, `loader_satisfied_mask=0xffffffff`, `loader_status=0x75000001`, and `kernel_entry returned success`, then returned to Android.

The current analysis/build pass focuses on Stage75:

1. Start from the Stage56/Stage57/Stage58/Stage59/Stage60/Stage61/Stage62/Stage63/Stage64/Stage65/Stage66/Stage67/Stage68/Stage69/Stage70/Stage71/Stage72/Stage73/Stage74 bootstrap, pmap/bootstrap, section-table dry-run, page-granular dry-run, cache/MMU attribute dry-run, multi-window dry-run, pmap transition prerequisite, pexpert interrupt/timer readiness, IOKit/platform scaffold readiness, local IOKit match dry-run, broader IOKit registry/service dry-run, IOKit provider-plane dry-run, IOKit catalog/property/personality dry-run, property-inheritance / registry-entry dry-run, registry-plane / IODeviceTree topology dry-run, attach/start readiness dry-run, lifecycle/register-service readiness dry-run, provider-notification / interest / delivery-readiness dry-run, and provider-callback / client-notification readiness dry-run contracts, keep the allowed public object subset stable, and refine real XNU IOKit/platform readiness deliberately.
2. Keep `external/xnu-upstream` detached at `xnu-2050.22.13` for Darwin 12/iOS 6-era context.
3. Use `external/xnu-4570.1.46` only as a public ARM/IOKit implementation reference where 2050 lacks ARMv7 files.
4. Keep outputs ignored under the next stage output directory and do not mutate `external/`.
5. Do not attempt a full public `mach_kernel` build, do not jump into XNU, do not execute public-XNU code, do not execute public pexpert/platform runtime code, do not execute public IOKit runtime code, do not execute IOKit service-match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, attach/start, register-service, notification, interest, delivery, callback, client-notification, open, claim, or close runtime code, and do not execute public VM/pmap runtime code on hardware yet.
6. Continue mapping the remaining real-XNU blockers: deeper MSM8974 pexpert/platform implementation, broader high-virtual `virtBase`/`physBase` pmap behavior, real IOKit provider/client open/claim/close execution prerequisites, kernelcache/prelink details, and later userspace/code-signing policy.

See also `docs/upstream-xnu-analysis.md`, `docs/experiment-53-stage50-public-xnu-workspace-cancro-scaffold.md`, `docs/experiment-54-stage51-public-xnu-object-subset-compile.md`, `docs/experiment-55-stage52-public-xnu-controlled-link-proof.md`, `docs/experiment-56-stage53-xnu-compile-graph.md`, `docs/experiment-57-stage54-pexpert-platform-compile-graph.md`, `docs/experiment-58-stage55-pexpert-consistent-debug-compile-graph.md`, `docs/experiment-59-stage56-xnu-bootstrap-mapping-contract.md`, `docs/experiment-60-stage57-xnu-pmap-bootstrap-contract.md`, `docs/experiment-61-stage58-xnu-pmap-table-dryrun-contract.md`, `docs/experiment-62-stage59-xnu-pmap-page-dryrun-contract.md`, `docs/experiment-63-stage60-xnu-pmap-attr-dryrun-contract.md`, `docs/experiment-64-stage61-xnu-pmap-multiwindow-dryrun-contract.md`, `docs/experiment-65-stage62-xnu-pmap-transition-dryrun-contract.md`, `docs/experiment-66-stage63-xnu-pexpert-hook-readiness-contract.md`, `docs/experiment-67-stage64-xnu-iokit-platform-scaffold.md`, `docs/experiment-68-stage65-xnu-iokit-match-dryrun-contract.md`, `docs/experiment-69-stage66-xnu-iokit-registry-service-dryrun-contract.md`, `docs/experiment-70-stage67-xnu-iokit-provider-plane-dryrun-contract.md`, `docs/experiment-71-stage68-xnu-iokit-catalog-property-dryrun-contract.md`, `docs/experiment-72-stage69-xnu-iokit-property-inheritance-dryrun-contract.md`, `docs/experiment-73-stage70-xnu-iokit-registry-topology-dryrun-contract.md`, `docs/experiment-74-stage71-xnu-iokit-attach-start-readiness-dryrun-contract.md`, `docs/experiment-75-stage72-xnu-iokit-lifecycle-register-service-readiness-dryrun-contract.md`, `docs/experiment-76-stage73-xnu-iokit-provider-notification-delivery-readiness-dryrun-contract.md`, `docs/experiment-77-stage74-xnu-iokit-provider-callback-client-notification-readiness-dryrun-contract.md`, and `docs/experiment-78-stage75-xnu-iokit-client-open-provider-claim-close-readiness-dryrun-contract.md` for the Stage50 scaffold result, Stage51 compile result, Stage52 controlled link-proof result, Stage53 compile-graph migration result, Stage54 pexpert/platform graph result, Stage55 consistent-debug proof, Stage56 bootstrap mapping contract proof, Stage57 pmap/bootstrap allocation contract proof, Stage58 pmap table dry-run proof, Stage59 page-granular dry-run proof, Stage60 pmap cache/MMU attribute dry-run proof, Stage61 pmap multi-window page-granular dry-run proof, Stage62 safe live-table transition prerequisite dry-run proof, Stage63 MSM8974 pexpert interrupt/timer hook readiness proof, Stage64 XNU IOKit/platform-driver scaffold readiness proof, Stage65 local IOKit service/driver match dry-run proof, Stage66 broader IOKit registry/service dry-run proof, Stage67 IOKit provider-plane publish/order dry-run proof, Stage68 IOKit catalog/property/personality dry-run proof, Stage69 IOKit property-inheritance / registry-entry dry-run proof, Stage70 IOKit registry-plane / IODeviceTree topology dry-run proof, Stage71 IOKit attach/start readiness dry-run proof, Stage72 IOKit lifecycle/register-service readiness dry-run proof, Stage73 IOKit provider-notification / interest / delivery-readiness dry-run proof, Stage74 IOKit provider-callback / client-notification readiness dry-run proof, and Stage75 IOKit client-open / provider-claim / close-readiness dry-run proof.
