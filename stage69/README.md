# Stage69 — XNU IOKit property-inheritance / registry-entry dry-run contract

Stage69 keeps the hardware-validated Stage68 IOKit catalog/property/personality dry-run proof stable and adds a Stage-owned **IOKit property-inheritance / registry-entry dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute an `IORegistryEntry`/`IOService`/catalog/property-inheritance runtime, not to execute attach/start, not to execute an MSM8974 platform driver, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage69 proves that the four selected local catalog personalities can be projected into deterministic local registry-entry facts while all decisions remain over Stage-owned Apple-DT data only.

## Safety model

Stage69 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, attach/start, or platform-driver runtime execution.
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

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage69 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage69/xnu_object_shims.c
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

## What Stage69 adds

Stage69 retains the Stage64-through-Stage68 IOKit prerequisite contracts after renumbering to Stage69:

```text
stage69/xnu_iokit_platform_scaffold_contract.c
struct stage69_xnu_iokit_platform_scaffold_contract

stage69/xnu_iokit_match_dryrun_contract.c
struct stage69_xnu_iokit_match_dryrun_contract

stage69/xnu_iokit_registry_service_dryrun_contract.c
struct stage69_xnu_iokit_registry_service_dryrun_contract

stage69/xnu_iokit_provider_plane_dryrun_contract.c
struct stage69_xnu_iokit_provider_plane_dryrun_contract

stage69/xnu_iokit_catalog_property_dryrun_contract.c
struct stage69_xnu_iokit_catalog_property_dryrun_contract
```

It then adds the property-inheritance / registry-entry dry-run contract:

```text
stage69/xnu_iokit_property_inheritance_dryrun_contract.c
struct stage69_xnu_iokit_property_inheritance_dryrun_contract
```

The property-inheritance dry-run imports source facts from:

- the Stage69 catalog/property/personality dry-run contract,
- the Stage69 provider-plane dry-run contract,
- the Stage69 broader IOKit registry/service dry-run contract,
- the Stage69 IOKit/platform scaffold contract,
- the Stage69 local IOKit match dry-run contract,
- the Stage69-renumbered pexpert hook readiness contract,
- the retained Stage69 pmap transition dry-run contract,
- the public compile graph/object subset/controlled link proof,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree keeps the Stage66/Stage67 provider/service/candidate nodes and the Stage68 catalog/personality nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
/iokit-catalog-property-dryrun
/stage69-platform-personality
/stage69-interrupt-personality
/stage69-timer-personality
/stage69-cpu-personality
/stage69-rejected-personality
```

Stage69 does not add new root nodes. It extends the four selected personality nodes from 11 to 17 properties with local registry-entry projection facts:

```text
registry-entry-dryrun=1
property-inheritance-dryrun=1
registry-entry-ordinal=0..3
registry-plane="IODeviceTree"
attach-deferred=1
start-deferred=1
```

The rejected personality is explicitly kept unpublished and unattached:

```text
provider-plane-published=0
registry-entry-dryrun=0
property-inheritance-dryrun=1
registry-entry-ordinal=0xffffffff
registry-entry-attached=0
rejected-candidate=1
stage-owned-local-only=1
```

The contract validates inherited properties for the four selected registry entries:

```text
IOClass
IOProviderClass
IOMatchCategory
compatible
IOProbeScore
CFBundleIdentifier
```

It also validates instance properties and local-only markers:

```text
source-service-bit
provider-plane-published
stage-owned-local-only
registry-entry-dryrun
property-inheritance-dryrun
registry-entry-ordinal
registry-plane
attach-deferred
start-deferred
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage69 does not add a new top-level loader satisfied bit. Instead, the property-inheritance / registry-entry dry-run contract is copied into loader preflight and logged as a roll-up. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x69000001
```

## Selected public XNU baselines

Stage69 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage69/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage69_xnu_compile_graph_status=0x69000001
stage69_xnu_compile_graph_required_mask=0x7fffffff
stage69_xnu_compile_graph_satisfied_mask=0x7fffffff
stage69_xnu_compile_graph_failure_mask=0x00000000
stage69_xnu_compile_graph_candidate_count=0x00000017
stage69_xnu_compile_graph_allowed_compile_count=0x00000005
stage69_xnu_compile_graph_allowed_link_count=0x00000005
stage69_xnu_compile_graph_forbidden_count=0x00000012
stage69_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage69_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage69_xnu_compile_graph_pmap_public_link_count=0x00000000
stage69_xnu_compile_graph_iokit_reference_mask=0x00000007
stage69_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage69_xnu_compile_graph_iokit_reference_count=0x00000003
stage69_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage69_xnu_compile_graph_iokit_public_link_count=0x00000000
stage69_xnu_compile_graph_iokit_reference_only=0x00000001
stage69_xnu_compile_graph_platform_reference_count=0x00000009
stage69_xnu_compile_graph_blocked_runtime_count=0x00000004
stage69_xnu_object_subset_status=0x69000001
stage69_xnu_object_count=0x00000006
stage69_xnu_object_duplicate_symbol_count=0x00000000
stage69_xnu_link_status=0x69000001
stage69_xnu_link_object_count=0x00000006
stage69_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage69/stage69.elf` and `arm-none-eabi-nm -u out/stage69/xnu-link/stage69-xnu-link.elf` both reported no undefined symbols.

## Retained prerequisite contracts

Stage69 retains all Stage56 through Stage68 XNU mapping/pmap/platform/IOKit prerequisites as fail-closed source facts:

```text
stage69_xnu_bootstrap_contract_status=0x69000001
stage69_xnu_pmap_bootstrap_contract_status=0x69000001
stage69_xnu_pmap_table_dryrun_contract_status=0x69000001
stage69_xnu_pmap_page_dryrun_contract_status=0x69000001
stage69_xnu_pmap_attr_dryrun_contract_status=0x69000001
stage69_xnu_pmap_multiwindow_dryrun_contract_status=0x69000001
stage69_xnu_pmap_transition_dryrun_contract_status=0x69000001
stage69_xnu_pexpert_hook_readiness_contract_status=0x69000001
stage69_xnu_iokit_platform_scaffold_contract_status=0x69000001
stage69_xnu_iokit_match_dryrun_contract_status=0x69000001
stage69_xnu_iokit_registry_service_dryrun_contract_status=0x69000001
stage69_xnu_iokit_provider_plane_dryrun_contract_status=0x69000001
stage69_xnu_iokit_catalog_property_dryrun_contract_status=0x69000001
```

## IOKit property-inheritance / registry-entry dry-run contract

Confirmed target-side property-inheritance / registry-entry markers:

```text
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_dryrun_contract_status=0x69000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_dryrun_contract_required_mask=0x7fffffff
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_dryrun_contract_checksum=0xceb8b00b
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_source_catalog_status=0x69000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_source_catalog_satisfied_mask=0x7fffffff
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_source_catalog_failure_mask=0x00000000
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_source_catalog_property_checksum=0xfffffffb
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_apple_dt_semantic_mask=0x0003ffff
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_registry_entry_marker_mask=0x0000000f
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_marker_mask=0x0000001f
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_registry_entry_ordinal_mask=0x0000000f
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_registry_plane_match_mask=0x0000000f
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_inherited_property_mask=0x0000000f
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_instance_property_mask=0x0000000f
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_selected_registry_entry_mask=0x0000000f
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_rejected_unattached_mask=0x00000010
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_matrix=0x0000001f
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_checksum=0x494f4455
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_expected_checksum=0x494f4455
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_candidate_count=0x00000005
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_selected_entry_count=0x00000004
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_rejected_entry_count=0x00000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_dryrun_count=0x00000004
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_attach_deferred_count=0x00000004
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_start_deferred_count=0x00000004
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_rejected_unattached_count=0x00000001
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_status=0x69000001
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_checksum=0xceb8b00b
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_status_rollup=0x69000001
```

Negative safety markers remained clear:

```text
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_public_compile_count=0x00000000
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_public_link_count=0x00000000
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_no_registry_entry_runtime_exec=0x00000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_no_property_runtime_exec=0x00000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_no_attach_runtime_exec=0x00000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_no_start_runtime_exec=0x00000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_no_provider_runtime_exec=0x00000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_proposed_workspace_written=0x00000000
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_caches_changed=0x00000000
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_persistent_write_attempted=0x00000000
```

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage69/build.sh
```

Final size:

```text
text=317208 data=0 bss=429092 dec=746300 hex=b633c
```

Hashes:

```text
d03a206260db6541bfd5069ec977a45759ab4c3bada5c7405f1f07c2db671ef7  out/stage69/stage69_fixture.macho
fa88b7fd500cace55f07e5db774629451bac3c97dd10af2f974f914f7fcbcafb  out/stage69/stage69.elf
922f8387fdeb04c4f6fe4e09a095696ba6374569339200e2dd4765de007251df  out/stage69/stage69.bin
a5af565914c02d352c69a1a80077ed517228d12d938d0d830b319aa0790be007  out/stage69/stage69.img
a54bc63d15284328c3021abd71f2471059feddd9024097f695579fa9d8db94db  out/stage69/stage69-qcdt.img
```

## Boot image parse facts

```text
out/stage69/stage69.img:
  page_size=2048
  kernel_size=317208 (0x4d718)
  dt_size=0

out/stage69/stage69-qcdt.img:
  page_size=2048
  kernel_size=317208 (0x4d718)
  dt_size=2521088 (0x267800)
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage69/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage69/stage69.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage69/stage69-qcdt.img
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage69/stage69.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage69/xnu-link/stage69-xnu-link.elf
```

Validation facts:

```text
stage69/boot_args.c command_line_len_with_nul=238 fits256=True
stage69/stage69_main.c command_line_len_with_nul=238 fits256=True
stage69/xnu_object_shims.c command_line_len_with_nul=238 fits256=True
build_cmdline_len=1406 fits1536=True has_propinh=True
stage69_stale_marker_violations=0
apple_dt_node_begin(b, 4, 19) present=True
root_dt_root_children == 19u present=True
root_dt_root_children != 19u present=True
out/stage69 ignored
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

The fixed public ARM `CommandLine[256]` strings include `iokit-propinh-dryrun` and fit at 238 bytes including NUL. The Android boot-image command line uses shortened property-inheritance markers (`xnu-iokit-propinh-contract`, `iokit-propinh-local-only`, and `no-iokit-propinh-exec`) and fits the legacy limit at 1406 bytes.

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage69/stage69-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage69-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage69-last_kmsg.txt
size=208904 bytes
MI4IOS6_STAGE69 marker count=2901
MI4IOS6_STAGE69_XNU marker count=2863
```

Final success markers:

```text
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_dryrun_contract_status=0x69000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_checksum=0x494f4455
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_property_inheritance_expected_checksum=0x494f4455
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_status_rollup=0x69000001
MI4IOS6_STAGE69_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE69_XNU loader_status=0x69000001
MI4IOS6_STAGE69_XNU kernel_entry ok
MI4IOS6_STAGE69 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, or `mmu high bootstrap selftest failed` in the successful run.

## Result

Stage69 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit property-inheritance / registry-entry dry-run over the retained catalog/property/personality dry-run, retained provider-plane dry-run, retained IOKit/platform scaffold, retained local service/driver match dry-run, retained broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit registry-plane/IODeviceTree relationship proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/attach/start runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
