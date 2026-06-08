# Experiment 70 — Stage67 XNU IOKit provider-plane dry-run contract

## Goal

Stage67 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage66 broader local IOKit registry/service dry-run toward a local provider-plane publication/order/dependency proof.

The goal is not to run XNU, not to execute IOKit, not to execute a provider-plane runtime, not to execute a platform driver, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the Stage66-selected local provider/service candidates can be projected into a deterministic provider-plane publication model: four intended MSM8974 service candidates publish in order under the local provider and one explicit negative candidate remains unpublished, with attach/start still deferred.

## Safety boundary

Stage67 preserves all Stage66 boundaries:

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
stage67/xnu_object_shims.c
```

Public ARM VM/pmap files and public IOKit files remain reference-only. The public IOKit reference-only set is:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, linked, or executed.

## Implementation

Stage67 adds:

```text
stage67/xnu_iokit_provider_plane_dryrun_contract.c
struct stage67_xnu_iokit_provider_plane_dryrun_contract
```

The new contract imports source facts from:

- the Stage67 broader IOKit registry/service dry-run contract,
- the Stage67 IOKit/platform scaffold contract,
- the Stage67 single-match IOKit dry-run contract,
- the Stage67 pexpert hook readiness contract,
- the Stage67 public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

Stage67 keeps the Stage66 Apple-DT scaffold and service nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
```

The provider-plane dry-run proves:

- Stage66-renumbered IOKit broader registry/service dry-run is OK.
- Stage65-renumbered IOKit single-match dry-run is OK.
- Stage64-renumbered IOKit/platform scaffold readiness is OK.
- Stage63-renumbered pexpert hook readiness is OK.
- Public compile graph/object subset/link facts are OK.
- Public IOKit reference mask is complete (`0x00000007`).
- Public IOKit runtime-blocked mask is complete (`0x00000007`).
- Public IOKit compile/link counts are zero.
- The provider, service, and rejected-candidate Apple-DT nodes remain present.
- The registry/service selected mask (`0x0000000f`) and rejected mask (`0x00000010`) are imported unchanged.
- Four selected local service candidates satisfy provider-parent, dependency, service-fact, and publish-order checks.
- The negative candidate remains unpublished and has rejected publish ordinal `0xffffffff`.
- The root/platform/interrupt/timer/CPU ordinals are deterministic (`0`, `1`, `2`, `3`, `4`).
- Exactly one provider candidate and five service candidates are considered.
- Exactly four dry-run publications occur and exactly one unpublished candidate is recorded.
- Attach and start remain deferred for all published dry-run services.
- Public pexpert/platform runtime, public IOKit runtime, IOKit provider-plane runtime, Stage-owned platform-driver runtime, public VM/pmap runtime, generated Mach-O execution, XNU entry execution, proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage67 does not add a 33rd top-level loader bit. Instead, it records the provider-plane dry-run as a contract roll-up copied into loader preflight. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x67000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage67/build.sh
```

Final build size:

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

Boot image parse checks confirmed:

```text
stage67.img:      page_size=2048 kernel_size=289376 (0x46a60) dt_size=0
stage67-qcdt.img: page_size=2048 kernel_size=289376 (0x46a60) dt_size=2521088 (0x267800)
```

Undefined symbol checks were clean for both:

```text
out/stage67/stage67.elf
out/stage67/xnu-link/stage67-xnu-link.elf
```

Static validation facts:

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

Hardware validation used non-persistent boot only:

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

The first Stage67 hardware pass exposed stale materialized Mach-O marker expectations (`ST66-*` versus generated `ST67-*`). The source now verifies `ST67-TEXT`, `ST67-DATA`, and `ST67-PRELINK-TEXT`, and the successful validation confirms:

```text
MI4IOS6_STAGE67_XNU macho_staging_marker_mask=0x00000007
MI4IOS6_STAGE67_XNU macho_staging_failure_mask=0x00000000
MI4IOS6_STAGE67_XNU macho_staging_status=0x67000001
MI4IOS6_STAGE67_XNU loader_materialized_status=0x67000001
```

Key IOKit provider-plane dry-run markers:

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
MI4IOS6_STAGE67_XNU loader_xnu_iokit_provider_plane_dryrun_contract_status_rollup=0x67000001
```

Retained IOKit scaffold, single-match dry-run, registry/service dry-run, pexpert, and pmap transition source facts also remained OK:

```text
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_platform_scaffold_contract_status=0x67000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_match_dryrun_contract_status=0x67000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_registry_service_dryrun_contract_status=0x67000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_registry_service_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_pmap_transition_dryrun_contract_status=0x67000001
MI4IOS6_STAGE67_XNU stage67_xnu_pexpert_hook_readiness_contract_status=0x67000001
MI4IOS6_STAGE67_XNU stage67_xnu_pexpert_hook_readiness_contract_failure_mask=0x00000000
```

Negative safety markers remained clear:

```text
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_no_provider_runtime_exec=0x00000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_proposed_workspace_written=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_caches_changed=0x00000000
MI4IOS6_STAGE67_XNU stage67_xnu_iokit_provider_plane_persistent_write_attempted=0x00000000
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

Stage67 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit provider-plane publish/order/dependency dry-run over the retained IOKit/platform scaffold, the local service/driver match dry-run, the broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit registry/catalog/property-plane proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit provider-plane runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
