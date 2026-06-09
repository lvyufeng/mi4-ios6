# Experiment 73 — Stage70 XNU IOKit registry-plane / IODeviceTree topology dry-run contract

## Goal

Stage70 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage69 local IOKit property-inheritance / registry-entry dry-run toward a local IODeviceTree topology proof.

The goal is not to run XNU, not to execute IOKit, not to execute `IORegistryEntry`, `IOService`, `IODeviceTreeSupport`, registry-topology, property-inheritance, registry-entry, attach/start, provider-plane, catalog, property-plane, or platform-driver runtime code, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the four selected Stage69 registry entries can be projected into deterministic local IODeviceTree topology facts for platform, interrupt, timer, and CPU personalities while one explicit rejected personality remains unpublished, unattached, and unlinked.

## Safety boundary

Stage70 preserves all Stage69 boundaries and extends them to registry-topology runtime:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, attach/start, or platform-driver runtime execution.
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
stage70/xnu_object_shims.c
```

Public ARM VM/pmap files and public IOKit files remain reference-only. The public IOKit reference-only set is:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, linked, or executed.

## Implementation

Stage70 adds:

```text
stage70/xnu_iokit_registry_topology_dryrun_contract.c
struct stage70_xnu_iokit_registry_topology_dryrun_contract
```

The new contract imports source facts from:

- the Stage70 IOKit property-inheritance / registry-entry dry-run contract,
- the Stage70 IOKit catalog/property/personality dry-run contract,
- the Stage70 IOKit provider-plane dry-run contract,
- the Stage70 broader IOKit registry/service dry-run contract,
- the Stage70 IOKit/platform scaffold contract,
- the Stage70 single-match IOKit dry-run contract,
- the Stage70 pexpert hook readiness contract,
- the Stage70 public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

Stage70 keeps the Stage69 root-node set and adds no new root children. The Stage-owned Apple flattened device tree root remains at 19 children. The four selected catalog personality nodes are extended with local IODeviceTree topology properties:

```text
/stage70-platform-personality
/stage70-interrupt-personality
/stage70-timer-personality
/stage70-cpu-personality

registry-topology-dryrun=1
registry-parent-path="IODeviceTree:/" or "IODeviceTree:/stage70-platform-personality"
registry-entry-path="IODeviceTree:/stage70-*-personality"
registry-parent-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
registry-sibling-order=0..3
registry-topology-provenance="stage70-property-inheritance"
```

The rejected catalog personality is extended with negative topology facts:

```text
/stage70-rejected-personality

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

The registry-plane / IODeviceTree topology dry-run proves:

- The Stage69-renumbered property-inheritance / registry-entry dry-run is OK.
- The Stage68-renumbered catalog/property dry-run is OK.
- The Stage67-renumbered provider-plane dry-run is OK.
- The Stage66-renumbered IOKit broader registry/service dry-run is OK.
- The Stage65-renumbered IOKit single-match dry-run is OK.
- The Stage64-renumbered IOKit/platform scaffold readiness is OK.
- The Stage63-renumbered pexpert hook readiness is OK.
- Public compile graph/object subset/link facts are OK.
- Public IOKit reference mask is complete (`0x00000007`).
- Public IOKit runtime-blocked mask is complete (`0x00000007`).
- Public IOKit compile/link counts are zero.
- The selected platform, interrupt, timer, and CPU personalities have `registry-topology-dryrun=1`.
- The selected platform, interrupt, timer, and CPU personalities still resolve to the `IODeviceTree` registry plane.
- Selected parent paths, entry paths, parent ordinals, sibling orders, and provenance strings are deterministic.
- The selected topology entry mask is complete (`0x0000000f`).
- The rejected personality remains unlinked with mask `0x00000010`.
- Exactly five personality candidates are considered, four selected topology dry-runs are produced, and one rejected/unlinked candidate is recorded.
- Public pexpert/platform runtime, public IOKit runtime, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start runtime, Stage-owned platform-driver runtime, public VM/pmap runtime, generated Mach-O execution, XNU entry execution, proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage70 does not add a 33rd top-level loader bit. Instead, it records the registry-plane / IODeviceTree topology dry-run as a contract roll-up copied into loader preflight. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x70000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage70/build.sh
```

Final build size:

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

Boot image parse checks confirmed:

```text
stage70.img:      page_size=2048 kernel_size=329100 (0x5058c) dt_size=0
stage70-qcdt.img: page_size=2048 kernel_size=329100 (0x5058c) dt_size=2521088 (0x267800)
```

Undefined symbol checks were clean for both:

```text
out/stage70/stage70.elf
out/stage70/xnu-link/stage70-xnu-link.elf
```

Static validation facts:

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

The fixed public ARM `CommandLine[256]` strings fit at 253 bytes including NUL and include `iokit-regtop-dryrun`. The Android boot-image command line uses shortened registry-topology markers (`xnu-iokit-regtop`, `iokit-regtop-local`, and `no-regtop-exec`) and fits the legacy `mkbootimg` limit at 1365 bytes.

## Hardware validation

Hardware validation used non-persistent boot only:

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
stage70_marker_count=2977
stage70_xnu_marker_count=2939
```

Key IOKit registry-plane / IODeviceTree topology dry-run markers:

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

Retained IOKit property-inheritance, catalog/property, provider-plane, registry/service, match, scaffold, pexpert, and pmap transition source facts also remained OK:

```text
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_property_inheritance_dryrun_contract_status=0x70000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_catalog_property_dryrun_contract_status=0x70000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_provider_plane_dryrun_contract_status=0x70000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_service_dryrun_contract_status=0x70000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_match_dryrun_contract_status=0x70000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_platform_scaffold_contract_status=0x70000001
MI4IOS6_STAGE70_XNU stage70_xnu_pmap_transition_dryrun_contract_status=0x70000001
MI4IOS6_STAGE70_XNU stage70_xnu_pexpert_hook_readiness_contract_status=0x70000001
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
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_provider_runtime_exec=0x00000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_proposed_workspace_written=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_caches_changed=0x00000000
MI4IOS6_STAGE70_XNU stage70_xnu_iokit_registry_topology_persistent_write_attempted=0x00000000
```

Final loader/handoff markers:

```text
MI4IOS6_STAGE70_XNU loader_xnu_iokit_registry_topology_dryrun_contract_status=0x70000001
MI4IOS6_STAGE70_XNU loader_xnu_iokit_registry_topology_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE70_XNU loader_xnu_iokit_registry_topology_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE70_XNU loader_xnu_iokit_registry_topology_dryrun_contract_checksum=0xf8001093
MI4IOS6_STAGE70_XNU loader_xnu_iokit_registry_topology_dryrun_contract_status_rollup=0x70000001
MI4IOS6_STAGE70_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE70_XNU loader_status=0x70000001
MI4IOS6_STAGE70_XNU kernel_entry ok
MI4IOS6_STAGE70 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, or `mmu high bootstrap selftest failed` in the successful run.

## Result

Stage70 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit registry-plane / IODeviceTree topology dry-run over the retained IOKit property-inheritance / registry-entry proof, retained IOKit catalog/property/personality proof, retained provider-plane publication proof, retained IOKit/platform scaffold, retained local service/driver match dry-run, retained broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit attach/start readiness proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
