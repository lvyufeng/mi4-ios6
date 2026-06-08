# Stage67 — XNU IOKit provider-plane publish/order dry-run contract

Stage67 keeps the Stage66 broader IOKit registry/service dry-run proof stable and adds a Stage-owned **IOKit provider-plane publish/order dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute a provider-plane runtime, not to execute an MSM8974 platform driver, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage67 proves that the local Stage-owned registry/service candidates can be projected into a provider-plane publication/dependency/order model while all decisions remain over Stage-owned data only.

## Safety model

Stage67 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit provider-plane runtime execution.
- No Stage-owned MSM8974 platform-driver execution.
- No IOKit service-match, registry/service, or provider-plane runtime execution.
- No generated Mach-O fixture execution.
- No XNU `_start` / `arm_init` jump.
- No proposed physical load-address writes.
- No proposed TTE/pmap workspace writes.
- No live proposed XNU/pmap table install.
- No TTBR/TTBCR/DACR/SCTLR writes for proposed pmap install.
- No TLB invalidation for proposed pmap install.
- No cache policy change.
- No mutation of ignored `external/` public XNU checkouts.

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage67 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage67/xnu_object_shims.c
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

## What Stage67 adds

Stage67 retains the Stage64-renumbered IOKit/platform scaffold contract, the Stage65-renumbered one-provider/one-driver IOKit match dry-run contract, and the Stage66-renumbered broader registry/service dry-run contract:

```text
stage67/xnu_iokit_platform_scaffold_contract.c
struct stage67_xnu_iokit_platform_scaffold_contract

stage67/xnu_iokit_match_dryrun_contract.c
struct stage67_xnu_iokit_match_dryrun_contract

stage67/xnu_iokit_registry_service_dryrun_contract.c
struct stage67_xnu_iokit_registry_service_dryrun_contract
```

It then adds the provider-plane dry-run contract:

```text
stage67/xnu_iokit_provider_plane_dryrun_contract.c
struct stage67_xnu_iokit_provider_plane_dryrun_contract
```

The provider-plane dry-run imports source facts from:

- the Stage67 broader IOKit registry/service dry-run contract,
- the Stage67 IOKit/platform scaffold contract,
- the Stage67 local IOKit match dry-run contract,
- the Stage67-renumbered pexpert hook readiness contract,
- the public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- loader safety masks,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree keeps the Stage66 provider/service/candidate nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
```

The provider-plane contract validates that the four selected Stage66 service candidates can be published below the local platform provider, in deterministic dry-run order, while the rejected candidate remains unpublished:

```text
root_publish_ordinal=0
platform_publish_ordinal=1
interrupt_publish_ordinal=2
timer_publish_ordinal=3
cpu_publish_ordinal=4
rejected_publish_ordinal=0xffffffff

provider_parent_match_mask=0x0000000f
provider_dependency_mask=0x0000000f
provider_publish_order_mask=0x0000000f
provider_published_service_mask=0x0000000f
provider_unpublished_service_mask=0x00000010
```

The dry-run records one provider candidate, five service candidates, four published services, one unpublished rejected candidate, and deferred attach/start for all published services:

```text
provider_candidate_count=1
service_candidate_count=5
published_service_count=4
unpublished_candidate_count=1
dryrun_publish_count=4
attach_deferred_count=4
start_deferred_count=4
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage67 does not add a new top-level loader satisfied bit. Instead, the IOKit/platform scaffold, single-match dry-run, broader registry/service dry-run, and provider-plane dry-run contracts are copied into loader preflight and logged as roll-ups. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x67000001
```

## Selected public XNU baselines

Stage67 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage67/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage67_xnu_compile_graph_status=0x67000001
stage67_xnu_compile_graph_required_mask=0x7fffffff
stage67_xnu_compile_graph_satisfied_mask=0x7fffffff
stage67_xnu_compile_graph_failure_mask=0x00000000
stage67_xnu_compile_graph_candidate_count=0x00000017
stage67_xnu_compile_graph_allowed_compile_count=0x00000005
stage67_xnu_compile_graph_allowed_link_count=0x00000005
stage67_xnu_compile_graph_forbidden_count=0x00000012
stage67_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage67_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage67_xnu_compile_graph_pmap_public_link_count=0x00000000
stage67_xnu_compile_graph_iokit_reference_mask=0x00000007
stage67_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage67_xnu_compile_graph_iokit_reference_count=0x00000003
stage67_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage67_xnu_compile_graph_iokit_public_link_count=0x00000000
stage67_xnu_compile_graph_iokit_reference_only=0x00000001
stage67_xnu_compile_graph_platform_reference_count=0x00000009
stage67_xnu_compile_graph_blocked_runtime_count=0x00000004
stage67_xnu_object_subset_status=0x67000001
stage67_xnu_object_count=0x00000006
stage67_xnu_object_duplicate_symbol_count=0x00000000
stage67_xnu_link_status=0x67000001
stage67_xnu_link_object_count=0x00000006
stage67_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage67/stage67.elf` and `arm-none-eabi-nm -u out/stage67/xnu-link/stage67-xnu-link.elf` both reported no undefined symbols.

## Retained prerequisite contracts

Stage67 retains all Stage56 through Stage66 XNU mapping/pmap/platform/IOKit prerequisites as fail-closed source facts:

```text
stage67_xnu_bootstrap_contract_status=0x67000001
stage67_xnu_pmap_bootstrap_contract_status=0x67000001
stage67_xnu_pmap_table_dryrun_contract_status=0x67000001
stage67_xnu_pmap_page_dryrun_contract_status=0x67000001
stage67_xnu_pmap_attr_dryrun_contract_status=0x67000001
stage67_xnu_pmap_multiwindow_dryrun_contract_status=0x67000001
stage67_xnu_pmap_transition_dryrun_contract_status=0x67000001
stage67_xnu_pexpert_hook_readiness_contract_status=0x67000001
stage67_xnu_iokit_platform_scaffold_contract_status=0x67000001
stage67_xnu_iokit_match_dryrun_contract_status=0x67000001
stage67_xnu_iokit_registry_service_dryrun_contract_status=0x67000001
```

Hardware validation confirmed the materialized Mach-O marker fix and retained source facts:

```text
MI4IOS6_STAGE67_XNU macho_staging_marker_mask=0x00000007
MI4IOS6_STAGE67_XNU macho_staging_status=0x67000001
MI4IOS6_STAGE67_XNU loader_materialized_status=0x67000001
MI4IOS6_STAGE67_XNU stage67_xnu_pexpert_hook_readiness_contract_status=0x67000001
MI4IOS6_STAGE67_XNU stage67_xnu_pexpert_hook_readiness_contract_failure_mask=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_pexpert_hook_boot_args_stage67_marker=0x00000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_registry_service_dryrun_contract_status=0x67000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_registry_service_dryrun_contract_failure_mask=0x00000000
```

## IOKit provider-plane dry-run contract

Confirmed target-side provider-plane markers:

```text
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_dryrun_contract_status=0x67000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_dryrun_contract_required_mask=0x07ffffff
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_dryrun_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_dryrun_contract_checksum=0xfc28394a
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_service_fact_mask=0x0000000f
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_parent_match_mask=0x0000000f
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_dependency_mask=0x0000000f
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_publish_order_mask=0x0000000f
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_published_service_mask=0x0000000f
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_unpublished_service_mask=0x00000010
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_provider_candidate_count=0x00000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_service_candidate_count=0x00000005
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_published_service_count=0x00000004
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_unpublished_candidate_count=0x00000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_dryrun_publish_count=0x00000004
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_attach_deferred_count=0x00000004
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_start_deferred_count=0x00000004
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_no_provider_runtime_exec=0x00000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_proposed_workspace_written=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_caches_changed=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_persistent_write_attempted=0x00000000
MI4IOS6_STAGE67_XNU loader_xnu_iokit_provider_plane_dryrun_contract_status=0x67000001
MI4IOS6_STAGE67_XNU loader_xnu_iokit_provider_plane_dryrun_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE67_XNU loader_xnu_iokit_provider_plane_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE67_XNU loader_xnu_iokit_provider_plane_dryrun_contract_checksum=0xfc28394a
MI4IOS6_STAGE67_XNU loader_xnu_iokit_provider_plane_dryrun_contract_status_rollup=0x67000001
```

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage67/build.sh
```

Final size:

```text
text=289376 data=0 bss=403640 dec=693016 hex=a9318
```

Hashes:

```text
23f30704df3005a528172bbcc509184ddbea105c9c8da21fe700490c0f74ecb8  out/stage67/stage67_fixture.macho
e6a90dcf366453e17fdbe61e1e27248fdd85f6f70013422c2913be71e00621c5  out/stage67/stage67.elf
7f01518f5933c26e7cde646bbcdbdc94f3546e43c41f7a075e5e5699ef91a42f  out/stage67/stage67.bin
293bf355a4a9e4de88419445ed86db7164ab9f47f13bf1b99c3aae087b89c03a  out/stage67/stage67.img
9fc9c3e0eaee02aaa22e21e199c8194a8bb39850dd0cd7c7e174f29a8171a64a  out/stage67/stage67-qcdt.img
```

## Boot image parse facts

```text
out/stage67/stage67.img:
  page_size=2048
  kernel_size=289376 (0x46a60)
  dt_size=0
  kernel_sha256=7f01518f5933c26e7cde646bbcdbdc94f3546e43c41f7a075e5e5699ef91a42f

out/stage67/stage67-qcdt.img:
  page_size=2048
  kernel_size=289376 (0x46a60)
  dt_size=2521088 (0x267800)
  kernel_sha256=7f01518f5933c26e7cde646bbcdbdc94f3546e43c41f7a075e5e5699ef91a42f
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage67/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage67/stage67.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage67/stage67-qcdt.img
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage67/stage67.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage67/xnu-link/stage67-xnu-link.elf
```

Validation facts:

```text
stage67/boot_args.c command_line_len_with_nul=237 fits256=True
stage67/stage67_main.c command_line_len_with_nul=246 fits256=True
stage67/xnu_object_shims.c command_line_len_with_nul=237 fits256=True
stage67_source_stale_marker_violations=0
out/stage67 ignored
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage67/stage67-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage67-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage67-last_kmsg.txt
size=195330 bytes
stage67_marker_count=2751
stage67_xnu_marker_count=2719
```

Final success markers:

```text
MI4IOS6_STAGE67_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE67_XNU loader_status=0x67000001
MI4IOS6_STAGE67_XNU kernel_entry ok
MI4IOS6_STAGE67_XNU Stage67 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE67 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, or `mmu high bootstrap selftest failed` in the successful run.

## Result

Stage67 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned provider-plane publish/order/dependency dry-run over the retained IOKit/platform scaffold, the retained local service/driver match dry-run, the retained broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit registry/catalog/property-plane proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit provider-plane runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
