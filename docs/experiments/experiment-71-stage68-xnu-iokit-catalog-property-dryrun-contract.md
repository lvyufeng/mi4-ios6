# Experiment 71 — Stage68 XNU IOKit catalog/property dry-run contract

## Goal

Stage68 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage67 local IOKit provider-plane publish/order dry-run toward a local catalog/property/personality proof.

The goal is not to run XNU, not to execute IOKit, not to execute catalog or property-plane runtime code, not to execute provider-plane runtime code, not to execute a platform driver, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the Stage67 provider-plane publications can be projected into a deterministic local IOKit catalog/personality/property model: four intended MSM8974 personalities are selected in order and one explicit negative personality remains rejected, with attach/start still deferred.

## Safety boundary

Stage68 preserves all Stage67 boundaries:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, or platform-driver runtime execution.
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
stage68/xnu_object_shims.c
```

Public ARM VM/pmap files and public IOKit files remain reference-only. The public IOKit reference-only set is:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, linked, or executed.

## Implementation

Stage68 adds:

```text
stage68/xnu_iokit_catalog_property_dryrun_contract.c
struct stage68_xnu_iokit_catalog_property_dryrun_contract
```

The new contract imports source facts from:

- the Stage68 IOKit provider-plane dry-run contract,
- the Stage68 broader IOKit registry/service dry-run contract,
- the Stage68 IOKit/platform scaffold contract,
- the Stage68 single-match IOKit dry-run contract,
- the Stage68 pexpert hook readiness contract,
- the Stage68 public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

Stage68 keeps the Stage67 provider/service/candidate nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
```

It adds local catalog/personality nodes:

```text
/iokit-catalog-property-dryrun
/stage68-platform-personality
/stage68-interrupt-personality
/stage68-timer-personality
/stage68-cpu-personality
/stage68-rejected-personality
```

The catalog/property dry-run proves:

- Stage67-renumbered provider-plane dry-run is OK.
- Stage66-renumbered IOKit broader registry/service dry-run is OK.
- Stage65-renumbered IOKit single-match dry-run is OK.
- Stage64-renumbered IOKit/platform scaffold readiness is OK.
- Stage63-renumbered pexpert hook readiness is OK.
- Public compile graph/object subset/link facts are OK.
- Public IOKit reference mask is complete (`0x00000007`).
- Public IOKit runtime-blocked mask is complete (`0x00000007`).
- Public IOKit compile/link counts are zero.
- The catalog and five personality Apple-DT nodes remain present.
- The provider-plane selected mask (`0x0000000f`) and rejected mask (`0x00000010`) are imported unchanged.
- Four selected local personalities satisfy name, bundle identifier, catalog marker, provider class, category, compatible, probe-score, and local-only property checks.
- The negative personality remains rejected and has rejected ordinal `0xffffffff`.
- The platform/interrupt/timer/CPU ordinals are deterministic (`0`, `1`, `2`, `3`).
- Exactly one catalog and five personality candidates are considered.
- Exactly four selected personality dry-runs occur and exactly one rejected personality is recorded.
- Attach and start remain deferred for all selected dry-run personalities.
- Public pexpert/platform runtime, public IOKit runtime, IOKit match/registry/service/provider-plane/catalog/property-plane runtime, Stage-owned platform-driver runtime, public VM/pmap runtime, generated Mach-O execution, XNU entry execution, proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage68 does not add a 33rd top-level loader bit. Instead, it records the catalog/property dry-run as a contract roll-up copied into loader preflight. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x68000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage68/build.sh
```

Final build size:

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

Boot image parse checks confirmed:

```text
stage68.img:      page_size=2048 kernel_size=300672 (0x49680) dt_size=0
stage68-qcdt.img: page_size=2048 kernel_size=300672 (0x49680) dt_size=2521088 (0x267800)
```

Undefined symbol checks were clean for both:

```text
out/stage68/stage68.elf
out/stage68/xnu-link/stage68-xnu-link.elf
```

Static validation facts:

```text
stage68/boot_args.c command_line_len_with_nul=242 fits256=True
stage68/stage68_main.c command_line_len_with_nul=242 fits256=True
stage68/xnu_object_shims.c command_line_len_with_nul=242 fits256=True
stage68_source_stale_marker_violations=0
out/stage68 ignored
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

The first generated boot image command line exceeded Android boot image command-line length (`max 1536, got 1563`). The retained build script uses shortened boot-image markers while preserving the fixed public ARM `CommandLine[256]` strings at 242 bytes including NUL.

## Hardware validation

Hardware validation used non-persistent boot only:

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
stage68_marker_count=2826
stage68_xnu_marker_count=2788
```

The first Stage68 hardware pass exposed a stale high-DT child-count expectation after the six catalog/personality nodes were added. The new Apple-DT root child count is `0x13` (19), and `stage68/mmu.c` now validates 19 rather than the old 13. The successful validation confirms the high-DT summary is accepted.

Key IOKit catalog/property dry-run markers:

```text
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_status=0x68000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_required_mask=0x7fffffff
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_catalog_property_dryrun_contract_checksum=0x6fffd084
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
MI4IOS6_STAGE68_XNU loader_xnu_iokit_catalog_property_dryrun_contract_status_rollup=0x68000001
```

Retained IOKit provider-plane, registry/service, match, scaffold, pexpert, and pmap transition source facts also remained OK:

```text
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_provider_plane_dryrun_contract_status=0x68000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_registry_service_dryrun_contract_status=0x68000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_match_dryrun_contract_status=0x68000001
MI4IOS6_STAGE68_XNU stage68_xnu_iokit_platform_scaffold_contract_status=0x68000001
MI4IOS6_STAGE68_XNU stage68_xnu_pmap_transition_dryrun_contract_status=0x68000001
MI4IOS6_STAGE68_XNU stage68_xnu_pexpert_hook_readiness_contract_status=0x68000001
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

Final success markers:

```text
MI4IOS6_STAGE68_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE68_XNU loader_status=0x68000001
MI4IOS6_STAGE68_XNU kernel_entry ok
MI4IOS6_STAGE68_XNU Stage68 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE68 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, or `mmu high bootstrap selftest failed` in the successful run.

## Result

Stage68 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit catalog/property/personality dry-run over the retained IOKit/platform scaffold, the local service/driver match dry-run, the broader registry/service dry-run, the provider-plane publish/order dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit property inheritance/registry-plane proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
