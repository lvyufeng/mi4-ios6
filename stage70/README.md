# Stage70 — XNU IOKit registry-plane / IODeviceTree topology dry-run contract

Stage70 keeps the hardware-validated Stage69 IOKit property-inheritance / registry-entry dry-run proof stable and adds a Stage-owned **IOKit registry-plane / IODeviceTree topology dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute an `IORegistryEntry`/`IOService`/`IODeviceTree` runtime, not to execute attach/start, not to execute an MSM8974 platform driver, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage70 proves that the four selected local registry entries can be projected into deterministic local IODeviceTree topology facts while all decisions remain over Stage-owned Apple-DT data only.

## Safety model

Stage70 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, attach/start, or platform-driver runtime execution.
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

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage70 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage70/xnu_object_shims.c
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

## What Stage70 adds

Stage70 retains the Stage64-through-Stage69 IOKit prerequisite contracts after renumbering to Stage70:

```text
stage70/xnu_iokit_platform_scaffold_contract.c
struct stage70_xnu_iokit_platform_scaffold_contract

stage70/xnu_iokit_match_dryrun_contract.c
struct stage70_xnu_iokit_match_dryrun_contract

stage70/xnu_iokit_registry_service_dryrun_contract.c
struct stage70_xnu_iokit_registry_service_dryrun_contract

stage70/xnu_iokit_provider_plane_dryrun_contract.c
struct stage70_xnu_iokit_provider_plane_dryrun_contract

stage70/xnu_iokit_catalog_property_dryrun_contract.c
struct stage70_xnu_iokit_catalog_property_dryrun_contract

stage70/xnu_iokit_property_inheritance_dryrun_contract.c
struct stage70_xnu_iokit_property_inheritance_dryrun_contract
```

It then adds the registry-plane / IODeviceTree topology dry-run contract:

```text
stage70/xnu_iokit_registry_topology_dryrun_contract.c
struct stage70_xnu_iokit_registry_topology_dryrun_contract
```

The topology dry-run imports source facts from:

- the Stage70 property-inheritance / registry-entry dry-run contract,
- the Stage70 catalog/property/personality dry-run contract,
- the Stage70 provider-plane dry-run contract,
- the Stage70 broader IOKit registry/service dry-run contract,
- the Stage70 IOKit/platform scaffold contract,
- the Stage70 local IOKit match dry-run contract,
- the Stage70-renumbered pexpert hook readiness contract,
- the retained Stage70 pmap transition dry-run contract,
- the public compile graph/object subset/controlled link proof,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree keeps the Stage66/Stage67 provider/service/candidate nodes and the Stage68/Stage69 catalog/personality/registry-entry nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
/iokit-catalog-property-dryrun
/stage70-platform-personality
/stage70-interrupt-personality
/stage70-timer-personality
/stage70-cpu-personality
/stage70-rejected-personality
```

Stage70 does not add new root nodes. The root child count remains 19. It extends the four selected personality nodes from 17 to 23 properties with local IODeviceTree topology projection facts:

```text
registry-topology-dryrun=1
registry-parent-path="IODeviceTree:/" or "IODeviceTree:/stage70-platform-personality"
registry-entry-path="IODeviceTree:/stage70-*-personality"
registry-parent-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
registry-sibling-order=0..3
registry-topology-provenance="stage70-property-inheritance"
```

The rejected personality is explicitly kept unpublished, unattached, and unlinked:

```text
provider-plane-published=0
registry-entry-dryrun=0
property-inheritance-dryrun=1
registry-entry-ordinal=0xffffffff
registry-entry-attached=0
registry-topology-dryrun=0
registry-parent-path="IODeviceTree:/unlinked"
registry-entry-path="IODeviceTree:/stage70-rejected-personality"
registry-parent-ordinal=0xffffffff
registry-sibling-order=0xffffffff
registry-topology-provenance="stage70-rejected-unlinked"
registry-entry-linked=0
rejected-candidate=1
stage-owned-local-only=1
```

The contract validates the selected topology matrix for platform, interrupt, timer, and CPU entries:

```text
registry-topology-dryrun
registry-plane="IODeviceTree"
registry-parent-path
registry-entry-path
registry-parent-ordinal
registry-sibling-order
registry-topology-provenance
selected_topology_entry_mask
```

It also validates the rejected-unlinked behavior:

```text
rejected_unlinked_mask=0x00000010
registry-entry-linked=0
registry-entry-dryrun=0
registry-entry-attached=0
provider-plane-published=0
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage70 does not add a new top-level loader satisfied bit. Instead, the registry-plane / IODeviceTree topology dry-run contract is copied into loader preflight and logged as a roll-up. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x70000001
```

## Selected public XNU baselines

Stage70 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage70/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage70_xnu_compile_graph_status=0x70000001
stage70_xnu_compile_graph_required_mask=0x7fffffff
stage70_xnu_compile_graph_satisfied_mask=0x7fffffff
stage70_xnu_compile_graph_failure_mask=0x00000000
stage70_xnu_compile_graph_candidate_count=0x00000017
stage70_xnu_compile_graph_allowed_compile_count=0x00000005
stage70_xnu_compile_graph_allowed_link_count=0x00000005
stage70_xnu_compile_graph_forbidden_count=0x00000012
stage70_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage70_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage70_xnu_compile_graph_pmap_public_link_count=0x00000000
stage70_xnu_compile_graph_iokit_reference_mask=0x00000007
stage70_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage70_xnu_compile_graph_iokit_reference_count=0x00000003
stage70_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage70_xnu_compile_graph_iokit_public_link_count=0x00000000
stage70_xnu_compile_graph_iokit_reference_only=0x00000001
stage70_xnu_object_subset_status=0x70000001
stage70_xnu_object_count=0x00000006
stage70_xnu_object_duplicate_symbol_count=0x00000000
stage70_xnu_link_status=0x70000001
stage70_xnu_link_object_count=0x00000006
stage70_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage70/stage70.elf` and `arm-none-eabi-nm -u out/stage70/xnu-link/stage70-xnu-link.elf` both reported no undefined symbols.

## Retained prerequisite contracts

Stage70 retains all Stage56 through Stage69 XNU mapping/pmap/platform/IOKit prerequisites as fail-closed source facts:

```text
stage70_xnu_bootstrap_contract_status=0x70000001
stage70_xnu_pmap_bootstrap_contract_status=0x70000001
stage70_xnu_pmap_table_dryrun_contract_status=0x70000001
stage70_xnu_pmap_page_dryrun_contract_status=0x70000001
stage70_xnu_pmap_attr_dryrun_contract_status=0x70000001
stage70_xnu_pmap_multiwindow_dryrun_contract_status=0x70000001
stage70_xnu_pmap_transition_dryrun_contract_status=0x70000001
stage70_xnu_pexpert_hook_readiness_contract_status=0x70000001
stage70_xnu_iokit_platform_scaffold_contract_status=0x70000001
stage70_xnu_iokit_match_dryrun_contract_status=0x70000001
stage70_xnu_iokit_registry_service_dryrun_contract_status=0x70000001
stage70_xnu_iokit_provider_plane_dryrun_contract_status=0x70000001
stage70_xnu_iokit_catalog_property_dryrun_contract_status=0x70000001
stage70_xnu_iokit_property_inheritance_dryrun_contract_status=0x70000001
```

## IOKit registry-plane / IODeviceTree topology dry-run contract

Confirmed target-side registry topology markers:

```text
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_dryrun_contract_status=0x70000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_dryrun_contract_required_mask=0x7fffffff
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_dryrun_contract_checksum=0xf8001093
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_topology_marker_mask=0x0000000f
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_registry_plane_match_mask=0x0000000f
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_parent_path_match_mask=0x0000000f
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_entry_path_match_mask=0x0000000f
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_parent_ordinal_match_mask=0x0000000f
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_sibling_order_match_mask=0x0000000f
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_provenance_match_mask=0x0000000f
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_selected_topology_entry_mask=0x0000000f
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_rejected_unlinked_mask=0x00000010
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_topology_matrix=0x0000001f
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_path_checksum=0x494f4454
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_checksum=0xb6b0bbbb
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_expected_checksum=0xb6b0bbbb
MI4IOS6_STAGE70_XNU loader_xnu_iokit_registry_topology_dryrun_contract_status_rollup=0x70000001
```

Negative safety markers remained clear:

```text
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_public_compile_count=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_public_link_count=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_registry_entry_runtime_exec=0x00000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_registry_topology_runtime_exec=0x00000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_property_runtime_exec=0x00000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_attach_runtime_exec=0x00000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_start_runtime_exec=0x00000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_proposed_workspace_written=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_caches_changed=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_persistent_write_attempted=0x00000000
```

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage70/build.sh
```

Final size:

```text
text=329100 data=0 bss=429532 dec=758632 hex=b9368
```

Hashes:

```text
d5b3892532f4855d903197f70dfb14b8b92bc67f346e5e04b70d60b998a1b5bb  out/stage70/stage70_fixture.macho
39b8fa91b65a2e468ce7affc626270cf4f3c3afe4374157141845f0d2ff478f8  out/stage70/stage70.elf
6057d3587fc5fe72a7e06d89d77fc4f8eedbadd1abc02191abbd2aa275a315c2  out/stage70/stage70.bin
a95538406e16c37b203bc187f907ebf41ce5f85e98c5875c58d99395c718e2b2  out/stage70/stage70.img
cb0585e36a68ad5400c716ecaeacaa3bb79860a216f9e6f5deb04fba6b81c998  out/stage70/stage70-qcdt.img
```

## Boot image parse facts

```text
out/stage70/stage70.img:
  page_size=2048
  kernel_size=329100 (0x5058c)
  dt_size=0

out/stage70/stage70-qcdt.img:
  page_size=2048
  kernel_size=329100 (0x5058c)
  dt_size=2521088 (0x267800)
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage70/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage70/stage70.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage70/stage70-qcdt.img
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage70/stage70.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage70/xnu-link/stage70-xnu-link.elf
```

Validation facts:

```text
stage70/boot_args.c command_line_len_with_nul=253 fits256=True has_regtop=True has_pmap_ref=True
stage70/stage70_main.c command_line_len_with_nul=253 fits256=True has_regtop=True has_pmap_ref=True
stage70/xnu_object_shims.c command_line_len_with_nul=253 fits256=True has_regtop=True has_pmap_ref=True
build_cmdline_len=1365 fits1536=True has_regtop=True
root_child_count_19=True
selected_prop_count_23=True
rejected_prop_count_22=True
has_registry_topology_dt_bit=True
stage70_source_stale_marker_violations=0
```

The fixed public ARM `CommandLine[256]` strings include `iokit-regtop-dryrun` and fit at 253 bytes including NUL. The Android boot-image command line uses shortened registry-topology markers (`xnu-iokit-regtop`, `iokit-regtop-local`, and `no-regtop-exec`) and fits the legacy limit at 1365 bytes.

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage70/stage70-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage70-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage70-last_kmsg.txt
size=215910 bytes
MI4IOS6_STAGE70_count=2977
MI4IOS6_STAGE70_XNU_count=2939
```

Final success markers:

```text
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_dryrun_contract_status=0x70000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_selected_topology_entry_mask=0x0000000f
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_rejected_unlinked_mask=0x00000010
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_checksum=0xb6b0bbbb
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_expected_checksum=0xb6b0bbbb
MI4IOS6_STAGE70_XNU loader_xnu_iokit_registry_topology_dryrun_contract_status_rollup=0x70000001
MI4IOS6_STAGE70_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE70_XNU loader_status=0x70000001
MI4IOS6_STAGE70_XNU kernel_entry ok
MI4IOS6_STAGE70 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, or `mmu high bootstrap selftest failed` in the successful run.

## Result

Stage70 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit registry-plane / IODeviceTree topology dry-run over the retained property-inheritance / registry-entry dry-run, retained catalog/property/personality dry-run, retained provider-plane dry-run, retained IOKit/platform scaffold, retained local service/driver match dry-run, retained broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit attach/start readiness proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
