# Stage72 — XNU IOKit lifecycle/register-service readiness dry-run contract

Stage72 keeps the hardware-validated Stage71 IOKit attach/start readiness dry-run proof stable and adds a Stage-owned **IOKit lifecycle/register-service readiness dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute `IORegistryEntry`, `IOService`, `IODeviceTreeSupport`, attach/start, register-service, notification, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, or platform-driver runtime code, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage72 proves that the four selected local IODeviceTree topology/attach-start entries have deterministic local lifecycle, register-service, notification, ordering, and dependency facts while all decisions remain over Stage-owned Apple-DT data only.

## Safety model

Stage72 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, attach/start, register-service, notification, or platform-driver runtime execution.
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

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage72 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage72/xnu_object_shims.c
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

## What Stage72 adds

Stage72 retains the Stage64-through-Stage71 IOKit prerequisite contracts after renumbering to Stage72:

```text
stage72/xnu_iokit_platform_scaffold_contract.c
stage72/xnu_iokit_match_dryrun_contract.c
stage72/xnu_iokit_registry_service_dryrun_contract.c
stage72/xnu_iokit_provider_plane_dryrun_contract.c
stage72/xnu_iokit_catalog_property_dryrun_contract.c
stage72/xnu_iokit_property_inheritance_dryrun_contract.c
stage72/xnu_iokit_registry_topology_dryrun_contract.c
stage72/xnu_iokit_attach_start_readiness_dryrun_contract.c
```

It then adds the lifecycle/register-service readiness dry-run contract:

```text
stage72/xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.c
struct stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract
```

The lifecycle/register-service dry-run imports source facts from:

- the Stage72 attach/start readiness dry-run contract,
- the Stage72 registry-plane / IODeviceTree topology dry-run contract,
- the Stage72 property-inheritance / registry-entry dry-run contract,
- the Stage72 catalog/property/personality dry-run contract,
- the Stage72 provider-plane dry-run contract,
- the Stage72 broader IOKit registry/service dry-run contract,
- the Stage72 IOKit/platform scaffold contract,
- the Stage72 local IOKit match dry-run contract,
- the Stage72-renumbered pexpert hook readiness contract,
- the retained Stage72 pmap transition dry-run contract,
- the public compile graph/object subset/controlled link proof,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree keeps the Stage66/Stage67 provider/service/candidate nodes and the Stage68/Stage69/Stage70/Stage71 catalog/personality/registry-entry/topology/attach-start nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
/iokit-catalog-property-dryrun
/stage72-platform-personality
/stage72-interrupt-personality
/stage72-timer-personality
/stage72-cpu-personality
/stage72-rejected-personality
```

Stage72 does not add new root nodes. The root child count remains 19. It extends the four selected personality nodes from 36 to 53 properties with local lifecycle/register-service/notification facts:

```text
lifecycle-regsvc-dryrun=1
lifecycle-state-matched=1
lifecycle-provider-published=1
lifecycle-registry-linked=1
lifecycle-topology-linked=1
register-service-readiness=1
service-registered=1
register-service-runtime-exec=0
notification-ready=1
notification-runtime-exec=0
register-service-order=0..3
notification-order=0..3
register-provider-path="IODeviceTree:/" or "IODeviceTree:/stage72-platform-personality"
register-provider-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
register-provider-start-req=0 for platform, 1 for interrupt/timer/CPU
register-dependency-ready=1
lifecycle-provenance="stage72-attach-start-readiness"
```

The rejected personality remains unpublished, unlinked, unattached, unstarted, unregistered, and non-notifying with 52 properties:

```text
provider-plane-published=0
registry-entry-dryrun=0
property-inheritance-dryrun=1
registry-entry-ordinal=0xffffffff
registry-entry-attached=0
registry-topology-dryrun=0
registry-parent-path="IODeviceTree:/unlinked"
registry-entry-path="IODeviceTree:/stage72-rejected-personality"
registry-parent-ordinal=0xffffffff
registry-sibling-order=0xffffffff
registry-topology-provenance="stage72-rejected-unlinked"
registry-entry-linked=0
attach-start-readiness-dryrun=0
attach-readiness=0
start-readiness=0
attach-runtime-exec=0
start-runtime-exec=0
attach-provider-path="IODeviceTree:/unlinked"
start-provider-path="IODeviceTree:/unlinked"
attach-provider-ordinal=0xffffffff
start-provider-ordinal=0xffffffff
attach-order=0xffffffff
start-order=0xffffffff
provider-start-required=0
attach-start-provenance="stage72-rejected-unattached"
lifecycle-regsvc-dryrun=0
lifecycle-state-matched=0
lifecycle-provider-published=0
lifecycle-registry-linked=0
lifecycle-topology-linked=0
register-service-readiness=0
service-registered=0
register-service-runtime-exec=0
notification-ready=0
notification-runtime-exec=0
register-provider-path="IODeviceTree:/unlinked"
register-provider-ordinal=0xffffffff
register-provider-start-req=0
register-service-order=0xffffffff
notification-order=0xffffffff
register-dependency-ready=0
lifecycle-provenance="stage72-rejected-unregistered"
rejected-candidate=1
stage-owned-local-only=1
```

The property names `lifecycle-regsvc-dryrun` and `register-provider-start-req` are intentionally compact. The public XNU-style flattened Apple-DT property-name field is 32 bytes and the local builder stores at most 31 visible characters plus NUL; the originally planned longer names would be truncated and fail exact lookup.

The contract validates the selected lifecycle/register-service matrix for platform, interrupt, timer, and CPU entries:

```text
matched_mask=0x0000000f
provider_published_mask=0x0000000f
registry_linked_mask=0x0000000f
topology_linked_mask=0x0000000f
attach_readiness_mask=0x0000000f
start_readiness_mask=0x0000000f
register_service_readiness_mask=0x0000000f
service_registered_mask=0x0000000f
notification_ready_mask=0x0000000f
dependency_ready_mask=0x0000000f
lifecycle_provenance_match_mask=0x0000000f
register_runtime_blocked_mask=0x0000000f
notification_runtime_blocked_mask=0x0000000f
register_order_match_mask=0x0000000f
notification_order_match_mask=0x0000000f
provider_dependency_match_mask=0x0000000f
register_provider_start_required_mask=0x0000000e
register_provider_start_satisfied_mask=0x0000000e
selected_lifecycle_entry_mask=0x0000000f
rejected_lifecycle_mask=0x00000010
lifecycle_matrix=0x0000001f
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage72 does not add a new top-level loader satisfied bit. Instead, the lifecycle/register-service readiness dry-run contract is copied into loader preflight and logged as a roll-up. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x72000001
```

## Selected public XNU baselines

Stage72 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM/IOKit reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage72/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage72_xnu_compile_graph_status=0x72000001
stage72_xnu_compile_graph_required_mask=0x7fffffff
stage72_xnu_compile_graph_satisfied_mask=0x7fffffff
stage72_xnu_compile_graph_failure_mask=0x00000000
stage72_xnu_compile_graph_candidate_count=0x00000017
stage72_xnu_compile_graph_allowed_compile_count=0x00000005
stage72_xnu_compile_graph_allowed_link_count=0x00000005
stage72_xnu_compile_graph_forbidden_count=0x00000012
stage72_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage72_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage72_xnu_compile_graph_pmap_public_link_count=0x00000000
stage72_xnu_compile_graph_iokit_reference_mask=0x00000007
stage72_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage72_xnu_compile_graph_iokit_reference_count=0x00000003
stage72_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage72_xnu_compile_graph_iokit_public_link_count=0x00000000
stage72_xnu_compile_graph_iokit_reference_only=0x00000001
stage72_xnu_object_subset_status=0x72000001
stage72_xnu_object_count=0x00000006
stage72_xnu_object_duplicate_symbol_count=0x00000000
stage72_xnu_link_status=0x72000001
stage72_xnu_link_object_count=0x00000006
stage72_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage72/stage72.elf` and `arm-none-eabi-nm -u out/stage72/xnu-link/stage72-xnu-link.elf` both reported no undefined symbols.

## Retained prerequisite contracts

Stage72 retains all Stage56 through Stage71 XNU mapping/pmap/platform/IOKit prerequisites as fail-closed source facts:

```text
stage72_xnu_bootstrap_contract_status=0x72000001
stage72_xnu_pmap_bootstrap_contract_status=0x72000001
stage72_xnu_pmap_table_dryrun_contract_status=0x72000001
stage72_xnu_pmap_page_dryrun_contract_status=0x72000001
stage72_xnu_pmap_attr_dryrun_contract_status=0x72000001
stage72_xnu_pmap_multiwindow_dryrun_contract_status=0x72000001
stage72_xnu_pmap_transition_dryrun_contract_status=0x72000001
stage72_xnu_pexpert_hook_readiness_contract_status=0x72000001
stage72_xnu_iokit_platform_scaffold_contract_status=0x72000001
stage72_xnu_iokit_match_dryrun_contract_status=0x72000001
stage72_xnu_iokit_registry_service_dryrun_contract_status=0x72000001
stage72_xnu_iokit_provider_plane_dryrun_contract_status=0x72000001
stage72_xnu_iokit_catalog_property_dryrun_contract_status=0x72000001
stage72_xnu_iokit_property_inheritance_dryrun_contract_status=0x72000001
stage72_xnu_iokit_registry_topology_dryrun_contract_status=0x72000001
stage72_xnu_iokit_attach_start_readiness_dryrun_contract_status=0x72000001
```

## IOKit lifecycle/register-service readiness dry-run contract

Confirmed target-side lifecycle/register-service readiness markers:

```text
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_attach_start_readiness_dryrun_contract_status=0x72000001
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status=0x72000001
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_selected_lifecycle_entry_mask=0x0000000f
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_rejected_lifecycle_mask=0x00000010
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_lifecycle_matrix=0x0000001f
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_checksum=0xbba5bdb5
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_expected_checksum=0xbba5bdb5
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_no_register_service_runtime_exec=0x00000001
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_no_notification_runtime_exec=0x00000001
MI4IOS6_STAGE72_XNU loader_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status_rollup=0x72000001
```

Negative safety markers remained clear: public IOKit compile/link counts are zero, public IOKit runtime remains blocked, register-service and notification runtime execution counters remain blocked, attach/start runtime execution counters remain blocked through the retained source contract, no proposed workspace is written, no live pmap tables are installed, no pmap TTBR writes or pmap TLB invalidations occur, caches remain unchanged, and no persistent write is attempted.

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage72/build.sh
```

Final size:

```text
text=365056 data=0 bss=446936 dec=811992 hex=c63d8
```

Build output summary:

```text
kernel_size=365056 ramdisk_size=0 second_size=0 dt_size=2521088 page_size=2048
sha256=c9e72d0156cd37e22e5e7ebd7fb13e39309a930266f56903d4930480ab58c770
-rwxrwxr-x 1 lvyufeng lvyufeng  365056 Jun  9 18:43 ../out/stage72/stage72.bin
-rwxrwxr-x 1 lvyufeng lvyufeng  414468 Jun  9 18:43 ../out/stage72/stage72.elf
-rw-rw-r-- 1 lvyufeng lvyufeng  368640 Jun  9 18:43 ../out/stage72/stage72.img
-rw-rw-r-- 1 lvyufeng lvyufeng 2889728 Jun  9 18:43 ../out/stage72/stage72-qcdt.img
```

Hashes:

```text
ffeacea62fd9df7e5ea1e9b9604efe71c6876021515f8306f10b7856adf6298d  out/stage72/stage72_fixture.macho
49c4693e8b7e118b8cb392b81c95975cadd40691839a3ec96585f9f14d2df613  out/stage72/stage72.elf
a4e8ef684c7515578b067f6575ed591626a854f7e6d1d99761c7e9d1724431b0  out/stage72/stage72.bin
2981f434e870f6d008802c63bb1a5c78896c44a269774ed2b0e6be646a91ffb1  out/stage72/stage72.img
c9e72d0156cd37e22e5e7ebd7fb13e39309a930266f56903d4930480ab58c770  out/stage72/stage72-qcdt.img
```

## Boot image parse facts

```text
out/stage72/stage72-qcdt.img:
  magic=ANDROID!
  page_size=2048
  kernel_size=365056 (0x59200)
  dt_size=2521088 (0x267800)
  part=kernel offset=0x800 size=365056 sha256=a4e8ef684c7515578b067f6575ed591626a854f7e6d1d99761c7e9d1724431b0
  part=dt.img offset=0x5a000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage72/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage72/stage72.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage72/stage72-qcdt.img
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage72/stage72.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage72/xnu-link/stage72-xnu-link.elf
git diff --check
```

Validation facts:

```text
stage72/boot_args.c command_line_len_with_nul=231 fits256=True has_lfrs=True has_pexpert_hook_ready=True
stage72/stage72_main.c command_line_len_with_nul=231 fits256=True has_lfrs=True has_pexpert_hook_ready=True
stage72/xnu_object_shims.c command_line_len_with_nul=231 fits256=True has_lfrs=True has_pexpert_hook_ready=True
build_cmdline_1_len=1442 fits1536=True has_lifecycle=True has_no_register=True has_no_notify=True
build_cmdline_2_len=1442 fits1536=True has_lifecycle=True has_no_register=True has_no_notify=True
root_child_count_19=True
selected_prop_count_53_nodes=4
rejected_prop_count_52_nodes=1
dt_buffer_32768=True
apple_dt_prop_name_long_violations=0
stage72_source_stale_marker_violations=0
```

The fixed public ARM `CommandLine[256]` strings include `iokit-lfrs`, preserve `pexpert-hook-ready`, and fit at 231 bytes including NUL. The Android boot-image command line uses compact lifecycle/register-service markers (`xnu-iokit-lifecycle`, `lifecycle-regsvc-local`, `no-lifecycle-exec`, `no-register-exec`, and `no-notify-exec`) and fits the legacy `mkbootimg` limit at 1442 bytes.

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage72/stage72-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage72-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage72-last_kmsg.txt
size=229447 bytes
MI4IOS6_STAGE72_count=3112
MI4IOS6_STAGE72_XNU_count=3074
```

Final success markers:

```text
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_attach_start_readiness_dryrun_contract_status=0x72000001
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status=0x72000001
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_selected_lifecycle_entry_mask=0x0000000f
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_rejected_lifecycle_mask=0x00000010
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_lifecycle_matrix=0x0000001f
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_checksum=0xbba5bdb5
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_expected_checksum=0xbba5bdb5
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_no_register_service_runtime_exec=0x00000001
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_no_notification_runtime_exec=0x00000001
MI4IOS6_STAGE72_XNU loader_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status_rollup=0x72000001
MI4IOS6_STAGE72_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE72_XNU loader_status=0x72000001
MI4IOS6_STAGE72_XNU kernel_entry ok
MI4IOS6_STAGE72 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, `mmu high bootstrap selftest failed`, or `apple_dt walk length mismatch` in the successful run.

## Result

Stage72 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit lifecycle/register-service readiness dry-run over the retained attach/start readiness dry-run, retained registry-plane / IODeviceTree topology dry-run, retained property-inheritance / registry-entry dry-run, retained catalog/property/personality dry-run, retained provider-plane dry-run, retained IOKit/platform scaffold, retained local service/driver match dry-run, retained broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality/topology/attach-start nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit lifecycle/provider notification proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start/register-service/notification runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
