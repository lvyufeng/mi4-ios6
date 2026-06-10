# Stage73 — XNU IOKit provider-notification / interest / delivery-readiness dry-run contract

Stage73 keeps the hardware-validated Stage72 IOKit lifecycle/register-service readiness dry-run proof stable and adds a Stage-owned **IOKit provider-notification / interest / delivery-readiness dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute `IORegistryEntry`, `IOService`, `IODeviceTreeSupport`, attach/start, register-service, provider-notification, interest, delivery, callback, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, or platform-driver runtime code, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage73 deepens Stage72's generic local `notification-ready` fact into deterministic provider-notification, interest-registration, dependency, expected/delivered notification-mask, and ordering facts over Stage-owned Apple-DT data only.

## Safety model

Stage73 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, attach/start, register-service, notification, interest, delivery, callback, or platform-driver runtime execution.
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

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage73 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage73/xnu_object_shims.c
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

## What Stage73 adds

Stage73 retains the Stage64-through-Stage72 IOKit prerequisite contracts after renumbering to Stage73:

```text
stage73/xnu_iokit_platform_scaffold_contract.c
stage73/xnu_iokit_match_dryrun_contract.c
stage73/xnu_iokit_registry_service_dryrun_contract.c
stage73/xnu_iokit_provider_plane_dryrun_contract.c
stage73/xnu_iokit_catalog_property_dryrun_contract.c
stage73/xnu_iokit_property_inheritance_dryrun_contract.c
stage73/xnu_iokit_registry_topology_dryrun_contract.c
stage73/xnu_iokit_attach_start_readiness_dryrun_contract.c
stage73/xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.c
```

It then adds the provider-notification / interest / delivery-readiness dry-run contract:

```text
stage73/xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.c
struct stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract
```

The new contract imports source facts from:

- the Stage73 lifecycle/register-service readiness dry-run contract,
- the Stage73 attach/start readiness dry-run contract,
- the Stage73 registry-plane / IODeviceTree topology dry-run contract,
- the Stage73 property-inheritance / registry-entry dry-run contract,
- the Stage73 catalog/property/personality dry-run contract,
- the Stage73 provider-plane dry-run contract,
- the Stage73 broader IOKit registry/service dry-run contract,
- the Stage73 IOKit/platform scaffold contract,
- the Stage73 local IOKit match dry-run contract,
- the Stage73-renumbered pexpert hook readiness contract,
- the retained Stage73 pmap transition dry-run contract,
- the public compile graph/object subset/controlled link proof,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree keeps the Stage66/Stage67 provider/service/candidate nodes and the Stage68/Stage69/Stage70/Stage71/Stage72 catalog/personality/registry-entry/topology/attach-start/lifecycle nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
/iokit-catalog-property-dryrun
/stage73-platform-personality
/stage73-interrupt-personality
/stage73-timer-personality
/stage73-cpu-personality
/stage73-rejected-personality
```

Stage73 does not add new root nodes. The root child count remains 19. It extends the four selected personality nodes from 53 to 68 properties with local provider-notification / interest / delivery-readiness facts:

```text
prov-notify-dryrun=1
prov-notify-ready=1
interest-ready=1
interest-runtime-exec=0
delivery-ready=1
delivery-runtime-exec=0
interest-provider-path="IODeviceTree:/" or "IODeviceTree:/stage73-platform-personality"
interest-provider-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
interest-type-mask=<platform/interrupt/timer/CPU service bit>
expected-notify-mask=<same per-service bit>
delivered-notify-mask=<same per-service bit>
interest-order=0..3
delivery-order=0..3
notify-dependency-ready=1
notify-provenance="stage73-lifecycle-regsvc"
```

The rejected personality remains unpublished, unlinked, unattached, unstarted, unregistered, non-notifying, no-interest, and undelivered with 67 properties:

```text
lifecycle-regsvc-dryrun=0
service-registered=0
notification-ready=0
prov-notify-dryrun=0
prov-notify-ready=0
interest-ready=0
interest-runtime-exec=0
delivery-ready=0
delivery-runtime-exec=0
interest-provider-path="IODeviceTree:/unlinked"
interest-provider-ordinal=0xffffffff
interest-type-mask=0
expected-notify-mask=0
delivered-notify-mask=0
interest-order=0xffffffff
delivery-order=0xffffffff
notify-dependency-ready=0
notify-provenance="stage73-rejected-undelivered"
rejected-candidate=1
stage-owned-local-only=1
```

All added Apple-DT property names are 31 visible characters or shorter. The public XNU-style flattened Apple-DT property-name field is 32 bytes and the local builder stores at most 31 visible characters plus NUL.

The provider-notification / interest / delivery-readiness contract validates the selected matrix for platform, interrupt, timer, and CPU entries:

```text
provider_notification_ready_mask=0x0000000f
interest_ready_mask=0x0000000f
delivery_ready_mask=0x0000000f
interest_provider_path_match_mask=0x0000000f
interest_provider_ordinal_match_mask=0x0000000f
interest_type_match_mask=0x0000000f
expected_notify_mask_match_mask=0x0000000f
delivered_notify_mask_match_mask=0x0000000f
notify_dependency_ready_mask=0x0000000f
interest_runtime_blocked_mask=0x0000000f
delivery_runtime_blocked_mask=0x0000000f
interest_order_match_mask=0x0000000f
delivery_order_match_mask=0x0000000f
notify_provenance_match_mask=0x0000000f
selected_provider_notification_entry_mask=0x0000000f
rejected_provider_notification_mask=0x00000010
provider_notification_matrix=0x0000001f
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage73 does not add a new top-level loader satisfied bit. Instead, the provider-notification / interest / delivery-readiness dry-run contract is copied into loader preflight and logged as a roll-up. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x73000001
```

## Selected public XNU baselines

Stage73 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM/IOKit reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage73/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage73_xnu_compile_graph_status=0x73000001
stage73_xnu_compile_graph_required_mask=0x7fffffff
stage73_xnu_compile_graph_satisfied_mask=0x7fffffff
stage73_xnu_compile_graph_failure_mask=0x00000000
stage73_xnu_compile_graph_candidate_count=0x00000017
stage73_xnu_compile_graph_allowed_compile_count=0x00000005
stage73_xnu_compile_graph_allowed_link_count=0x00000005
stage73_xnu_compile_graph_forbidden_count=0x00000012
stage73_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage73_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage73_xnu_compile_graph_pmap_public_link_count=0x00000000
stage73_xnu_compile_graph_iokit_reference_mask=0x00000007
stage73_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage73_xnu_compile_graph_iokit_reference_count=0x00000003
stage73_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage73_xnu_compile_graph_iokit_public_link_count=0x00000000
stage73_xnu_compile_graph_iokit_reference_only=0x00000001
stage73_xnu_object_subset_status=0x73000001
stage73_xnu_object_count=0x00000006
stage73_xnu_object_duplicate_symbol_count=0x00000000
stage73_xnu_link_status=0x73000001
stage73_xnu_link_object_count=0x00000006
stage73_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage73/stage73.elf` and `arm-none-eabi-nm -u out/stage73/xnu-link/stage73-xnu-link.elf` both reported no undefined symbols.

## Retained prerequisite contracts

Stage73 retains all Stage56 through Stage72 XNU mapping/pmap/platform/IOKit prerequisites as fail-closed source facts:

```text
stage73_xnu_bootstrap_contract_status=0x73000001
stage73_xnu_pmap_bootstrap_contract_status=0x73000001
stage73_xnu_pmap_table_dryrun_contract_status=0x73000001
stage73_xnu_pmap_page_dryrun_contract_status=0x73000001
stage73_xnu_pmap_attr_dryrun_contract_status=0x73000001
stage73_xnu_pmap_multiwindow_dryrun_contract_status=0x73000001
stage73_xnu_pmap_transition_dryrun_contract_status=0x73000001
stage73_xnu_pexpert_hook_readiness_contract_status=0x73000001
stage73_xnu_iokit_platform_scaffold_contract_status=0x73000001
stage73_xnu_iokit_match_dryrun_contract_status=0x73000001
stage73_xnu_iokit_registry_service_dryrun_contract_status=0x73000001
stage73_xnu_iokit_provider_plane_dryrun_contract_status=0x73000001
stage73_xnu_iokit_catalog_property_dryrun_contract_status=0x73000001
stage73_xnu_iokit_property_inheritance_dryrun_contract_status=0x73000001
stage73_xnu_iokit_registry_topology_dryrun_contract_status=0x73000001
stage73_xnu_iokit_attach_start_readiness_dryrun_contract_status=0x73000001
stage73_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status=0x73000001
```

## IOKit provider-notification / interest / delivery-readiness contract

Confirmed target-side provider-notification / interest / delivery-readiness markers:

```text
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status=0x73000001
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status=0x73000001
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_selected_provider_notification_entry_mask=0x0000000f
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_rejected_provider_notification_mask=0x00000010
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_matrix=0x0000001f
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_checksum=0xebebe9ff
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_expected_checksum=0xebebe9ff
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_no_interest_runtime_exec=0x00000001
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_no_delivery_runtime_exec=0x00000001
MI4IOS6_STAGE73_XNU loader_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status_rollup=0x73000001
```

Negative safety markers remained clear: public IOKit compile/link counts are zero, public IOKit runtime remains blocked, interest runtime execution remains blocked, delivery/callback runtime execution remains blocked, register-service and notification runtime execution counters remain blocked through the retained source contract, attach/start runtime execution counters remain blocked through the retained source contract, no proposed workspace is written, no live pmap tables are installed, no pmap TTBR writes or pmap TLB invalidations occur, caches remain unchanged, and no persistent write is attempted.

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage73/build.sh
```

Final size:

```text
text=382092 data=0 bss=447516 dec=829608 hex=ca8a8
```

Build output summary:

```text
kernel_size=382092 ramdisk_size=0 second_size=0 dt_size=2521088 page_size=2048
```

Hashes:

```text
9f72526cb89f8d073a3b1cbbdc23f17537181431e10e57e3f775b99d3d41ab04  out/stage73/stage73_fixture.macho
34e7a0ae3775f43d979b4c9e70ac9d70b79e3f3c61262921e7279b2343cf13c5  out/stage73/stage73.elf
af7e0dadcf8bc925f91ddb6e86d4d5e780887f1e09f2d29889139d8260701e8e  out/stage73/stage73.bin
0e67aacf1265847d32b52a3a427d043c9368c32c182ee143b858a2237d60f3c8  out/stage73/stage73.img
bcec03b0936a1c58a8d129eeeef8a9545ad56b4bdf73e148c55baa6c7298615b  out/stage73/stage73-qcdt.img
```

## Boot image parse facts

```text
out/stage73/stage73-qcdt.img:
  magic=ANDROID!
  page_size=2048
  kernel_size=382092 (0x5d48c)
  dt_size=2521088 (0x267800)
  part=kernel offset=0x800 size=382092 sha256=af7e0dadcf8bc925f91ddb6e86d4d5e780887f1e09f2d29889139d8260701e8e
  part=dt.img offset=0x5e000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage73/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage73/stage73.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage73/stage73-qcdt.img
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage73/stage73.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage73/xnu-link/stage73-xnu-link.elf
git diff --check
```

Validation facts:

```text
stage73/boot_args.c command_line_len_with_nul=245 fits256=True has_pnotify=True has_lfrs=True
stage73/stage73_main.c command_line_len_with_nul=245 fits256=True has_pnotify=True has_lfrs=True
stage73/xnu_object_shims.c command_line_len_with_nul=245 fits256=True has_pnotify=True has_lfrs=True
out/stage73/stage73.img build_cmdline_len=1508 fits1536=True has_pnotify=True has_no_interest=True has_no_delivery=True
out/stage73/stage73-qcdt.img build_cmdline_len=1508 fits1536=True has_pnotify=True has_no_interest=True has_no_delivery=True
root_child_count_19=True
selected_prop_count_68_nodes=4
rejected_prop_count_67_nodes=1
dt_buffer_32768=True
apple_dt_prop_name_long_violations=0
stage73_source_stale_marker_violations=0
```

The fixed public ARM `CommandLine[256]` strings include `iokit-lfrs` and `iokit-pnotify` and fit at 245 bytes including NUL. The Android boot-image command line uses compact provider-notification markers (`xnu-iokit-pnotify`, `pnotify-local`, `no-interest-exec`, and `no-delivery-exec`) and fits the legacy `mkbootimg` limit at 1508 bytes.

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage73/stage73-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage73-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage73-last_kmsg.txt
size=235817 bytes
stage73_marker_count=3177
stage73_xnu_marker_count=3139
```

Final success markers:

```text
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status=0x73000001
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_selected_provider_notification_entry_mask=0x0000000f
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_rejected_provider_notification_mask=0x00000010
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_matrix=0x0000001f
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_checksum=0xebebe9ff
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_expected_checksum=0xebebe9ff
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_no_interest_runtime_exec=0x00000001
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_no_delivery_runtime_exec=0x00000001
MI4IOS6_STAGE73_XNU loader_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status_rollup=0x73000001
MI4IOS6_STAGE73_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE73_XNU loader_status=0x73000001
MI4IOS6_STAGE73_XNU kernel_entry ok
MI4IOS6_STAGE73 kernel_entry returned success
```

No target-side failure markers were observed:

```text
kernel_entry returned failure: 0
loader preflight failed: 0
```

## Result

Stage73 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit provider-notification / interest / delivery-readiness dry-run over the retained lifecycle/register-service readiness dry-run, retained attach/start readiness dry-run, retained registry-plane / IODeviceTree topology dry-run, retained property-inheritance / registry-entry dry-run, retained catalog/property/personality dry-run, retained provider-plane dry-run, retained IOKit/platform scaffold, retained local service/driver match dry-run, retained broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality/topology/attach-start/lifecycle nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit provider-callback/client-notification proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start/register-service/notification/interest/delivery runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
