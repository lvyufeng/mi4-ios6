# Stage71 — XNU IOKit attach/start readiness dry-run contract

Stage71 keeps the hardware-validated Stage70 IOKit registry-plane / IODeviceTree topology dry-run proof stable and adds a Stage-owned **IOKit attach/start readiness dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute `IORegistryEntry`, `IOService`, `IODeviceTreeSupport`, attach/start, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, or platform-driver runtime code, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage71 proves that the four selected local IODeviceTree topology entries have deterministic local attach/start readiness facts while all decisions remain over Stage-owned Apple-DT data only.

## Safety model

Stage71 preserves the no-runtime safety envelope:

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

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage71 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage71/xnu_object_shims.c
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

## What Stage71 adds

Stage71 retains the Stage64-through-Stage70 IOKit prerequisite contracts after renumbering to Stage71:

```text
stage71/xnu_iokit_platform_scaffold_contract.c
stage71/xnu_iokit_match_dryrun_contract.c
stage71/xnu_iokit_registry_service_dryrun_contract.c
stage71/xnu_iokit_provider_plane_dryrun_contract.c
stage71/xnu_iokit_catalog_property_dryrun_contract.c
stage71/xnu_iokit_property_inheritance_dryrun_contract.c
stage71/xnu_iokit_registry_topology_dryrun_contract.c
```

It then adds the attach/start readiness dry-run contract:

```text
stage71/xnu_iokit_attach_start_readiness_dryrun_contract.c
struct stage71_xnu_iokit_attach_start_readiness_dryrun_contract
```

The attach/start readiness dry-run imports source facts from:

- the Stage71 registry-plane / IODeviceTree topology dry-run contract,
- the Stage71 property-inheritance / registry-entry dry-run contract,
- the Stage71 catalog/property/personality dry-run contract,
- the Stage71 provider-plane dry-run contract,
- the Stage71 broader IOKit registry/service dry-run contract,
- the Stage71 IOKit/platform scaffold contract,
- the Stage71 local IOKit match dry-run contract,
- the Stage71-renumbered pexpert hook readiness contract,
- the retained Stage71 pmap transition dry-run contract,
- the public compile graph/object subset/controlled link proof,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree keeps the Stage66/Stage67 provider/service/candidate nodes and the Stage68/Stage69/Stage70 catalog/personality/registry-entry/topology nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
/iokit-catalog-property-dryrun
/stage71-platform-personality
/stage71-interrupt-personality
/stage71-timer-personality
/stage71-cpu-personality
/stage71-rejected-personality
```

Stage71 does not add new root nodes. The root child count remains 19. It extends the four selected personality nodes to 36 properties with local attach/start readiness facts:

```text
attach-start-readiness-dryrun=1
attach-readiness=1
start-readiness=1
attach-runtime-exec=0
start-runtime-exec=0
attach-provider-path="IODeviceTree:/" or "IODeviceTree:/stage71-platform-personality"
start-provider-path="IODeviceTree:/" or "IODeviceTree:/stage71-platform-personality"
attach-provider-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
start-provider-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
attach-order=0..3
start-order=0..3
provider-start-required=0 for platform, 1 for interrupt/timer/CPU
attach-start-provenance="stage71-registry-topology"
```

The rejected personality remains unpublished, unattached, unstarted, and unlinked with 35 properties:

```text
provider-plane-published=0
registry-entry-dryrun=0
property-inheritance-dryrun=1
registry-entry-ordinal=0xffffffff
registry-entry-attached=0
registry-topology-dryrun=0
registry-parent-path="IODeviceTree:/unlinked"
registry-entry-path="IODeviceTree:/stage71-rejected-personality"
registry-parent-ordinal=0xffffffff
registry-sibling-order=0xffffffff
registry-topology-provenance="stage71-rejected-unlinked"
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
attach-start-provenance="stage71-rejected-unattached"
rejected-candidate=1
stage-owned-local-only=1
```

The contract validates the selected attach/start matrix for platform, interrupt, timer, and CPU entries:

```text
attach_readiness_mask=0x0000000f
start_readiness_mask=0x0000000f
attach_provider_path_match_mask=0x0000000f
start_provider_path_match_mask=0x0000000f
attach_provider_ordinal_match_mask=0x0000000f
start_provider_ordinal_match_mask=0x0000000f
attach_order_match_mask=0x0000000f
start_order_match_mask=0x0000000f
provider_start_required_mask=0x0000000e
provider_start_satisfied_mask=0x0000000e
attach_runtime_blocked_mask=0x0000000f
start_runtime_blocked_mask=0x0000000f
attach_start_provenance_match_mask=0x0000000f
selected_attach_start_entry_mask=0x0000000f
rejected_attach_start_mask=0x00000010
attach_start_matrix=0x0000001f
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage71 does not add a new top-level loader satisfied bit. Instead, the attach/start readiness dry-run contract is copied into loader preflight and logged as a roll-up. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x71000001
```

## Selected public XNU baselines

Stage71 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM/IOKit reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage71/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage71_xnu_compile_graph_status=0x71000001
stage71_xnu_compile_graph_required_mask=0x7fffffff
stage71_xnu_compile_graph_satisfied_mask=0x7fffffff
stage71_xnu_compile_graph_failure_mask=0x00000000
stage71_xnu_compile_graph_candidate_count=0x00000017
stage71_xnu_compile_graph_allowed_compile_count=0x00000005
stage71_xnu_compile_graph_allowed_link_count=0x00000005
stage71_xnu_compile_graph_forbidden_count=0x00000012
stage71_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage71_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage71_xnu_compile_graph_pmap_public_link_count=0x00000000
stage71_xnu_compile_graph_iokit_reference_mask=0x00000007
stage71_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage71_xnu_compile_graph_iokit_reference_count=0x00000003
stage71_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage71_xnu_compile_graph_iokit_public_link_count=0x00000000
stage71_xnu_compile_graph_iokit_reference_only=0x00000001
stage71_xnu_object_subset_status=0x71000001
stage71_xnu_object_count=0x00000006
stage71_xnu_object_duplicate_symbol_count=0x00000000
stage71_xnu_link_status=0x71000001
stage71_xnu_link_object_count=0x00000006
stage71_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage71/stage71.elf` and `arm-none-eabi-nm -u out/stage71/xnu-link/stage71-xnu-link.elf` both reported no undefined symbols.

## Retained prerequisite contracts

Stage71 retains all Stage56 through Stage70 XNU mapping/pmap/platform/IOKit prerequisites as fail-closed source facts:

```text
stage71_xnu_bootstrap_contract_status=0x71000001
stage71_xnu_pmap_bootstrap_contract_status=0x71000001
stage71_xnu_pmap_table_dryrun_contract_status=0x71000001
stage71_xnu_pmap_page_dryrun_contract_status=0x71000001
stage71_xnu_pmap_attr_dryrun_contract_status=0x71000001
stage71_xnu_pmap_multiwindow_dryrun_contract_status=0x71000001
stage71_xnu_pmap_transition_dryrun_contract_status=0x71000001
stage71_xnu_pexpert_hook_readiness_contract_status=0x71000001
stage71_xnu_iokit_platform_scaffold_contract_status=0x71000001
stage71_xnu_iokit_match_dryrun_contract_status=0x71000001
stage71_xnu_iokit_registry_service_dryrun_contract_status=0x71000001
stage71_xnu_iokit_provider_plane_dryrun_contract_status=0x71000001
stage71_xnu_iokit_catalog_property_dryrun_contract_status=0x71000001
stage71_xnu_iokit_property_inheritance_dryrun_contract_status=0x71000001
stage71_xnu_iokit_registry_topology_dryrun_contract_status=0x71000001
```

## IOKit attach/start readiness dry-run contract

Confirmed target-side attach/start readiness markers:

```text
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_status=0x71000001
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_required_mask=0x7fffffff
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_checksum=0x011f8bea
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_selected_attach_start_entry_mask=0x0000000f
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_rejected_attach_start_mask=0x00000010
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_attach_start_matrix=0x0000001f
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_checksum=0xf7e3efe7
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_expected_checksum=0xf7e3efe7
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_no_attach_runtime_exec=0x00000001
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_no_start_runtime_exec=0x00000001
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_status=0x71000001
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_checksum=0x011f8bea
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_status_rollup=0x71000001
```

The retained Stage71 topology source contract also remains OK:

```text
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_status=0x71000001
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_checksum=0xf80001d6
```

Negative safety markers remained clear: public IOKit compile/link counts are zero, public IOKit runtime remains blocked, attach/start runtime execution counters remain blocked, no proposed workspace is written, no live pmap tables are installed, no pmap TTBR writes or pmap TLB invalidations occur, caches remain unchanged, and no persistent write is attempted.

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage71/build.sh
```

Final size:

```text
text=347004 data=0 bss=430028 dec=777032 hex=bdb48
```

Hashes:

```text
4c884c03d4b1b304d0b663bc841333881ce1f5879dbef9c928d3330f7065722f  out/stage71/stage71_fixture.macho
e14a822f66e9e047ca4a133790ce22ccdfc93d24bec4d9bae0f2d4ad26c2ecc8  out/stage71/stage71.elf
6230da48a315018cebc1c573c742e92526fe58547563639c0c5662d2f793f0ec  out/stage71/stage71.bin
5a8b9357f2041cece915d51cbca429a9739a22f2c935e90d85051facbeeb5e29  out/stage71/stage71.img
1365ddbd1f285d7771ab0cc6e232b5e677b196c3857d0871c904b15658c8f3af  out/stage71/stage71-qcdt.img
```

## Boot image parse facts

```text
out/stage71/stage71.img:
  page_size=2048
  kernel_size=347004 (0x54b7c)
  dt_size=0

out/stage71/stage71-qcdt.img:
  page_size=2048
  kernel_size=347004 (0x54b7c)
  dt_size=2521088 (0x267800)
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage71/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage71/stage71.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage71/stage71-qcdt.img
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage71/stage71.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage71/xnu-link/stage71-xnu-link.elf
```

Validation facts:

```text
stage71/boot_args.c command_line_len_with_nul=220 fits256=True has_attachstart=True has_pexpert_hook_ready=True
stage71/stage71_main.c command_line_len_with_nul=220 fits256=True has_attachstart=True has_pexpert_hook_ready=True
stage71/xnu_object_shims.c command_line_len_with_nul=220 fits256=True has_attachstart=True has_pexpert_hook_ready=True
build_cmdline_len=1349 fits1536=True has_attachstart=True has_no_attachstart_exec=True
root_child_count_19=True
selected_prop_count_36_nodes=4
rejected_prop_count_35_nodes=1
stage71_attach_start_expected_checksum_marker=True
stage71_source_stale_marker_violations=0
```

The fixed public ARM `CommandLine[256]` strings include `iokit-attachstart`, preserve `pexpert-hook-ready`, and fit at 220 bytes including NUL. The Android boot-image command line uses shortened attach/start markers (`xnu-iokit-attachstart`, `iokit-attachstart-local`, and `no-attachstart-exec`) and fits the legacy `mkbootimg` limit at 1349 bytes.

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage71/stage71-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage71-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage71-last_kmsg.txt
size=222337 bytes
MI4IOS6_STAGE71_count=3044
MI4IOS6_STAGE71_XNU_count=3006
```

Final success markers:

```text
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_status=0x71000001
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_status=0x71000001
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_selected_attach_start_entry_mask=0x0000000f
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_rejected_attach_start_mask=0x00000010
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_attach_start_matrix=0x0000001f
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_checksum=0xf7e3efe7
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_expected_checksum=0xf7e3efe7
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_no_attach_runtime_exec=0x00000001
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_no_start_runtime_exec=0x00000001
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_status_rollup=0x71000001
MI4IOS6_STAGE71_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE71_XNU loader_status=0x71000001
MI4IOS6_STAGE71_XNU kernel_entry ok
MI4IOS6_STAGE71 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, `mmu high bootstrap selftest failed`, or `apple_dt walk length mismatch` in the successful run.

## Result

Stage71 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit attach/start readiness dry-run over the retained registry-plane / IODeviceTree topology dry-run, retained property-inheritance / registry-entry dry-run, retained catalog/property/personality dry-run, retained provider-plane dry-run, retained IOKit/platform scaffold, retained local service/driver match dry-run, retained broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality/topology nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit lifecycle proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
