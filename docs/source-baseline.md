# Source Baseline

Date: 2026-06-13

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

Stage80 is the current hardware-validated focus, building on the hardware-validated Stage78 early pmap/platform-init result (`21fb079`) and hardware-validated Stage79 PE-init-platform-false result (`ee6f5b9`). Stage80 keeps the Stage-owned `_start` / `arm_init`-shaped entry path, retains the Stage-owned early pmap/platform-init and `PE_init_platform(FALSE,args)`-shaped micro-sequences, and adds a Stage-owned post-PE_FALSE bootstrap/timebase-registration micro-sequence invoked from the Stage-owned `arm_init` stub before `arm_vm_init`. The retained Stage56-Stage62 bootstrap/pmap dry-run chain, Stage63 pexpert interrupt/timer readiness proof, Stage64-Stage75 IOKit dry-run ladder, Stage76 live-probe boundary, Stage77 entry-stub boundary, Stage78 early-init boundary, and Stage79 PE-init-platform-false boundary remain prerequisites.

The public source baseline remains unchanged: `external/xnu-upstream` stays detached at `xnu-2050.22.13` (`cc8a9b0c`, `MasterVersion 12.3.0`) for Darwin 12/iOS 6-era context, and `external/xnu-4570.1.46` remains a later public ARM/IOKit implementation reference. Stage80 still classifies the same selected public candidates through the fail-closed compile graph, allows only the bounded pexpert object subset (`pexpert/gen/device_tree.c`, `pexpert/gen/bootargs.c`, `pexpert/gen/pe_gen.c`, `pexpert/arm/pe_bootargs.c`, and `pexpert/arm/pe_consistent_debug.c`) for the host-only link proof, treats public ARM VM/pmap and public IOKit sources as reference-only, and keeps public pmap/IOKit compile/link/execute counts at zero.

Stage77 non-persistent hardware validation remains the first live handoff baseline: it recovered 248873 bytes from `/tmp/cancro-stage77-entry-stub-last_kmsg.txt` and confirmed `stage77_xnu_entry_stub_status=0x77000001`, satisfied mask `0x00000fff`, failure mask `0x00000000`, magic `0x58535442`, boot_args/device-tree valid, TTBR0 `0x000a8000` unchanged, SCTLR `0x00c5487b` unchanged, checksum `0x2952cf1d`, `loader_xnu_entry_stub_status_rollup=0x77000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x77000001`, and `kernel_entry returned success`.

Stage78 local validation confirmed `stage78-qcdt.img` `dt_size=2521088`, zero undefined symbols in both the payload and host-only public-XNU link proof, fixed `CommandLine[256]` strings at 246 bytes including NUL, the chosen Apple-DT boot-args string at 217 bytes including NUL, Android boot-image command lines at 1480 bytes including NUL, no stale Stage77 tokens under `stage78/`, and real disassembly calls `bl stage78_xnu_start_stub`, `bl stage78_arm_init_stub`, and `bl stage78_xnu_early_pmap_platform_init_run`. Stage78 non-persistent hardware validation recovered 254756 bytes from `/tmp/cancro-stage78-early-pmap-platform-last_kmsg.txt` and confirmed `stage78_xnu_early_init_status=0x78000001`, early-init satisfied mask `0x01ffffff`, failure mask `0x00000000`, control registers unchanged, platform facts valid, safety boundary preserved, no live pmap install, no TTBR write, no TLB invalidation, no cache-policy change, no persistent write, `stage78_xnu_entry_stub_status=0x78000001`, entry satisfied mask `0x00001fff`, `loader_xnu_early_init_status_rollup=0x78000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x78000001`, and `kernel_entry returned success`.

Stage79 local validation confirmed `stage79-qcdt.img` `dt_size=2521088`, zero undefined symbols in both the payload and host-only public-XNU link proof, fixed `CommandLine[256]` strings at 238 bytes including NUL, the chosen Apple-DT boot-args string at 153 bytes including NUL, Android boot-image command lines at 1283 bytes including NUL, no stale Stage78 tokens under Stage79 code/build files, and real disassembly calls `bl stage79_xnu_start_stub`, `bl stage79_arm_init_stub`, `bl stage79_xnu_early_pmap_platform_init_run`, and `bl stage79_xnu_pe_init_platform_false_run`. Stage79 non-persistent hardware validation recovered 263638 bytes from `/tmp/cancro-stage79-pe-init-platform-false-last_kmsg.txt` and confirmed `stage79_xnu_pe_init_platform_false_status=0x79000001`, PE-init satisfied mask `0x01ffffff`, failure mask `0x00000000`, PE_state/deviceTreeHead matched, DTInit-shaped and identify-machine-shaped facts matched, explicit zero counters for public `PE_init_platform`, `DTInit`, `pe_identify_machine`, public XNU/pexpert/pmap/IOKit runtime, generated Mach-O execution, live pmap install, control-register writes, TLB invalidation, cache-policy change, and persistent writes, `loader_xnu_pe_init_platform_false_status_rollup=0x79000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x79000001`, and `kernel_entry returned success`.

Stage80 local validation confirmed `stage80-qcdt.img` `dt_size=2521088`, zero undefined symbols in both the payload and host-only public-XNU link proof, fixed `CommandLine[256]` strings at 227 bytes including NUL, the chosen Apple-DT boot-args string at 198 bytes including NUL, Android boot-image command lines at 1372 bytes including NUL, no stale Stage79 tokens under Stage80 code/build files, no legacy failure-status OR collision sites outside the `STAGE80_STATUS_FAIL` macro, and real disassembly calls `bl stage80_xnu_start_stub`, `bl stage80_arm_init_stub`, `bl stage80_xnu_early_pmap_platform_init_run`, `bl stage80_xnu_pe_init_platform_false_run`, and `bl stage80_xnu_arm_init_post_pe_bootstrap_run`. Stage80 non-persistent hardware validation recovered 271930 bytes from `/tmp/cancro-stage80-arm-init-post-pe-bootstrap-last_kmsg.txt` and confirmed `stage80_xnu_arm_init_post_pe_bootstrap_status=0x80000001`, post-PE satisfied mask `0x01ffffff`, failure mask `0x00000000`, CPU count `0x00000004`, master CPU valid, BootCpuData/CpuDataEntries populated, timebase callback registered at `0x0124f800`, boot-arg parse OK, explicit stop-before-`arm_vm_init`, explicit zero counters for public bootstrap routines, public `arm_vm_init`, live pmap install, TLB invalidation, cache-policy change, and persistent writes, `loader_xnu_arm_init_post_pe_bootstrap_status_rollup=0x80000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x80000001`, and `kernel_entry returned success`, recovering cleanly to Android with no persistent change.

The current analysis/build pass focuses on the next post-PE_FALSE Method-C boundary:

1. Start from the hardware-validated Stage78 early-init result, hardware-validated Stage79 PE-init-platform-false result, and locally validated Stage80 post-PE bootstrap/timebase result as the live handoff baseline.
2. Keep `external/xnu-upstream` detached at `xnu-2050.22.13` for Darwin 12/iOS 6-era context.
3. Use `external/xnu-4570.1.46` only as a public ARM/IOKit implementation reference where 2050 lacks ARMv7 files.
4. Keep outputs ignored under the next stage output directory and do not mutate `external/`.
5. Do not attempt a full public `mach_kernel` build, do not enter public XNU `_start` / `arm_init`, do not call public `PE_init_platform`, do not call public `DTInit`, do not call public `pe_identify_machine`, do not execute public-XNU pexpert/pmap/IOKit code, do not execute generated Mach-O bytes, do not install live pmap tables, do not write TTBR0/TTBCR/DACR/SCTLR for a proposed XNU pmap transition, do not invalidate TLBs, do not change cache policy, and do not write persistent storage.
6. Continue mapping the next blockers: a deeper MSM8974 `PE_init_platform(FALSE,args)` platform-hook model, stricter DT/interrupt/timer handoff verification, broader high-virtual `virtBase`/`physBase` pmap behavior, kernelcache/prelink details, and later userspace/code-signing policy.

See also `docs/upstream-xnu-analysis.md`, `docs/experiment-79-stage76-live-xnu-like-execution-probe.md`, `docs/experiment-80-stage77-xnu-entry-stub.md`, `docs/experiment-81-stage78-early-pmap-platform-init.md`, `docs/experiment-82-stage79-pe-init-platform-false.md`, and `docs/experiment-83-stage80-arm-init-post-pe-bootstrap.md` for the current live-execution pivot, plus the Stage50-Stage80 experiment documents for the preceding public-XNU workspace, object-subset, link-proof, bootstrap, pmap, pexpert, IOKit dry-run, and Method-C prerequisites.
