# Stage68 — XNU IOKit catalog/property/personality dry-run contract

Stage68 keeps the hardware-validated Stage67 IOKit provider-plane publish/order dry-run proof stable and adds a Stage-owned **IOKit catalog/property/personality dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute a catalog/property-plane runtime, not to execute a provider-plane runtime, not to execute an MSM8974 platform driver, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage68 proves that the local Stage-owned provider-plane publications can be projected into a deterministic catalog/personality/property model while all decisions remain over Stage-owned Apple-DT data only.

## Safety model

Stage68 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, or platform-driver runtime execution.
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

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage68 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage68/xnu_object_shims.c
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

## What Stage68 adds

Stage68 retains the Stage64-renumbered IOKit/platform scaffold contract, the Stage65-renumbered one-provider/one-driver IOKit match dry-run contract, the Stage66-renumbered broader registry/service dry-run contract, and the Stage67-renumbered provider-plane publish/order dry-run contract:

```text
stage68/xnu_iokit_platform_scaffold_contract.c
struct stage68_xnu_iokit_platform_scaffold_contract

stage68/xnu_iokit_match_dryrun_contract.c
struct stage68_xnu_iokit_match_dryrun_contract

stage68/xnu_iokit_registry_service_dryrun_contract.c
struct stage68_xnu_iokit_registry_service_dryrun_contract

stage68/xnu_iokit_provider_plane_dryrun_contract.c
struct stage68_xnu_iokit_provider_plane_dryrun_contract
```

It then adds the catalog/property/personality dry-run contract:

```text
stage68/xnu_iokit_catalog_property_dryrun_contract.c
struct stage68_xnu_iokit_catalog_property_dryrun_contract
```

The catalog/property dry-run imports source facts from:

- the Stage68 provider-plane dry-run contract,
- the Stage68 broader IOKit registry/service dry-run contract,
- the Stage68 IOKit/platform scaffold contract,
- the Stage68 local IOKit match dry-run contract,
- the Stage68-renumbered pexpert hook readiness contract,
- the retained Stage68 pmap transition dry-run contract,
- the public compile graph/object subset/controlled link proof,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree keeps the Stage66/Stage67 provider/service/candidate nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
```

Stage68 adds six local catalog/personality nodes:

```text
/iokit-catalog-property-dryrun
/stage68-platform-personality
/stage68-interrupt-personality
/stage68-timer-personality
/stage68-cpu-personality
/stage68-rejected-personality
```

The catalog root is local-only (`stage-owned-local-only=1`) with catalog version `1` and five personalities. Four selected personalities project the Stage67 provider-plane publications, and one rejected personality remains unpublished:

```text
platform_personality_ordinal=0
interrupt_personality_ordinal=1
timer_personality_ordinal=2
cpu_personality_ordinal=3
rejected_personality_ordinal=0xffffffff

source_selected_service_mask=0x0000000f
source_rejected_service_mask=0x00000010
selected_personality_mask=0x0000000f
rejected_personality_mask=0x00000010
catalog_property_matrix=0x0000001f
```

The dry-run validates deterministic property coverage for personality name, bundle identifier, catalog marker, provider class, match category, compatible string, probe score, and local-only marker. Attach and start remain deferred for all selected personalities:

```text
name_match_mask=0x0000000f
bundle_identifier_match_mask=0x0000000f
catalog_marker_match_mask=0x0000000f
provider_class_match_mask=0x0000000f
category_match_mask=0x0000000f
compatible_match_mask=0x0000000f
probe_score_match_mask=0x0000000f
local_only_match_mask=0x0000000f

catalog_count=1
personality_candidate_count=5
selected_personality_count=4
rejected_personality_count=1
property_dryrun_count=4
attach_deferred_count=4
start_deferred_count=4
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage68 does not add a new top-level loader satisfied bit. Instead, the IOKit/platform scaffold, single-match dry-run, broader registry/service dry-run, provider-plane dry-run, and catalog/property/personality dry-run contracts are copied into loader preflight and logged as roll-ups. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x68000001
```

## Selected public XNU baselines

Stage68 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage68/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage68_xnu_compile_graph_status=0x68000001
stage68_xnu_compile_graph_required_mask=0x7fffffff
stage68_xnu_compile_graph_satisfied_mask=0x7fffffff
stage68_xnu_compile_graph_failure_mask=0x00000000
stage68_xnu_compile_graph_candidate_count=0x00000017
stage68_xnu_compile_graph_allowed_compile_count=0x00000005
stage68_xnu_compile_graph_allowed_link_count=0x00000005
stage68_xnu_compile_graph_forbidden_count=0x00000012
stage68_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage68_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage68_xnu_compile_graph_pmap_public_link_count=0x00000000
stage68_xnu_compile_graph_iokit_reference_mask=0x00000007
stage68_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage68_xnu_compile_graph_iokit_reference_count=0x00000003
stage68_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage68_xnu_compile_graph_iokit_public_link_count=0x00000000
stage68_xnu_compile_graph_iokit_reference_only=0x00000001
stage68_xnu_compile_graph_platform_reference_count=0x00000009
stage68_xnu_compile_graph_blocked_runtime_count=0x00000004
stage68_xnu_object_subset_status=0x68000001
stage68_xnu_object_count=0x00000006
stage68_xnu_object_duplicate_symbol_count=0x00000000
stage68_xnu_link_status=0x68000001
stage68_xnu_link_object_count=0x00000006
stage68_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage68/stage68.elf` and `arm-none-eabi-nm -u out/stage68/xnu-link/stage68-xnu-link.elf` both reported no undefined symbols.

## Retained prerequisite contracts

Stage68 retains all Stage56 through Stage67 XNU mapping/pmap/platform/IOKit prerequisites as fail-closed source facts:

```text
stage68_xnu_bootstrap_contract_status=0x68000001
stage68_xnu_pmap_bootstrap_contract_status=0x68000001
stage68_xnu_pmap_table_dryrun_contract_status=0x68000001
stage68_xnu_pmap_page_dryrun_contract_status=0x68000001
stage68_xnu_pmap_attr_dryrun_contract_status=0x68000001
stage68_xnu_pmap_multiwindow_dryrun_contract_status=0x68000001
stage68_xnu_pmap_transition_dryrun_contract_status=0x68000001
stage68_xnu_pexpert_hook_readiness_contract_status=0x68000001
stage68_xnu_iokit_platform_scaffold_contract_status=0x68000001
stage68_xnu_iokit_match_dryrun_contract_status=0x68000001
stage68_xnu_iokit_registry_service_dryrun_contract_status=0x68000001
stage68_xnu_iokit_provider_plane_dryrun_contract_status=0x68000001
```

## IOKit catalog/property/personality dry-run contract

Confirmed target-side catalog/property markers:

```text
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_status=0x68000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_required_mask=0x7fffffff
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_checksum=0x6fffd084
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_source_provider_status=0x68000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_source_registry_status=0x68000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_apple_dt_semantic_mask=0x0001ffff
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_catalog_version=0x00000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_catalog_local_only=0x00000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_source_selected_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_source_rejected_mask=0x00000010
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_source_fact_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_source_parent_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_source_dependency_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_source_publish_order_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_name_match_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_bundle_match_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_catalog_marker_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_provider_class_match_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_category_match_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_compatible_match_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_probe_score_match_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_local_only_match_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_selected_personality_mask=0x0000000f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_rejected_personality_mask=0x00000010
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_matrix=0x0000001f
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_order_checksum=0xffffffff
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_checksum=0xfffffffb
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_expected_checksum=0xfffffffb
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_catalog_count=0x00000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_personality_candidate_count=0x00000005
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_selected_personality_count=0x00000004
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_rejected_personality_count=0x00000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_count=0x00000004
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_attach_deferred_count=0x00000004
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_start_deferred_count=0x00000004
MI4IOS6_STAGE68_XNU loader_xnu_iokit_catalog_property_dryrun_contract_status=0x68000001
MI4IOS6_STAGE68_XNU loader_xnu_iokit_catalog_property_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE68_XNU loader_xnu_iokit_catalog_property_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE68_XNU loader_xnu_iokit_catalog_property_dryrun_contract_checksum=0x6fffd084
MI4IOS6_STAGE68_XNU loader_xnu_iokit_catalog_property_dryrun_contract_status_rollup=0x68000001
```

Negative safety markers remained clear:

```text
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_public_compile_count=0x00000000
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_public_link_count=0x00000000
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_no_catalog_runtime_exec=0x00000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_no_provider_runtime_exec=0x00000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_proposed_workspace_written=0x00000000
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_caches_changed=0x00000000
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_persistent_write_attempted=0x00000000
```

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage68/build.sh
```

Final size:

```text
text=300672 data=0 bss=428636 dec=729308 hex=b20dc
```

Hashes:

```text
a5ca5b26473564a763bc5f61c2196bbbe86c4d73bafe6e87c377b5ef26367cfc  out/stage68/stage68_fixture.macho
a2e474202c5d8f3f651b1c2cc172668dc1acfd564aeee72e0ec9394f98fd56f2  out/stage68/stage68.elf
799e6c82c011b4cc742c9634aae38a829d56f5fcd452645067a77160abce37a8  out/stage68/stage68.bin
3579fe36ee2a345cc55b9537231f2ff42523d81eaf3bf23b86515d0587de0af8  out/stage68/stage68.img
21d46a040917364e20573a9905ab6fd38a5981e8106a5c421bda688fc8915339  out/stage68/stage68-qcdt.img
```

## Boot image parse facts

```text
out/stage68/stage68.img:
  page_size=2048
  kernel_size=300672 (0x49680)
  dt_size=0
  kernel_sha256=799e6c82c011b4cc742c9634aae38a829d56f5fcd452645067a77160abce37a8

out/stage68/stage68-qcdt.img:
  page_size=2048
  kernel_size=300672 (0x49680)
  dt_size=2521088 (0x267800)
  kernel_sha256=799e6c82c011b4cc742c9634aae38a829d56f5fcd452645067a77160abce37a8
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage68/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage68/stage68.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py /mnt/data/mi4-ios6/out/stage68/stage68-qcdt.img
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage68/stage68.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage68/xnu-link/stage68-xnu-link.elf
```

Validation facts:

```text
stage68/boot_args.c command_line_len_with_nul=242 fits256=True
stage68/stage68_main.c command_line_len_with_nul=242 fits256=True
stage68/xnu_object_shims.c command_line_len_with_nul=242 fits256=True
stage68_source_stale_marker_violations=0
out/stage68 ignored
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

The first generated boot image command-line attempt exceeded the Android boot-image `cmdline` limit. The retained Stage68 build script uses shortened boot-image markers (`xnu-iokit-provider-plane-contract`, `xnu-iokit-catalog-property-contract`, `iokit-provider-plane-local-only`, and `iokit-catalog-property-local-only`) while keeping the fixed public ARM `CommandLine[256]` strings at 242 bytes including NUL.

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage68/stage68-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage68-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage68-last_kmsg.txt
size=201848 bytes
MI4IOS6_STAGE68 marker count=2826
MI4IOS6_STAGE68_XNU marker count=2788
```

The first Stage68 hardware pass exposed one stale high-DT child-count check after adding the six catalog/personality nodes. The new Apple-DT root has 19 children (`0x13`); `stage68/mmu.c` now validates 19 instead of the old 13, and the successful validation confirms the high-DT summary is accepted.

Final success markers:

```text
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_status=0x68000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_required_mask=0x7fffffff
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE68_XNU loader_xnu_iokit_catalog_property_dryrun_contract_status_rollup=0x68000001
MI4IOS6_STAGE68_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE68_XNU loader_status=0x68000001
MI4IOS6_STAGE68_XNU kernel_entry ok
MI4IOS6_STAGE68_XNU Stage68 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE68 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, or `mmu high bootstrap selftest failed` in the successful run.

## Result

Stage68 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit catalog/property/personality dry-run over the retained IOKit/platform scaffold, the retained local service/driver match dry-run, the retained broader registry/service dry-run, the retained provider-plane publish/order dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit property inheritance/registry-plane proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
