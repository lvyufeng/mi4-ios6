# Experiment 67 — Stage64 XNU IOKit/platform-driver scaffold readiness contract

## Goal

Stage64 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage63 MSM8974 pexpert interrupt/timer hook readiness proof toward the next real-XNU blocker: IOKit/platform-driver scaffolding.

The goal is not to run XNU, not to execute IOKit, not to execute a platform driver, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the Apple flattened device tree now exposes local registry-plane and MSM8974 platform-driver match facts that can later feed an IOKit/platform implementation.

## Safety boundary

Stage64 preserves all Stage63 boundaries:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No Stage-owned MSM8974 platform-driver execution.
- No generated Mach-O execution.
- No XNU `_start` / `arm_init` jump.
- No proposed physical load-address writes.
- No proposed TTE/pmap workspace writes.
- No live proposed XNU/pmap table install.
- No TTBR/TTBCR/DACR/SCTLR writes for proposed pmap install.
- No TLB invalidation for proposed pmap install.
- No cache policy change.
- No mutation of ignored `external/` public XNU checkouts.

The public object subset remains bounded to:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage64/xnu_object_shims.c
```

Public ARM VM/pmap files remain reference-only. Stage64 additionally classifies these public IOKit files as reference-only:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, linked, or executed.

## Implementation

Stage64 adds:

```text
stage64/xnu_iokit_platform_scaffold_contract.c
struct stage64_xnu_iokit_platform_scaffold_contract
```

The contract imports source facts from:

- the Stage64 pexpert hook readiness contract,
- the Stage64 public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

Stage64 extends the Apple flattened device tree with two Stage-owned top-level nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
```

`/iokit-platform-scaffold` records a local registry-plane import shape:

```text
compatible=apple,iokit-platform-scaffold
IOClass=IOPlatformExpertDevice
IOProviderClass=IODeviceTree:/
device_type=platform
registry-plane=IODeviceTree
```

`/msm8974-platform-driver` records Stage-owned MSM8974 platform-driver match facts only:

```text
compatible=qcom,msm8974-cancro-stage64
IOClass=MSM8974PlatformExpert
IOProviderClass=IOPlatformExpertDevice
IOMatchCategory=Stage64LocalPlatformScaffold
reg=<0xf9000000 0x1000 0xf9020000 0x1000>
cpu-count=4
timebase-frequency=19200000
stage-owned-local-only=1
```

The contract proves:

- Stage63-renumbered pexpert hook readiness is OK.
- Public compile graph/object subset/link facts are OK.
- Public IOKit reference mask is complete (`0x00000007`).
- Public IOKit runtime-blocked mask is complete (`0x00000007`).
- Public IOKit compile/link counts are zero.
- `/device-tree`, `/iokit-platform-scaffold`, and `/msm8974-platform-driver` Apple-DT facts are present.
- The MSM8974 match facts exactly record GIC distributor base `0xf9000000`, timer base `0xf9020000`, 19.2 MHz timebase, and CPU count 4.
- Public pexpert/platform runtime, public IOKit runtime, Stage-owned platform-driver runtime, public VM/pmap runtime, generated Mach-O execution, XNU entry execution, proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage64 does not add a 33rd top-level loader bit. Instead, it records the IOKit/platform scaffold as a contract roll-up and refines the existing platform gap mask by clearing only `STAGE64_PLATFORM_GAP_IOKIT_STACK` after the scaffold proof succeeds. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x64000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage64/build.sh
```

Final build size:

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

Boot image parse checks confirmed:

```text
stage64.img:      page_size=2048 kernel_size=262336 dt_size=0
stage64-qcdt.img: page_size=2048 kernel_size=262336 dt_size=2521088 (0x267800)
```

Undefined symbol checks were clean for both:

```text
out/stage64/stage64.elf
out/stage64/xnu-link/stage64-xnu-link.elf
```

Static validation facts:

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

Hardware validation used non-persistent boot only:

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

Key IOKit/platform scaffold markers:

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
MI4IOS6_STAGE64_XNU loader_xnu_iokit_platform_scaffold_contract_status_rollup=0x64000001
```

Retained pexpert and pmap transition source facts also remained OK:

```text
MI4IOS6_STAGE64_XNU stage64_xnu_pmap_transition_dryrun_contract_status=0x64000001
MI4IOS6_STAGE64_XNU stage64_xnu_pmap_transition_dryrun_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE64_XNU stage64_xnu_pmap_transition_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE64_XNU stage64_xnu_pexpert_hook_readiness_contract_status=0x64000001
MI4IOS6_STAGE64_XNU stage64_xnu_pexpert_hook_readiness_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE64_XNU stage64_xnu_pexpert_hook_readiness_contract_failure_mask=0x00000000
```

Final success markers:

```text
MI4IOS6_STAGE64_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE64_XNU loader_status=0x64000001
MI4IOS6_STAGE64_XNU kernel_entry ok
MI4IOS6_STAGE64 kernel_entry returned success
```

No `kernel_entry returned failure` or `mmu high bootstrap selftest failed` marker was present in the successful run.

## Result

Stage64 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned XNU IOKit/platform-driver scaffold readiness contract while preserving every no-XNU/no-public-runtime/no-I/O-Kit-runtime/no-live-pmap-install/no-persistent-write boundary from Stage63.

The next safe direction is a still-local IOKit service/driver matching dry-run, deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof before any attempt to jump into XNU, execute public runtime code, install proposed live pmap tables, invalidate TLBs, change cache policy, or perform persistent writes.
