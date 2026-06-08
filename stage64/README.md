# Stage64 — XNU IOKit/platform-driver scaffold readiness contract

Stage64 keeps the Stage63 MSM8974 pexpert interrupt/timer readiness proof and adds a Stage-owned **XNU IOKit/platform-driver scaffold readiness contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, and not to execute any public ARM pexpert/platform or VM/pmap runtime path. Stage64 proves that the Stage-owned Apple flattened device tree now contains enough local registry-plane and MSM8974 platform-driver match facts for a later IOKit/platform bring-up step, while all public IOKit files remain reference-only.

## Safety model

Stage64 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No Stage-owned MSM8974 platform-driver execution.
- No generated Mach-O fixture execution.
- No XNU `_start` / `arm_init` jump.
- No proposed physical load-address writes.
- No proposed TTE/pmap workspace writes.
- No live proposed XNU/pmap table install.
- No TTBR/TTBCR/DACR/SCTLR writes for proposed pmap install.
- No TLB invalidation for proposed pmap install.
- No cache policy change.
- No mutation of ignored `external/` public XNU checkouts.

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only the earlier controlled local safety tests; Stage64 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage64/xnu_object_shims.c
```

Public ARM VM/pmap files remain reference-only:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/vm/pmap.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

Stage64 adds public IOKit files as reference-only compile-graph inputs:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, not linked, and not executed.

## What Stage64 adds

Stage64 adds a fail-closed IOKit/platform scaffold contract:

```text
stage64/xnu_iokit_platform_scaffold_contract.c
struct stage64_xnu_iokit_platform_scaffold_contract
```

The contract imports source facts from:

- the Stage64-renumbered pexpert hook readiness contract,
- the public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- loader safety masks,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree gains two Stage-owned top-level nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
```

`/iokit-platform-scaffold` records a local registry-plane import shape with properties such as `IOClass`, `IOProviderClass`, `device_type`, and `registry-plane`.

`/msm8974-platform-driver` records local MSM8974 platform-driver match facts only:

```text
compatible=qcom,msm8974-cancro-stage64
IOClass=MSM8974PlatformExpert
IOProviderClass=IOPlatformExpertDevice
IOMatchCategory=Stage64LocalPlatformScaffold
GIC distributor base=0xf9000000
timer base=0xf9020000
timebase-frequency=19200000
cpu-count=4
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage64 does not add a new top-level loader satisfied bit. Instead, the IOKit/platform scaffold contract is a roll-up that clears only the existing `STAGE64_PLATFORM_GAP_IOKIT_STACK` gap after the scaffold proof succeeds. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x64000001
```

## Selected public XNU baselines

Stage64 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage64/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage64_xnu_compile_graph_status=0x64000001
stage64_xnu_compile_graph_required_mask=0x7fffffff
stage64_xnu_compile_graph_satisfied_mask=0x7fffffff
stage64_xnu_compile_graph_failure_mask=0x00000000
stage64_xnu_compile_graph_candidate_count=0x00000017
stage64_xnu_compile_graph_allowed_compile_count=0x00000005
stage64_xnu_compile_graph_allowed_link_count=0x00000005
stage64_xnu_compile_graph_forbidden_count=0x00000012
stage64_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage64_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage64_xnu_compile_graph_pmap_public_link_count=0x00000000
stage64_xnu_compile_graph_iokit_reference_mask=0x00000007
stage64_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage64_xnu_compile_graph_iokit_reference_count=0x00000003
stage64_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage64_xnu_compile_graph_iokit_public_link_count=0x00000000
stage64_xnu_compile_graph_iokit_reference_only=0x00000001
stage64_xnu_compile_graph_platform_reference_count=0x00000009
stage64_xnu_compile_graph_blocked_runtime_count=0x00000004
stage64_xnu_object_subset_status=0x64000001
stage64_xnu_object_count=0x00000006
stage64_xnu_object_duplicate_symbol_count=0x00000000
stage64_xnu_link_status=0x64000001
stage64_xnu_link_object_count=0x00000006
stage64_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage64/stage64.elf` and `arm-none-eabi-nm -u out/stage64/xnu-link/stage64-xnu-link.elf` both reported no undefined symbols.

## Retained prerequisite contracts

Stage64 retains all Stage56 through Stage63 XNU mapping/pmap/platform prerequisites as fail-closed source facts:

```text
stage64_xnu_bootstrap_contract_status=0x64000001
stage64_xnu_pmap_bootstrap_contract_status=0x64000001
stage64_xnu_pmap_table_dryrun_contract_status=0x64000001
stage64_xnu_pmap_page_dryrun_contract_status=0x64000001
stage64_xnu_pmap_attr_dryrun_contract_status=0x64000001
stage64_xnu_pmap_multiwindow_dryrun_contract_status=0x64000001
stage64_xnu_pmap_transition_dryrun_contract_status=0x64000001
stage64_xnu_pexpert_hook_readiness_contract_status=0x64000001
```

Hardware validation confirmed the retained pmap transition and pexpert readiness roll-ups:

```text
MI4IOS6_STAGE64_XNU stage64_xnu_pmap_transition_dryrun_contract_status=0x64000001
MI4IOS6_STAGE64_XNU stage64_xnu_pmap_transition_dryrun_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE64_XNU stage64_xnu_pmap_transition_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE64_XNU stage64_xnu_pexpert_hook_readiness_contract_status=0x64000001
MI4IOS6_STAGE64_XNU stage64_xnu_pexpert_hook_readiness_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE64_XNU stage64_xnu_pexpert_hook_readiness_contract_failure_mask=0x00000000
MI4IOS6_STAGE64_XNU loader_xnu_pexpert_hook_readiness_contract_status_rollup=0x64000001
```

## IOKit/platform scaffold contract

Confirmed target-side IOKit/platform scaffold markers:

```text
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_platform_scaffold_contract_status=0x64000001
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_platform_scaffold_contract_required_mask=0x00ffffff
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_platform_scaffold_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_platform_scaffold_contract_failure_mask=0x00000000
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_platform_scaffold_contract_checksum=0x89af157f
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_reference_mask=0x00000007
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_runtime_blocked_mask=0x00000007
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_public_compile_count=0x00000000
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_public_link_count=0x00000000
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE64_XNU stage64_xnu_iokit_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE64_XNU loader_xnu_iokit_platform_scaffold_contract_status=0x64000001
MI4IOS6_STAGE64_XNU loader_xnu_iokit_platform_scaffold_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE64_XNU loader_xnu_iokit_platform_scaffold_contract_failure_mask=0x00000000
MI4IOS6_STAGE64_XNU loader_xnu_iokit_platform_scaffold_contract_checksum=0x89af157f
MI4IOS6_STAGE64_XNU loader_xnu_iokit_platform_scaffold_contract_status_rollup=0x64000001
```

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage64/build.sh
```

Final size:

```text
text=262336 data=0 bss=398396 dec=660732 hex=a14fc
```

Hashes:

```text
cf90c4670aac7326af1456b8f7ea82ac786ebbaa7972997bea940f1b3f838cfb  out/stage64/stage64_fixture.macho
e61724c0c08d46adf3fa1feb380e654f1c4e4724f22af804a6757946f20e6ac2  out/stage64/stage64.elf
41b5ba197355d32a4ce1197c70a2fa86efa7ef6bdbee7ef77ae2d9a56052c691  out/stage64/stage64.bin
fd9f7983b96decd8fd824eb514c6503916f370ad9ddb222a4b5d9823d2ef57d8  out/stage64/stage64.img
b6dc97d83cb2d1d17cbcbc36f8997cae97b74e7a4bbe2a8623b4be20c24f6e51  out/stage64/stage64-qcdt.img
```

## Boot image parse facts

```text
out/stage64/stage64.img:
  page_size=2048
  kernel_size=262336 (0x400c0)
  dt_size=0
  kernel_sha256=41b5ba197355d32a4ce1197c70a2fa86efa7ef6bdbee7ef77ae2d9a56052c691

out/stage64/stage64-qcdt.img:
  page_size=2048
  kernel_size=262336 (0x400c0)
  dt_size=2521088 (0x267800)
  kernel_sha256=41b5ba197355d32a4ce1197c70a2fa86efa7ef6bdbee7ef77ae2d9a56052c691
  dt_sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage64/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage64/stage64.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage64/stage64-qcdt.img
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage64/stage64.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage64/xnu-link/stage64-xnu-link.elf
```

Validation facts:

```text
stage64/boot_args.c command_line_len_with_nul=191 fits256=True
stage64/stage64_main.c command_line_len_with_nul=191 fits256=True
stage64/xnu_object_shims.c command_line_len_with_nul=160 fits256=True
stale_marker_violations=0
public_vm_pmap_iokit_command_violations=0
out/stage64 ignored
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage64/stage64-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage64-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage64-last_kmsg.txt
size=182310 bytes
stage64_marker_count=2599
stage64_xnu_marker_count=2571
```

Final success markers:

```text
MI4IOS6_STAGE64_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE64_XNU loader_status=0x64000001
MI4IOS6_STAGE64_XNU kernel_entry ok
MI4IOS6_STAGE64 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure` or `mmu high bootstrap selftest failed` in the successful run.

## Result

Stage64 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned XNU IOKit/platform-driver scaffold readiness contract over Stage63 pexpert hook readiness, Apple-DT registry-plane nodes, MSM8974 platform-driver match facts, public IOKit reference-only classification, and retained pmap transition safety facts.

The next safe direction is a still-local deeper MSM8974 pexpert/platform implementation proof, IOKit service/driver matching dry-run, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
