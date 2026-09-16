# Experiment 68 — Stage65 XNU IOKit service/driver match dry-run contract

## Goal

Stage65 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage64 IOKit/platform-driver scaffold readiness proof toward the next local IOKit blocker: service/driver matching.

The goal is not to run XNU, not to execute IOKit, not to execute a platform driver, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the Apple flattened device tree now exposes local provider/driver match facts and that a single MSM8974 platform-driver candidate can be selected in a dry-run while attach/start remain deferred.

## Safety boundary

Stage65 preserves all Stage64 boundaries:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No Stage-owned MSM8974 platform-driver execution.
- No IOKit service-match runtime execution.
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
stage65/xnu_object_shims.c
```

Public ARM VM/pmap files and public IOKit files remain reference-only. The public IOKit reference-only set is:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, linked, or executed.

## Implementation

Stage65 adds:

```text
stage65/xnu_iokit_match_dryrun_contract.c
struct stage65_xnu_iokit_match_dryrun_contract
```

The contract imports source facts from:

- the Stage65 IOKit/platform scaffold contract,
- the Stage65 pexpert hook readiness contract,
- the Stage65 public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

Stage65 keeps the Stage64 Apple-DT scaffold nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
```

`/iokit-platform-scaffold` records a local registry-plane provider shape:

```text
compatible=apple,iokit-platform-scaffold
IOClass=IOPlatformExpertDevice
IOProviderClass=IODeviceTree:/
device_type=platform
registry-plane=IODeviceTree
```

`/msm8974-platform-driver` records Stage-owned MSM8974 platform-driver match facts only:

```text
compatible=qcom,msm8974-cancro-stage65
IOClass=MSM8974PlatformExpert
IOProviderClass=IOPlatformExpertDevice
IOMatchCategory=Stage65LocalPlatformScaffold
IOProbeScore=0x00000650
reg=<0xf9000000 0x1000 0xf9020000 0x1000>
cpu-count=4
timebase-frequency=19200000
stage-owned-local-only=1
```

The match dry-run proves:

- Stage64-renumbered IOKit/platform scaffold readiness is OK.
- Stage63-renumbered pexpert hook readiness is OK.
- Public compile graph/object subset/link facts are OK.
- Public IOKit reference mask is complete (`0x00000007`).
- Public IOKit runtime-blocked mask is complete (`0x00000007`).
- Public IOKit compile/link counts are zero.
- The provider and driver Apple-DT nodes are present.
- The provider and driver properties match the expected local-only values.
- The expected local probe score is `0x00000650`.
- Exactly one provider candidate and one driver candidate are considered.
- Exactly one dry-run match is selected.
- Attach and start remain deferred.
- Public pexpert/platform runtime, public IOKit runtime, IOKit service-match runtime, Stage-owned platform-driver runtime, public VM/pmap runtime, generated Mach-O execution, XNU entry execution, proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage65 does not add a 33rd top-level loader bit. Instead, it records the IOKit match dry-run as a contract roll-up copied into loader preflight. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x65000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage65/build.sh
```

Final build size:

```text
text=270228 data=0 bss=398740 dec=668968 hex=a3528
```

Hashes:

```text
03d34fbac9fa56ebf1185228be366526ddbce259a0d5ced740179f5282e0b4a2  out/stage65/stage65_fixture.macho
4ae534bb2db767049a7fd0de2c882d39445eb66cac18c9fb2e403981226f58e2  out/stage65/stage65.elf
3dd24ff32f96b87d117d0dd2b7bfd0327064b0725be86d12fb5898bdf55ebdd8  out/stage65/stage65.bin
f742d1144c46a4b181f5401065b438a03bffe8cb8de190c9179ea7b9e11b9f36  out/stage65/stage65.img
6e063c5738c66002075ea4b2451a6413b6d6e8d63de20b72fb1d5febff3ac740  out/stage65/stage65-qcdt.img
```

Boot image parse checks confirmed:

```text
stage65.img:      page_size=2048 kernel_size=270228 dt_size=0
stage65-qcdt.img: page_size=2048 kernel_size=270228 dt_size=2521088 (0x267800)
```

Undefined symbol checks were clean for both:

```text
out/stage65/stage65.elf
out/stage65/xnu-link/stage65-xnu-link.elf
```

Static validation facts:

```text
stage65/boot_args.c command_line_len_with_nul=210 fits256=True
stage65/stage65_main.c command_line_len_with_nul=210 fits256=True
stage65/xnu_object_shims.c command_line_len_with_nul=179 fits256=True
stale_marker_violations=0
public_vm_pmap_iokit_command_violations=0
out/stage65 ignored
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

## Hardware validation

Hardware validation used non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage65/stage65-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage65-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage65-last_kmsg.txt
size=184957 bytes
stage65_marker_count=2632
stage65_xnu_marker_count=2604
```

Key IOKit match dry-run markers:

```text
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_contract_status=0x65000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_contract_required_mask=0x03ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_contract_satisfied_mask=0x03ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_contract_checksum=0xf9646bb0
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_provider_class_match=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_provider_path_match=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_category_match=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_compatible_match=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_driver_probe_score=0x00000650
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_dryrun_match_count=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_attach_deferred=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_start_deferred=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_iokit_reference_mask=0x00000007
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_iokit_runtime_blocked_mask=0x00000007
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_public_compile_count=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_public_link_count=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_no_iokit_runtime_exec=0x00000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_no_platform_driver_exec=0x00000001
MI4IOS6_STAGE65_XNU loader_xnu_iokit_match_dryrun_contract_status_rollup=0x65000001
```

Retained IOKit scaffold, pexpert, and pmap transition source facts also remained OK:

```text
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_platform_scaffold_contract_status=0x65000001
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_platform_scaffold_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_platform_scaffold_contract_failure_mask=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_pmap_transition_dryrun_contract_status=0x65000001
MI4IOS6_STAGE65_XNU stage65_xnu_pmap_transition_dryrun_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_pmap_transition_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_pexpert_hook_readiness_contract_status=0x65000001
MI4IOS6_STAGE65_XNU stage65_xnu_pexpert_hook_readiness_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE65_XNU stage65_xnu_pexpert_hook_readiness_contract_failure_mask=0x00000000
```

Negative safety markers remained clear:

```text
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_no_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_proposed_workspace_written=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_pmap_ttbr_written=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_pmap_tlbs_invalidated=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_caches_changed=0x00000000
MI4IOS6_STAGE65_XNU stage65_xnu_iokit_match_persistent_write_attempted=0x00000000
```

Final success markers:

```text
MI4IOS6_STAGE65_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE65_XNU loader_status=0x65000001
MI4IOS6_STAGE65_XNU kernel_entry ok
MI4IOS6_STAGE65 kernel_entry returned success
```

No `kernel_entry returned failure` or `mmu high bootstrap selftest failed` marker was present in the successful run.

## Result

Stage65 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned local IOKit service/driver match dry-run while preserving every no-XNU/no-public-runtime/no-I/O-Kit-runtime/no-service-match-runtime/no-live-pmap-install/no-persistent-write boundary from Stage64.

The next safe direction is a still-local broader IOKit registry/service matching dry-run, deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof before any attempt to jump into XNU, execute public runtime code, execute IOKit matching runtime code, install proposed live pmap tables, invalidate TLBs, change cache policy, or perform persistent writes.
