# Experiment 69 — Stage66 XNU IOKit registry/service dry-run contract

## Goal

Stage66 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage65 single local IOKit service/driver match dry-run toward a broader local IOKit registry/service matching proof.

The goal is not to run XNU, not to execute IOKit, not to execute a platform driver, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the Apple flattened device tree now exposes multiple local provider/service/candidate facts and that four MSM8974 service candidates can be selected in a dry-run while one negative candidate is rejected and attach/start remain deferred.

## Safety boundary

Stage66 preserves all Stage65 boundaries:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No Stage-owned MSM8974 platform-driver execution.
- No IOKit service-match or registry/service runtime execution.
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
stage66/xnu_object_shims.c
```

Public ARM VM/pmap files and public IOKit files remain reference-only. The public IOKit reference-only set is:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, linked, or executed.

## Implementation

Stage66 adds:

```text
stage66/xnu_iokit_registry_service_dryrun_contract.c
struct stage66_xnu_iokit_registry_service_dryrun_contract
```

The new contract imports source facts from:

- the Stage66 IOKit/platform scaffold contract,
- the Stage66 single-match IOKit dry-run contract,
- the Stage66 pexpert hook readiness contract,
- the Stage66 public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

Stage66 keeps the Stage64/Stage65 Apple-DT scaffold nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
```

It expands the root from 9 to 13 children and adds four local-only service/candidate nodes:

```text
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
```

The selected local service candidates are:

```text
/msm8974-platform-driver:
  compatible=qcom,msm8974-cancro-stage66
  IOClass=MSM8974PlatformExpert
  IOProviderClass=IOPlatformExpertDevice
  IOMatchCategory=Stage66LocalPlatformScaffold
  IOProbeScore=0x00000650
  stage-owned-local-only=1

/msm8974-interrupt-service:
  compatible=qcom,msm8974-gic-stage66
  IOClass=MSM8974InterruptController
  IOProviderClass=IOPlatformExpertDevice
  IOMatchCategory=Stage66LocalInterruptService
  IOProbeScore=0x00000660
  irq-count=288
  interrupt-controller=1
  stage-owned-local-only=1

/msm8974-timer-service:
  compatible=qcom,msm8974-timer-stage66
  IOClass=MSM8974Timer
  IOProviderClass=IOPlatformExpertDevice
  IOMatchCategory=Stage66LocalTimerService
  IOProbeScore=0x00000661
  frequency=19200000
  timer-ppi-mask=0x000c0000
  stage-owned-local-only=1

/msm8974-cpu-service:
  compatible=qcom,msm8974-cpu-stage66
  IOClass=IOCPU
  IOProviderClass=IOPlatformExpertDevice
  IOMatchCategory=Stage66LocalCPUService
  IOProbeScore=0x00000662
  reg=<0 1 2 3>
  cpu-count=4
  timebase-frequency=19200000
  stage-owned-local-only=1
```

The explicit negative candidate is:

```text
/msm8974-rejected-driver:
  compatible=qcom,msm8974-rejected-stage66
  IOClass=Stage66RejectedDriver
  IOProviderClass=IOUnknownProvider
  IOMatchCategory=Stage66RejectedLocalService
  IOProbeScore=0x00000000
  rejected-candidate=1
  stage-owned-local-only=1
```

The registry/service dry-run proves:

- Stage65-renumbered IOKit single-match dry-run is OK.
- Stage64-renumbered IOKit/platform scaffold readiness is OK.
- Stage63-renumbered pexpert hook readiness is OK.
- Public compile graph/object subset/link facts are OK.
- Public IOKit reference mask is complete (`0x00000007`).
- Public IOKit runtime-blocked mask is complete (`0x00000007`).
- Public IOKit compile/link counts are zero.
- The provider, service, and rejected-candidate Apple-DT nodes are present.
- Provider class, category, compatible, probe-score, and local-only masks select the four intended services.
- The negative candidate is rejected through provider/probe mismatch accounting.
- Exactly one provider candidate and five driver candidates are considered.
- Exactly four dry-run matches are selected and exactly one rejected candidate is recorded.
- Attach and start remain deferred for all selected dry-run matches.
- Public pexpert/platform runtime, public IOKit runtime, IOKit registry/service runtime, Stage-owned platform-driver runtime, public VM/pmap runtime, generated Mach-O execution, XNU entry execution, proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage66 does not add a 33rd top-level loader bit. Instead, it records the registry/service dry-run as a contract roll-up copied into loader preflight. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x66000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage66/build.sh
```

Final build size:

```text
text=281804 data=0 bss=403248 dec=685052 hex=a73fc
```

Hashes:

```text
599d1d7fb365aef15ff1cbcf036767b1b25f2782db970602c24c23f617ef2269  out/stage66/stage66_fixture.macho
d41622681d97ecafb23f8d3e408d76e1b4adb2dfc7721c6d27387aae426e17e3  out/stage66/stage66.elf
57a1842f71a5679dedef3d96093d0bfa5a0dacd5d59f4d90cf2e521667d84d25  out/stage66/stage66.bin
2d9bad388826612980a8957dc9b1e87bdef8d492817a1a5cfc4fe4651bce857d  out/stage66/stage66.img
7c082e8eb986deaa0115c3cc6b23627f1864a69b0391070995b9c79d5dff9f90  out/stage66/stage66-qcdt.img
```

Boot image parse checks confirmed:

```text
stage66.img:      page_size=2048 kernel_size=281804 (0x44ccc) dt_size=0
stage66-qcdt.img: page_size=2048 kernel_size=281804 (0x44ccc) dt_size=2521088 (0x267800)
```

Undefined symbol checks were clean for both:

```text
out/stage66/stage66.elf
out/stage66/xnu-link/stage66-xnu-link.elf
```

Static validation facts:

```text
stage66/boot_args.c command_line_len_with_nul=232 fits256=True
stage66/stage66_main.c command_line_len_with_nul=232 fits256=True
stage66/xnu_object_shims.c command_line_len_with_nul=201 fits256=True
stage66_source_stale_marker_violations=0
public_vm_pmap_iokit_command_violations=0
out/stage66 ignored
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

## Hardware validation

Hardware validation used non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage66/stage66-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage66-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage66-last_kmsg.txt
size=190399 bytes
stage66_marker_count=2695
stage66_xnu_marker_count=2663
```

Key IOKit registry/service dry-run markers:

```text
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_contract_status=0x66000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_contract_required_mask=0x07ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_contract_checksum=0x9dd7207f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_provider_class_match_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_category_match_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_compatible_match_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_probe_score_match_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_selected_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_rejected_mask=0x00000010
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_fact_mask=0x0000000f
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_selected_driver_count=0x00000004
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_rejected_driver_count=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_dryrun_match_count=0x00000004
MI4IOS6_STAGE66_XNU loader_xnu_iokit_registry_service_dryrun_contract_status_rollup=0x66000001
```

Retained IOKit scaffold, single-match dry-run, pexpert, and pmap transition source facts also remained OK:

```text
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_platform_scaffold_contract_status=0x66000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_platform_scaffold_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_platform_scaffold_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_dryrun_contract_status=0x66000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_dryrun_contract_satisfied_mask=0x03ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_match_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_pmap_transition_dryrun_contract_status=0x66000001
MI4IOS6_STAGE66_XNU stage66_xnu_pmap_transition_dryrun_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_pmap_transition_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_pexpert_hook_readiness_contract_status=0x66000001
MI4IOS6_STAGE66_XNU stage66_xnu_pexpert_hook_readiness_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE66_XNU stage66_xnu_pexpert_hook_readiness_contract_failure_mask=0x00000000
```

Negative safety markers remained clear:

```text
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_proposed_workspace_written=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_caches_changed=0x00000000
MI4IOS6_STAGE66_XNU stage66_xnu_iokit_registry_service_persistent_write_attempted=0x00000000
```

Final success markers:

```text
MI4IOS6_STAGE66_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE66_XNU loader_status=0x66000001
MI4IOS6_STAGE66_XNU kernel_entry ok
MI4IOS6_STAGE66_XNU Stage66 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE66 kernel_entry returned success
```

No `kernel_entry returned failure` or `mmu high bootstrap selftest failed` marker was present in the successful run.

## Result

Stage66 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned broader IOKit registry/service dry-run while preserving every no-XNU/no-public-runtime/no-IOKit-runtime/no-registry-service-runtime/no-live-pmap-install/no-persistent-write boundary from Stage65.
