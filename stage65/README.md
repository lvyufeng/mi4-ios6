# Stage65 — XNU IOKit service/driver match dry-run contract

Stage65 keeps the Stage64 XNU IOKit/platform-driver scaffold readiness proof and adds a Stage-owned **local IOKit service/driver match dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute an MSM8974 platform driver, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage65 proves that the Stage-owned Apple flattened device tree now contains enough local provider/driver properties for a later IOKit service matching step, while all matching decisions remain a dry-run over Stage-owned data.

## Safety model

Stage65 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No Stage-owned MSM8974 platform-driver execution.
- No IOKit service-match runtime execution.
- No generated Mach-O fixture execution.
- No XNU `_start` / `arm_init` jump.
- No proposed physical load-address writes.
- No proposed TTE/pmap workspace writes.
- No live proposed XNU/pmap table install.
- No TTBR/TTBCR/DACR/SCTLR writes for proposed pmap install.
- No TLB invalidation for proposed pmap install.
- No cache policy change.
- No mutation of ignored `external/` public XNU checkouts.

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage65 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage65/xnu_object_shims.c
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

Public IOKit files remain reference-only:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, not linked, and not executed.

## What Stage65 adds

Stage65 retains the Stage64-renumbered IOKit/platform scaffold contract:

```text
stage65/xnu_iokit_platform_scaffold_contract.c
struct stage65_xnu_iokit_platform_scaffold_contract
```

It then adds a fail-closed local IOKit match dry-run contract:

```text
stage65/xnu_iokit_match_dryrun_contract.c
struct stage65_xnu_iokit_match_dryrun_contract
```

The match dry-run imports source facts from:

- the Stage65 IOKit/platform scaffold contract,
- the Stage65-renumbered pexpert hook readiness contract,
- the public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- loader safety masks,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree keeps the two Stage-owned top-level nodes introduced by Stage64:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
```

`/iokit-platform-scaffold` records a local registry-plane provider shape with properties such as `IOClass`, `IOProviderClass`, `device_type`, and `registry-plane`.

`/msm8974-platform-driver` records local MSM8974 platform-driver match facts only:

```text
compatible=qcom,msm8974-cancro-stage65
IOClass=MSM8974PlatformExpert
IOProviderClass=IOPlatformExpertDevice
IOMatchCategory=Stage65LocalPlatformScaffold
IOProbeScore=0x00000650
GIC distributor base=0xf9000000
timer base=0xf9020000
timebase-frequency=19200000
cpu-count=4
stage-owned-local-only=1
```

The match dry-run verifies provider and driver facts without executing IOKit:

```text
provider_class_match=1
provider_path_match=1
match_category_match=1
compatible_match=1
dryrun_match_count=1
attach_deferred=1
start_deferred=1
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage65 does not add a new top-level loader satisfied bit. Instead, both IOKit/platform contracts are copied into loader preflight and logged as roll-ups. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x65000001
```

## Selected public XNU baselines

Stage65 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage65/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage65_xnu_compile_graph_status=0x65000001
stage65_xnu_compile_graph_required_mask=0x7fffffff
stage65_xnu_compile_graph_satisfied_mask=0x7fffffff
stage65_xnu_compile_graph_failure_mask=0x00000000
stage65_xnu_compile_graph_candidate_count=0x00000017
stage65_xnu_compile_graph_allowed_compile_count=0x00000005
stage65_xnu_compile_graph_allowed_link_count=0x00000005
stage65_xnu_compile_graph_forbidden_count=0x00000012
stage65_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage65_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage65_xnu_compile_graph_pmap_public_link_count=0x00000000
stage65_xnu_compile_graph_iokit_reference_mask=0x00000007
stage65_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage65_xnu_compile_graph_iokit_reference_count=0x00000003
stage65_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage65_xnu_compile_graph_iokit_public_link_count=0x00000000
stage65_xnu_compile_graph_iokit_reference_only=0x00000001
stage65_xnu_compile_graph_platform_reference_count=0x00000009
stage65_xnu_compile_graph_blocked_runtime_count=0x00000004
stage65_xnu_object_subset_status=0x65000001
stage65_xnu_object_count=0x00000006
stage65_xnu_object_duplicate_symbol_count=0x00000000
stage65_xnu_link_status=0x65000001
stage65_xnu_link_object_count=0x00000006
stage65_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage65/stage65.elf` and `arm-none-eabi-nm -u out/stage65/xnu-link/stage65-xnu-link.elf` both reported no undefined symbols.

## Retained prerequisite contracts

Stage65 retains all Stage56 through Stage64 XNU mapping/pmap/platform prerequisites as fail-closed source facts:

```text
stage65_xnu_bootstrap_contract_status=0x65000001
stage65_xnu_pmap_bootstrap_contract_status=0x65000001
stage65_xnu_pmap_table_dryrun_contract_status=0x65000001
stage65_xnu_pmap_page_dryrun_contract_status=0x65000001
stage65_xnu_pmap_attr_dryrun_contract_status=0x65000001
stage65_xnu_pmap_multiwindow_dryrun_contract_status=0x65000001
stage65_xnu_pmap_transition_dryrun_contract_status=0x65000001
stage65_xnu_pexpert_hook_readiness_contract_status=0x65000001
stage65_xnu_iokit_platform_scaffold_contract_status=0x65000001
```

Hardware validation confirmed the retained pmap transition and pexpert readiness roll-ups:

```text
MI4IOS6_STAGE65_XNU stage65_xnu_pmap_transition_dryrun_contract_status=0x65000001
MI4IOS6_STAGE65_XNU stage65_xnu_pmap_transition_dryrun_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_pmap_transition_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_pexpert_hook_readiness_contract_status=0x65000001
MI4IOS6_STAGE65_XNU stage65_xnu_pexpert_hook_readiness_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_pexpert_hook_readiness_contract_failure_mask=0x00000000
MI4IOS6_STAGE65_XNU loader_xnu_pexpert_hook_readiness_contract_status_rollup=0x65000001
```

## IOKit/platform scaffold contract

Confirmed target-side IOKit/platform scaffold markers:

```text
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_platform_scaffold_contract_status=0x65000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_platform_scaffold_contract_required_mask=0x00ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_platform_scaffold_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_platform_scaffold_contract_failure_mask=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_platform_scaffold_contract_checksum=0x89af157f
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_reference_mask=0x00000007
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_runtime_blocked_mask=0x00000007
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_public_compile_count=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_public_link_count=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE65_XNU loader_xnu_iokit_platform_scaffold_contract_status=0x65000001
MI4IOS6_STAGE65_XNU loader_xnu_iokit_platform_scaffold_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE65_XNU loader_xnu_iokit_platform_scaffold_contract_failure_mask=0x00000000
MI4IOS6_STAGE65_XNU loader_xnu_iokit_platform_scaffold_contract_checksum=0x89af157f
MI4IOS6_STAGE65_XNU loader_xnu_iokit_platform_scaffold_contract_status_rollup=0x65000001
```

## IOKit service/driver match dry-run contract

Confirmed target-side IOKit match dry-run markers:

```text
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_contract_status=0x65000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_contract_required_mask=0x03ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_contract_satisfied_mask=0x03ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_contract_checksum=0xf9646bb0
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_provider_class_match=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_provider_path_match=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_category_match=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_compatible_match=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_driver_probe_score=0x00000650
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_match_count=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_attach_deferred=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_start_deferred=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_iokit_reference_mask=0x00000007
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_iokit_runtime_blocked_mask=0x00000007
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_public_compile_count=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_public_link_count=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_proposed_workspace_written=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_caches_changed=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_persistent_write_attempted=0x00000000
MI4IOS6_STAGE65_XNU loader_xnu_iokit_match_dryrun_contract_status=0x65000001
MI4IOS6_STAGE65_XNU loader_xnu_iokit_match_dryrun_contract_satisfied_mask=0x03ffffff
MI4IOS6_STAGE65_XNU loader_xnu_iokit_match_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE65_XNU loader_xnu_iokit_match_dryrun_contract_checksum=0xf9646bb0
MI4IOS6_STAGE65_XNU loader_xnu_iokit_match_dryrun_contract_status_rollup=0x65000001
```

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage65/build.sh
```

Final size:

```text
text=270228 data=0 bss=398740 dec=668968 hex=a3528
```

Hashes:

```text
03d34fbac9fa56ebf1185228be366526ddbce259a0d5ced740179f5282e0b4a2  out/stage65/stage65_fixture.macho
4ae534bb2db767049a7fd0de2c882d39445eb66cac18c9fb2e403981226f58e2  out/stage65/stage65.elf
3dd24ff32f96b87d117d0dd2b7bfd0327064b0725be86d12fb5898bdf55ebdd8  out/stage65/stage65.bin
f742d1144c46a4b181f5401065b438a03bffe8cb8de190c9179ea7b9e11b9f36  out/stage65/stage65.img
6e063c5738c66002075ea4b2451a6413b6d6e8d63de20b72fb1d5febff3ac740  out/stage65/stage65-qcdt.img
```

## Boot image parse facts

```text
out/stage65/stage65.img:
  page_size=2048
  kernel_size=270228 (0x41f94)
  dt_size=0
  kernel_sha256=3dd24ff32f96b87d117d0dd2b7bfd0327064b0725be86d12fb5898bdf55ebdd8

out/stage65/stage65-qcdt.img:
  page_size=2048
  kernel_size=270228 (0x41f94)
  dt_size=2521088 (0x267800)
  kernel_sha256=3dd24ff32f96b87d117d0dd2b7bfd0327064b0725be86d12fb5898bdf55ebdd8
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage65/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage65/stage65.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage65/stage65-qcdt.img
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage65/stage65.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage65/xnu-link/stage65-xnu-link.elf
```

Validation facts:

```text
stage65/boot_args.c command_line_len_with_nul=210 fits256=True
stage65/stage65_main.c command_line_len_with_nul=210 fits256=True
stage65/xnu_object_shims.c command_line_len_with_nul=179 fits256=True
stale_marker_violations=0
public_vm_pmap_iokit_command_violations=0
out/stage65 ignored
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage65/stage65-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage65-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage65-last_kmsg.txt
size=184957 bytes
stage65_marker_count=2632
stage65_xnu_marker_count=2604
```

Final success markers:

```text
MI4IOS6_STAGE65_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE65_XNU loader_status=0x65000001
MI4IOS6_STAGE65_XNU kernel_entry ok
MI4IOS6_STAGE65 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure` or `mmu high bootstrap selftest failed` in the successful run.

## Result

Stage65 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned local IOKit service/driver match dry-run over the Stage64 IOKit/platform scaffold, Apple-DT provider/driver nodes, MSM8974 platform-driver match facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper MSM8974 pexpert/platform implementation proof, a broader IOKit registry/service matching dry-run, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
