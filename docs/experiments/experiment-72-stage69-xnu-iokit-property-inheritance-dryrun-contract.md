# Experiment 72 — Stage69 XNU IOKit property-inheritance / registry-entry dry-run contract

## Goal

Stage69 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage68 local IOKit catalog/property/personality dry-run toward a local registry-entry projection proof.

The goal is not to run XNU, not to execute IOKit, not to execute `IORegistryEntry`, `IOCatalogue`, `IOService`, `IODeviceTreeSupport`, property-inheritance, registry-entry, attach/start, provider-plane, catalog, property-plane, or platform-driver runtime code, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the four selected Stage68 catalog personalities can be projected into deterministic local registry-entry facts for platform, interrupt, timer, and CPU services while one explicit rejected personality remains unpublished and unattached.

## Safety boundary

Stage69 preserves all Stage68 boundaries:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, attach/start, or platform-driver runtime execution.
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
stage69/xnu_object_shims.c
```

Public ARM VM/pmap files and public IOKit files remain reference-only. The public IOKit reference-only set is:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, linked, or executed.

## Implementation

Stage69 adds:

```text
stage69/xnu_iokit_property_inheritance_dryrun_contract.c
struct stage69_xnu_iokit_property_inheritance_dryrun_contract
```

The new contract imports source facts from:

- the Stage69 IOKit catalog/property/personality dry-run contract,
- the Stage69 IOKit provider-plane dry-run contract,
- the Stage69 broader IOKit registry/service dry-run contract,
- the Stage69 IOKit/platform scaffold contract,
- the Stage69 single-match IOKit dry-run contract,
- the Stage69 pexpert hook readiness contract,
- the Stage69 public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

Stage69 keeps the Stage68 root-node set and adds no new root children. The Stage-owned Apple flattened device tree root remains at 19 children. The four selected catalog personality nodes are extended with local registry-entry projection properties:

```text
/stage69-platform-personality
/stage69-interrupt-personality
/stage69-timer-personality
/stage69-cpu-personality

registry-entry-dryrun=1
property-inheritance-dryrun=1
registry-entry-ordinal=0..3
registry-plane="IODeviceTree"
attach-deferred=1
start-deferred=1
```

The rejected catalog personality is extended with negative registry-entry facts:

```text
/stage69-rejected-personality

provider-plane-published=0
registry-entry-dryrun=0
property-inheritance-dryrun=1
registry-entry-ordinal=0xffffffff
registry-entry-attached=0
rejected-candidate=1
stage-owned-local-only=1
```

The property-inheritance / registry-entry dry-run proves:

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
- The selected platform, interrupt, timer, and CPU personalities have `registry-entry-dryrun=1`.
- All five personalities have the property-inheritance marker, while only the selected four become local registry-entry dry-runs.
- The selected platform/interrupt/timer/CPU registry-entry ordinals are deterministic (`0`, `1`, `2`, `3`).
- The rejected ordinal remains `0xffffffff`.
- The local registry plane marker is `IODeviceTree` for all selected entries.
- Inherited property masks for `IOClass`, `IOProviderClass`, `IOMatchCategory`, `compatible`, `IOProbeScore`, and `CFBundleIdentifier` are complete for the selected entries.
- Instance property masks for `source-service-bit`, `provider-plane-published`, `stage-owned-local-only`, `registry-entry-dryrun`, attach-deferred, and start-deferred are complete for the selected entries.
- The rejected personality remains unpublished and unattached with mask `0x00000010`.
- Exactly five personality candidates are considered, four selected registry-entry dry-runs are produced, and one rejected/unattached candidate is recorded.
- Public pexpert/platform runtime, public IOKit runtime, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/attach/start runtime, Stage-owned platform-driver runtime, public VM/pmap runtime, generated Mach-O execution, XNU entry execution, proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage69 does not add a 33rd top-level loader bit. Instead, it records the property-inheritance / registry-entry dry-run as a contract roll-up copied into loader preflight. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x69000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage69/build.sh
```

Final build size:

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

Boot image parse checks confirmed:

```text
stage69.img:      page_size=2048 kernel_size=317208 (0x4d718) dt_size=0
stage69-qcdt.img: page_size=2048 kernel_size=317208 (0x4d718) dt_size=2521088 (0x267800)
```

Undefined symbol checks were clean for both:

```text
out/stage69/stage69.elf
out/stage69/xnu-link/stage69-xnu-link.elf
```

Static validation facts:

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

The fixed public ARM `CommandLine[256]` strings fit at 238 bytes including NUL. The Android boot-image command line uses shortened markers (`xnu-iokit-propinh-contract`, `iokit-propinh-local-only`, and `no-iokit-propinh-exec`) and fits the legacy `mkbootimg` limit at 1406 bytes.

## Hardware validation

Hardware validation used non-persistent boot only:

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
stage69_marker_count=2901
stage69_xnu_marker_count=2863
```

Key IOKit property-inheritance / registry-entry dry-run markers:

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
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_status_rollup=0x69000001
```

Retained IOKit catalog/property, provider-plane, registry/service, match, scaffold, pexpert, and pmap transition source facts also remained OK:

```text
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_catalog_property_dryrun_contract_status=0x69000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_provider_plane_dryrun_contract_status=0x69000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_registry_service_dryrun_contract_status=0x69000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_match_dryrun_contract_status=0x69000001
MI4IOS6_STAGE69_XNU stage69_xnu_iokit_platform_scaffold_contract_status=0x69000001
MI4IOS6_STAGE69_XNU stage69_xnu_pmap_transition_dryrun_contract_status=0x69000001
MI4IOS6_STAGE69_XNU stage69_xnu_pexpert_hook_readiness_contract_status=0x69000001
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

Final loader/handoff markers:

```text
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_status=0x69000001
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_checksum=0xceb8b00b
MI4IOS6_STAGE69_XNU loader_xnu_iokit_property_inheritance_dryrun_contract_status_rollup=0x69000001
MI4IOS6_STAGE69_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE69_XNU loader_status=0x69000001
MI4IOS6_STAGE69_XNU kernel_entry ok
MI4IOS6_STAGE69 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, or `mmu high bootstrap selftest failed` in the successful run.

## Result

Stage69 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit property-inheritance / registry-entry dry-run over the retained IOKit catalog/property/personality proof, retained provider-plane publication proof, retained IOKit/platform scaffold, retained local service/driver match dry-run, retained broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit registry-plane/IODeviceTree relationship proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/attach/start runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
