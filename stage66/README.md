# Stage66 — XNU broader IOKit registry/service dry-run contract

Stage66 keeps the Stage65 local IOKit service/driver match dry-run proof stable and adds a Stage-owned **broader IOKit registry/service matching dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute an MSM8974 platform driver, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage66 proves that the Stage-owned Apple flattened device tree now contains enough local registry/service facts to dry-run multiple IOKit-style service candidates while all matching decisions remain over Stage-owned data only.

## Safety model

Stage66 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No Stage-owned MSM8974 platform-driver execution.
- No IOKit service-match or registry/service runtime execution.
- No generated Mach-O fixture execution.
- No XNU `_start` / `arm_init` jump.
- No proposed physical load-address writes.
- No proposed TTE/pmap workspace writes.
- No live proposed XNU/pmap table install.
- No TTBR/TTBCR/DACR/SCTLR writes for proposed pmap install.
- No TLB invalidation for proposed pmap install.
- No cache policy change.
- No mutation of ignored `external/` public XNU checkouts.

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage66 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage66/xnu_object_shims.c
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

## What Stage66 adds

Stage66 retains the Stage64-renumbered IOKit/platform scaffold contract and the Stage65-renumbered one-provider/one-driver IOKit match dry-run contract:

```text
stage66/xnu_iokit_platform_scaffold_contract.c
struct stage66_xnu_iokit_platform_scaffold_contract

stage66/xnu_iokit_match_dryrun_contract.c
struct stage66_xnu_iokit_match_dryrun_contract
```

It then adds a fail-closed broader registry/service dry-run contract:

```text
stage66/xnu_iokit_registry_service_dryrun_contract.c
struct stage66_xnu_iokit_registry_service_dryrun_contract
```

The registry/service dry-run imports source facts from:

- the Stage66 IOKit/platform scaffold contract,
- the Stage66 local IOKit match dry-run contract,
- the Stage66-renumbered pexpert hook readiness contract,
- the public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- loader safety masks,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree keeps the Stage64/Stage65 provider and platform-driver nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
```

Stage66 expands the root from 9 to 13 children and adds four local-only top-level service/candidate nodes:

```text
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
```

`/msm8974-platform-driver` remains the platform match candidate:

```text
compatible=qcom,msm8974-cancro-stage66
IOClass=MSM8974PlatformExpert
IOProviderClass=IOPlatformExpertDevice
IOMatchCategory=Stage66LocalPlatformScaffold
IOProbeScore=0x00000650
GIC distributor base=0xf9000000
timer base=0xf9020000
timebase-frequency=19200000
cpu-count=4
stage-owned-local-only=1
```

The new service nodes add local-only dry-run facts:

```text
/msm8974-interrupt-service:
  compatible=qcom,msm8974-gic-stage66
  IOClass=MSM8974InterruptController
  IOMatchCategory=Stage66LocalInterruptService
  IOProbeScore=0x00000660
  irq-count=288
  interrupt-controller=1

/msm8974-timer-service:
  compatible=qcom,msm8974-timer-stage66
  IOClass=MSM8974Timer
  IOMatchCategory=Stage66LocalTimerService
  IOProbeScore=0x00000661
  frequency=19200000
  timer-ppi-mask=0x000c0000

/msm8974-cpu-service:
  compatible=qcom,msm8974-cpu-stage66
  IOClass=IOCPU
  IOMatchCategory=Stage66LocalCPUService
  IOProbeScore=0x00000662
  reg=[0,1,2,3]
  cpu-count=4
  timebase-frequency=19200000

/msm8974-rejected-driver:
  compatible=qcom,msm8974-rejected-stage66
  IOClass=Stage66RejectedDriver
  IOProviderClass=IOUnknownProvider
  IOMatchCategory=Stage66RejectedLocalService
  IOProbeScore=0x00000000
  rejected-candidate=1
```

The broader dry-run validates one local provider candidate, five local driver candidates, four selected service matches, and one rejected negative candidate:

```text
provider_candidate_count=1
driver_candidate_count=5
selected_driver_count=4
rejected_driver_count=1
dryrun_match_count=4
attach_deferred_count=4
start_deferred_count=4
attach_deferred=1
start_deferred=1
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage66 does not add a new top-level loader satisfied bit. Instead, the IOKit/platform scaffold, single-match dry-run, and broader registry/service dry-run contracts are copied into loader preflight and logged as roll-ups. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x66000001
```

## Selected public XNU baselines

Stage66 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage66/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage66_xnu_compile_graph_status=0x66000001
stage66_xnu_compile_graph_required_mask=0x7fffffff
stage66_xnu_compile_graph_satisfied_mask=0x7fffffff
stage66_xnu_compile_graph_failure_mask=0x00000000
stage66_xnu_compile_graph_candidate_count=0x00000017
stage66_xnu_compile_graph_allowed_compile_count=0x00000005
stage66_xnu_compile_graph_allowed_link_count=0x00000005
stage66_xnu_compile_graph_forbidden_count=0x00000012
stage66_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage66_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage66_xnu_compile_graph_pmap_public_link_count=0x00000000
stage66_xnu_compile_graph_iokit_reference_mask=0x00000007
stage66_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage66_xnu_compile_graph_iokit_reference_count=0x00000003
stage66_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage66_xnu_compile_graph_iokit_public_link_count=0x00000000
stage66_xnu_compile_graph_iokit_reference_only=0x00000001
stage66_xnu_compile_graph_platform_reference_count=0x00000009
stage66_xnu_compile_graph_blocked_runtime_count=0x00000004
stage66_xnu_object_subset_status=0x66000001
stage66_xnu_object_count=0x00000006
stage66_xnu_object_duplicate_symbol_count=0x00000000
stage66_xnu_link_status=0x66000001
stage66_xnu_link_object_count=0x00000006
stage66_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage66/stage66.elf` and `arm-none-eabi-nm -u out/stage66/xnu-link/stage66-xnu-link.elf` both reported no undefined symbols.

## Retained prerequisite contracts

Stage66 retains all Stage56 through Stage65 XNU mapping/pmap/platform/IOKit prerequisites as fail-closed source facts:

```text
stage66_xnu_bootstrap_contract_status=0x66000001
stage66_xnu_pmap_bootstrap_contract_status=0x66000001
stage66_xnu_pmap_table_dryrun_contract_status=0x66000001
stage66_xnu_pmap_page_dryrun_contract_status=0x66000001
stage66_xnu_pmap_attr_dryrun_contract_status=0x66000001
stage66_xnu_pmap_multiwindow_dryrun_contract_status=0x66000001
stage66_xnu_pmap_transition_dryrun_contract_status=0x66000001
stage66_xnu_pexpert_hook_readiness_contract_status=0x66000001
stage66_xnu_iokit_platform_scaffold_contract_status=0x66000001
stage66_xnu_iokit_match_dryrun_contract_status=0x66000001
```

Hardware validation confirmed the retained pmap transition and pexpert readiness roll-ups:

```text
MI4IOS6_STAGE66_XNU stage66_xnu_pmap_transition_dryrun_contract_status=0x66000001
MI4IOS6_STAGE66_XNU stage66_xnu_pmap_transition_dryrun_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_pmap_transition_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_pexpert_hook_readiness_contract_status=0x66000001
MI4IOS6_STAGE66_XNU stage66_xnu_pexpert_hook_readiness_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_pexpert_hook_readiness_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU loader_xnu_pexpert_hook_readiness_contract_status_rollup=0x66000001
```

## IOKit/platform scaffold contract

Confirmed target-side IOKit/platform scaffold markers:

```text
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_platform_scaffold_contract_status=0x66000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_platform_scaffold_contract_required_mask=0x00ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_platform_scaffold_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_platform_scaffold_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_platform_scaffold_contract_checksum=0x89af157f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_reference_mask=0x00000007
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_runtime_blocked_mask=0x00000007
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_public_compile_count=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_public_link_count=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE66_XNU loader_xnu_iokit_platform_scaffold_contract_status=0x66000001
MI4IOS6_STAGE66_XNU loader_xnu_iokit_platform_scaffold_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE66_XNU loader_xnu_iokit_platform_scaffold_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU loader_xnu_iokit_platform_scaffold_contract_checksum=0x89af157f
MI4IOS6_STAGE66_XNU loader_xnu_iokit_platform_scaffold_contract_status_rollup=0x66000001
```

## IOKit service/driver match dry-run contract

Confirmed target-side single service/driver match dry-run markers:

```text
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_dryrun_contract_status=0x66000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_dryrun_contract_required_mask=0x03ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_dryrun_contract_satisfied_mask=0x03ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_dryrun_contract_checksum=0xf9646bb0
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_provider_class_match=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_provider_path_match=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_category_match=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_compatible_match=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_driver_probe_score=0x00000650
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_dryrun_match_count=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_attach_deferred=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_start_deferred=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_iokit_reference_mask=0x00000007
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_iokit_runtime_blocked_mask=0x00000007
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_public_compile_count=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_public_link_count=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_proposed_workspace_written=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_caches_changed=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_persistent_write_attempted=0x00000000
MI4IOS6_STAGE66_XNU loader_xnu_iokit_match_dryrun_contract_status=0x66000001
MI4IOS6_STAGE66_XNU loader_xnu_iokit_match_dryrun_contract_satisfied_mask=0x03ffffff
MI4IOS6_STAGE66_XNU loader_xnu_iokit_match_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU loader_xnu_iokit_match_dryrun_contract_checksum=0xf9646bb0
MI4IOS6_STAGE66_XNU loader_xnu_iokit_match_dryrun_contract_status_rollup=0x66000001
```

## IOKit registry/service dry-run contract

Confirmed target-side broader registry/service dry-run markers:

```text
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_contract_status=0x66000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_contract_required_mask=0x07ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_contract_checksum=0x9dd7207f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_provider_class_match_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_category_match_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_compatible_match_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_probe_score_match_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_selected_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_rejected_mask=0x00000010
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_fact_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_selected_driver_count=0x00000004
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_rejected_driver_count=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_match_count=0x00000004
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_proposed_workspace_written=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_caches_changed=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_persistent_write_attempted=0x00000000
MI4IOS6_STAGE66_XNU loader_xnu_iokit_registry_service_dryrun_contract_status=0x66000001
MI4IOS6_STAGE66_XNU loader_xnu_iokit_registry_service_dryrun_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE66_XNU loader_xnu_iokit_registry_service_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU loader_xnu_iokit_registry_service_dryrun_contract_checksum=0x9dd7207f
MI4IOS6_STAGE66_XNU loader_xnu_iokit_registry_service_dryrun_contract_status_rollup=0x66000001
```

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage66/build.sh
```

Final size:

```text
text=281804 data=0 bss=403248 dec=685052 hex=a73fc
```

Hashes:

```text
599d1d7fb365aef15ff1cbcf036767b1b25f2782db970602c24c23f617ef2269  out/stage66/stage66_fixture.macho
d41622681d97ecafb23f8d3e408d76e1b4adb2dfc7721c6d27387aae426e17e3  out/stage66/stage66.elf
57a1842f71a5679dedef3d96093d0bfa5a0dacd5d59f4d90cf2e521667d84d25  out/stage66/stage66.bin
2d9bad388826612980a8957dc9b1e87bdef8d492817a1a5cfc4fe4651bce857d  out/stage66/stage66.img
7c082e8eb986deaa0115c3cc6b23627f1864a69b0391070995b9c79d5dff9f90  out/stage66/stage66-qcdt.img
```

## Boot image parse facts

```text
out/stage66/stage66.img:
  page_size=2048
  kernel_size=281804 (0x44ccc)
  dt_size=0
  kernel_sha256=57a1842f71a5679dedef3d96093d0bfa5a0dacd5d59f4d90cf2e521667d84d25

out/stage66/stage66-qcdt.img:
  page_size=2048
  kernel_size=281804 (0x44ccc)
  dt_size=2521088 (0x267800)
  kernel_sha256=57a1842f71a5679dedef3d96093d0bfa5a0dacd5d59f4d90cf2e521667d84d25
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage66/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage66/stage66.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage66/stage66-qcdt.img
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage66/stage66.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage66/xnu-link/stage66-xnu-link.elf
```

Validation facts:

```text
stage66/boot_args.c command_line_len_with_nul=232 fits256=True
stage66/stage66_main.c command_line_len_with_nul=232 fits256=True
stage66/xnu_object_shims.c command_line_len_with_nul=201 fits256=True
stage66_source_stale_marker_violations=0
public_vm_pmap_iokit_command_violations=0
out/stage66 ignored
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage66/stage66-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage66-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage66-last_kmsg.txt
size=190399 bytes
stage66_marker_count=2695
stage66_xnu_marker_count=2663
```

Final success markers:

```text
MI4IOS6_STAGE66_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE66_XNU loader_status=0x66000001
MI4IOS6_STAGE66_XNU kernel_entry ok
MI4IOS6_STAGE66_XNU Stage66 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE66 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure` or `mmu high bootstrap selftest failed` in the successful run.

## Result

Stage66 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned broader IOKit registry/service dry-run over the retained IOKit/platform scaffold, the Stage65 local service/driver match dry-run, Stage-owned Apple-DT provider/service/candidate nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit registry/provider plane proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
